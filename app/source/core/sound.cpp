// Sound through ndsp. UI sounds are pre-rendered PCM (romfs:/audio/sfx.bin), so playing them costs next
// to nothing. Music is decoded on a small thread:
// the app's own loops (romfs:/audio/trackN.bcstm, made by tools/make_music.py), a theme's BCSTM straight
// out of its zip, or the Ogg preview that Theme Plaza serves for items not downloaded yet.
#include "sound.h"
#include <3ds.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <tremor/ivorbisfile.h>
#include "audio_gen.h"
#include "backend.h"
#include "bcstm.h"
#include "log.h"
#include "model.h"
#include "pack.h"
#include "worker.h"

namespace sound {

namespace {

enum { CH_MUSIC = 0, CH_SFX0 = 1, SFX_CHANNELS = 4, CH_THEME = 5 };
constexpr int STREAM_FRAMES = 4096, STREAM_BUFS = 3;
// A theme's own music is mastered much louder than the app's sounds (measured over 14 themes: RMS 0.10 to 0.35 of
// full scale, median 0.21; the app's music loops are at 0.047, its UI sounds around 0.06). The design plays
// both kinds of music at about the same level, so the theme's is turned down to sit just above the app's.
constexpr float THEME_GAIN = 0.28f, THEME_DELAY = 0.8f;

bool g_on = false;
s16* g_sfxPcm = nullptr;
ndspWaveBuf g_sfxBuf[SFX_CHANNELS];
int g_sfxNext = 0;

constexpr int TRACKS = 3;
int g_track = MUSIC_SHUFFLE;     // the setting: a track, MUSIC_SHUFFLE or MUSIC_OFF
int g_now = -1;                  // the track to play (under Shuffle: the one drawn for now)
bool g_leaving = false;          // Shuffle: fading the track out to make way for the next
float g_musicGain = 0;
// Under Shuffle a track plays through twice (1.5 to 2 minutes) and then gives way to another.
constexpr int SHUFFLE_LOOPS = 2;
constexpr float SHUFFLE_FADE = 3.0f;      // seconds before the end at which it starts to fade
bool g_musicWanted = true;       // false while a theme's own music has the floor

Item* g_want = nullptr;          // the item whose music should play
enum ThemeState { TH_IDLE, TH_WAITING, TH_FETCHING, TH_PLAYING, TH_FAILED };
ThemeState g_theme = TH_IDLE;
float g_wantTimer = 0;
int g_wantGen = 0;
float g_themeGain = 0;

// One of the tracks at random, other than `not`.
int draw(int not_) {
    static u32 seed = 0;
    if (!seed) seed = (u32)svcGetSystemTick() | 1;
    int t;
    do { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; t = (int)(seed % TRACKS); } while (t == not_);
    return t;
}

void setVolume(int ch, float v) {
    float mix[12] = {};
    mix[0] = mix[1] = v;
    ndspChnSetMix(ch, mix);
}

void play(int id) {
    if (!g_on || id < 0 || id >= SFX_COUNT) return;
    int i = g_sfxNext; g_sfxNext = (g_sfxNext + 1) % SFX_CHANNELS;
    ndspChnWaveBufClear(CH_SFX0 + i);
    ndspWaveBuf& b = g_sfxBuf[i];
    memset(&b, 0, sizeof b);
    b.data_pcm16 = g_sfxPcm + g_sfx[id].offset;
    b.nsamples = g_sfx[id].samples;
    ndspChnWaveBufAdd(CH_SFX0 + i, &b);
}

// ---------- the Ogg preview of a Theme Plaza item (the BCSTM decoder is in bcstm.cpp) ----------
using Bytes = std::shared_ptr<StreamBuf>;
volatile bool g_quit = false;   // the app is closing
volatile int g_reqGen = 0;      // goes up with every request to the stream thread (changed under g_reqLock)

// Decodes from memory while the download is still running: the preview never touches the SD card, and it
// starts playing as soon as its first part has arrived. Read as a plain forward stream (no seeking).
struct Ogg : Decoder {
    OggVorbis_File vf; bool opened = false;
    Bytes data; size_t at = 0; int gen = 0;
    ~Ogg() override { if (opened) ov_clear(&vf); }
    static size_t readCb(void* dst, size_t size, size_t count, void* user) {
        Ogg* o = (Ogg*)user;
        size_t want = size * count;
        for (;;) {
            LightLock_Lock(&o->data->lock);
            size_t left = o->data->data.size() - o->at, n = want < left ? want : left;
            if (n) memcpy(dst, o->data->data.data() + o->at, n);
            bool finished = o->data->finished;
            LightLock_Unlock(&o->data->lock);
            if (n) { o->at += n; return size ? n / size : 0; }
            if (finished || o->gen != g_reqGen || g_quit) return 0;   // the end, something else was asked for meanwhile, or the app is closing
            svcSleepThread(15 * 1000 * 1000LL);                 // wait for the download to bring more
        }
    }
    bool openAt(size_t start) {
        at = start;
        ov_callbacks cb = {readCb, nullptr, nullptr, nullptr};
        if (ov_open_callbacks(this, &vf, nullptr, 0, cb) < 0) return false;
        opened = true;
        return true;
    }
    bool open(const Bytes& bytes, int forGen) {
        if (!bytes) return false;
        data = bytes; gen = forGen;
        if (!openAt(0)) return false;
        vorbis_info* vi = ov_info(&vf, -1);
        rate = (int)vi->rate; channels = vi->channels;
        return channels == 1 || channels == 2;
    }
    size_t read(s16* out, size_t frames) override {
        size_t bytes = frames * channels * 2, done = 0; int section = 0;
        while (done < bytes) {
            long n = ov_read(&vf, (char*)out + done, (int)(bytes - done), &section);
            if (n <= 0) break;
            done += (size_t)n;
        }
        return done / (channels * 2);
    }
    bool rewind() override {
        if (gen != g_reqGen) return false;
        if (opened) { ov_clear(&vf); opened = false; }
        return openAt(0);
    }
};

// ---------- the stream thread ----------
Thread g_thread = nullptr;
LightEvent g_wake;
LightLock g_reqLock;
std::string g_reqSource; Bytes g_reqOgg;   // guarded by g_reqLock: a theme's zip or folder, or a downloaded Ogg
s16* g_streamData[STREAM_BUFS];
ndspWaveBuf g_streamBuf[STREAM_BUFS];
volatile bool g_streamPlaying = false;
volatile int g_failGen = -1;    // the request (g_reqGen) whose music could not be opened
// the app's own music, streamed the same way on its own channel
volatile int g_musicReq = -1;         // the track that should be open (-1: none)
volatile bool g_musicFeed = false;    // false while it is silent: nothing is decoded for it then, and it resumes where it stopped
volatile bool g_musicPlaying = false; // the track is open and its channel set up
volatile int g_musicOpen = -1;        // which track that is
volatile u32 g_musicFed = 0, g_musicLength = 0, g_musicRate = 32000;   // frames queued since it was opened; frames in one pass; per second
s16* g_musicData[STREAM_BUFS];
ndspWaveBuf g_musicWb[STREAM_BUFS];

void setUp(int ch, Decoder* dec) {
    ndspChnReset(ch);
    ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
    ndspChnSetRate(ch, (float)dec->rate);
    ndspChnSetFormat(ch, dec->channels == 2 ? NDSP_FORMAT_STEREO_PCM16 : NDSP_FORMAT_MONO_PCM16);
    setVolume(ch, 0);
}

// Tops up a channel's queue from its decoder, starting again from the beginning at the end.
// False: the decoder has nothing to give even from the start.
bool feed(int ch, Decoder* dec, ndspWaveBuf* bufs, s16** data, volatile u32* fed = nullptr) {
    for (int i = 0; i < STREAM_BUFS; i++) {
        ndspWaveBuf& b = bufs[i];
        if (b.status != NDSP_WBUF_FREE && b.status != NDSP_WBUF_DONE) continue;
        size_t n = dec->read(data[i], STREAM_FRAMES);
        if (n == 0) {
            if (!dec->rewind()) return true;
            n = dec->read(data[i], STREAM_FRAMES);
            if (n == 0) return false;
        }
        memset(&b, 0, sizeof b);
        b.data_pcm16 = data[i]; b.nsamples = n;
        DSP_FlushDataCache(data[i], n * dec->channels * 2);
        ndspChnWaveBufAdd(ch, &b);
        if (fed) *fed = *fed + (u32)n;
    }
    return true;
}

void streamThread(void*) {
    int gen = 0; Decoder* dec = nullptr;
    int track = -1; Bcstm* music = nullptr;
    while (!g_quit) {
        int wantTrack = g_musicReq;
        if (wantTrack != track) {
            track = wantTrack;
            delete music; music = nullptr;
            g_musicPlaying = false; g_musicOpen = -1;
            ndspChnWaveBufClear(CH_MUSIC);
            for (auto& b : g_musicWb) b.status = NDSP_WBUF_FREE;
            if (track >= 0) {
                char name[24]; snprintf(name, sizeof name, "track%d.bcstm", track);
                Bcstm* b = new Bcstm;
                if (b->open("romfs:/audio", name)) {
                    music = b; setUp(CH_MUSIC, music);
                    g_musicFed = 0; g_musicLength = music->frames; g_musicRate = (u32)music->rate;
                    g_musicOpen = track; g_musicPlaying = true;
                }
                else { delete b; LOG("app music: could not open %s", name); }
            }
        }
        if (music && g_musicFeed && !feed(CH_MUSIC, music, g_musicWb, g_musicData, &g_musicFed)) { delete music; music = nullptr; g_musicPlaying = false; }
        LightLock_Lock(&g_reqLock);
        int reqGen = g_reqGen; std::string src = g_reqSource; Bytes ogg = g_reqOgg;
        LightLock_Unlock(&g_reqLock);
        if (reqGen != gen) {
            gen = reqGen;
            delete dec; dec = nullptr;
            ndspChnWaveBufClear(CH_THEME);
            g_streamPlaying = false;
            for (auto& b : g_streamBuf) b.status = NDSP_WBUF_FREE;
            if (!src.empty() || ogg) {
                if (ogg) { Ogg* o = new Ogg; if (o->open(ogg, gen)) dec = o; else delete o; }
                else { Bcstm* b = new Bcstm; if (b->open(src)) dec = b; else delete b; }
                if (dec) { setUp(CH_THEME, dec); g_streamPlaying = true; }
                else { g_failGen = gen; LOG("theme music: could not open %s", ogg ? "the downloaded preview" : src.c_str()); }
            }
        }
        // nothing even from the start (a cut-off file): give up on it rather than reopen it for ever
        if (dec && !feed(CH_THEME, dec, g_streamBuf, g_streamData) && gen == g_reqGen) {
            g_failGen = gen; delete dec; dec = nullptr; g_streamPlaying = false; LOG("theme music: no sound data in it");
        }
        if (!dec && !(music && g_musicFeed)) { LightEvent_Wait(&g_wake); continue; }
        LightEvent_WaitTimeout(&g_wake, 25 * 1000 * 1000LL);
    }
    delete dec; delete music;
}

void request(const std::string& source, const Bytes& ogg) {
    LightLock_Lock(&g_reqLock);
    g_reqGen = g_reqGen + 1; g_reqSource = source; g_reqOgg = ogg;
    LightLock_Unlock(&g_reqLock);
    LightEvent_Signal(&g_wake);
}

void stopTheme() {
    if (g_theme == TH_FETCHING || g_theme == TH_PLAYING) backend::cancelBgmPreview();
    if (g_theme == TH_PLAYING) request("", nullptr);
    g_theme = TH_IDLE; g_themeGain = 0;
}

}  // namespace

void init() {
    if (R_FAILED(ndspInit())) { LOG("ndspInit failed: no sound (is sdmc:/3ds/dspfirm.cdc there?)"); return; }
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    for (int ch = 0; ch <= CH_THEME; ch++) {
        ndspChnReset(ch);
        ndspChnSetInterp(ch, NDSP_INTERP_LINEAR);
        ndspChnSetRate(ch, AUDIO_RATE);
        ndspChnSetFormat(ch, NDSP_FORMAT_MONO_PCM16);
        setVolume(ch, ch == CH_MUSIC || ch == CH_THEME ? 0.0f : 1.0f);
    }
    g_sfxPcm = (s16*)linearAlloc(SFX_TOTAL_SAMPLES * 2);
    if (g_sfxPcm) {
        if (FILE* f = fopen("romfs:/audio/sfx.bin", "rb")) { fread(g_sfxPcm, 2, SFX_TOTAL_SAMPLES, f); fclose(f); }
        DSP_FlushDataCache(g_sfxPcm, SFX_TOTAL_SAMPLES * 2);
    }
    for (auto& d : g_streamData) d = (s16*)linearAlloc(STREAM_FRAMES * 2 * sizeof(s16));
    for (auto& d : g_musicData) d = (s16*)linearAlloc(STREAM_FRAMES * 2 * sizeof(s16));
    LightEvent_Init(&g_wake, RESET_ONESHOT);
    LightLock_Init(&g_reqLock);
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    g_thread = threadCreate(streamThread, nullptr, 96 * 1024, prio + 1 > 0x3f ? 0x3f : prio + 1, -2, false);
    g_on = g_sfxPcm != nullptr;
    g_track = backend::setting("music", MUSIC_SHUFFLE);
    if (g_track < MUSIC_OFF || g_track > MUSIC_SHUFFLE) g_track = MUSIC_SHUFFLE;
    g_now = g_track == MUSIC_SHUFFLE ? draw(-1) : g_track;
}

void fini() {
    if (!g_thread && !g_on) return;
    g_quit = true;
    if (g_thread) { LightEvent_Signal(&g_wake); threadJoin(g_thread, U64_MAX); threadFree(g_thread); g_thread = nullptr; }
    for (int ch = 0; ch <= CH_THEME; ch++) ndspChnWaveBufClear(ch);
    ndspExit();
    g_on = false;
}

void update(float dt) {
    if (!g_on) return;
    // Shuffle: near the end of its last pass the track fades out, and another is drawn once it is silent
    if (g_track == MUSIC_SHUFFLE && g_musicOpen == g_now && g_musicLength) {
        u32 end = g_musicLength * SHUFFLE_LOOPS, lead = (u32)(SHUFFLE_FADE * g_musicRate);
        if (!g_leaving && g_musicFed + lead >= end) g_leaving = true;
        if (g_leaving && g_musicGain < 0.02f) { g_now = draw(g_now); g_leaving = false; g_musicGain = 0; }
    }
    // the app's music fades in slowly and makes way quickly
    float target = g_musicWanted && g_now >= 0 && !g_leaving ? 1.0f : 0.0f;
    float tau = g_leaving ? 0.8f : target > g_musicGain ? 0.4f : 0.1f;
    g_musicGain += (target - g_musicGain) * (1 - expf(-dt / tau));
    // while it is silent nothing is decoded for it (it resumes where it stopped); a track on its way out plays on
    bool feed = target > 0 || g_musicGain > 0.003f;
    if (g_musicReq != g_now || g_musicFeed != feed) { g_musicReq = g_now; g_musicFeed = feed; LightEvent_Signal(&g_wake); }
    if (g_musicPlaying && g_musicOpen == g_now) setVolume(CH_MUSIC, g_musicGain);

    // a theme's music starts once the cursor has rested on it for a moment
    if (g_theme == TH_WAITING && (g_wantTimer -= dt) < 0) {
        Item* it = g_want; int gen = g_wantGen;
        g_themeGain = 0;
        if (!it->path.empty()) { request(it->path, nullptr); g_theme = TH_PLAYING; }
        else if (it->plaza) {
            g_theme = TH_FETCHING;
            backend::fetchBgmPreview(it, [gen](const Bytes& ogg) {
                if (gen != g_wantGen) return;
                if (!ogg) { g_theme = TH_FAILED; g_musicWanted = true; return; }
                request("", ogg); g_theme = TH_PLAYING;
            });
        } else { g_theme = TH_FAILED; g_musicWanted = true; }
    }
    // music that could not be opened: the app's own music comes back rather than silence
    if (g_theme == TH_PLAYING && g_failGen == g_reqGen) { g_theme = TH_FAILED; g_musicWanted = true; }
    if (g_theme == TH_PLAYING && g_streamPlaying) {
        g_themeGain += dt / 0.9f * THEME_GAIN;
        if (g_themeGain > THEME_GAIN) g_themeGain = THEME_GAIN;
        setVolume(CH_THEME, g_themeGain);
    }
}

bool available() { return g_on; }

void move() { play(SFX_MOVE); }
void page() { play(SFX_PAGE); }
void tab() { play(SFX_TAB); }
void ok() { play(SFX_OK); }
void back() { play(SFX_BACK); }
void tick(int n) { play(SFX_TICK1 + (n < 1 ? 0 : n > 10 ? 9 : n - 1)); }
void untick(int n) { play(SFX_UNTICK1 + (n < 1 ? 0 : n > 10 ? 9 : n - 1)); }
void err() { play(SFX_ERR); }
void done() { play(SFX_DONE); }
void open() { play(SFX_OPEN); }
void close() { play(SFX_CLOSE); }
void prog(float p) { play(SFX_PROG0 + (int)((p < 0 ? 0 : p > 1 ? 1 : p) * 11 + 0.5f)); }
void key() { play(SFX_KEY); }
void del() { play(SFX_DEL); }

const char* trackName(int i) {
    static const char* n[TRACKS] = {"Tide Pool", "Shop Window", "After Hours"};
    return i == MUSIC_SHUFFLE ? "Shuffle" : i < 0 || i >= TRACKS ? "Off" : n[i];
}
int track() { return g_track; }
int nextTrackSetting(int i) { return i == MUSIC_SHUFFLE ? 0 : i == MUSIC_OFF ? MUSIC_SHUFFLE : i + 1 >= TRACKS ? MUSIC_OFF : i + 1; }

void setTrack(int i) {
    g_track = i;
    backend::setSetting("music", i);
    g_now = i == MUSIC_SHUFFLE ? draw(g_now) : i;
    g_leaving = false;
    if (g_on) g_musicGain = 0;           // the new track fades in from silence (update() tells the stream thread)
}

void wantTheme(Item* it, bool quiet) {
    if (it == g_want && (g_theme != TH_IDLE || !it)) {
        // nothing new to start; only whether the app's own music may play can have changed
        if (!it) g_musicWanted = !quiet;
        return;
    }
    g_wantGen++;
    stopTheme();
    g_want = it;
    g_musicWanted = it == nullptr && !quiet;
    if (it) { g_theme = TH_WAITING; g_wantTimer = THEME_DELAY; }
}

}  // namespace sound
