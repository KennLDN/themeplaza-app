#include "qr.h"
#include <cstdlib>
#include <cstring>
#include "formats.h"
#include "log.h"
#include "qrprep.h"
extern "C" {
#include "../third_party/quirc/quirc.h"
}

namespace qr {

namespace {

// The part of the picture that is searched: a square in the middle, where the on-screen frame is.
constexpr int SCAN = 224, SCAN_X = (W - SCAN) / 2, SCAN_Y = (H - SCAN) / 2;

Thread g_thread = nullptr;
volatile bool g_quit = false, g_running = false, g_failed = false;
LightLock g_lock;
u8* g_tiled = nullptr;            // newest picture in texture layout, guarded by g_lock
u32 g_serial = 0;
std::string g_text; bool g_haveText = false;
aptHookCookie g_hook; bool g_hooked = false;

void onApt(APT_HookType type, void*) {
    // the camera must not keep running while the app is suspended or the lid is shut
    if (type != APTHOOK_ONSUSPEND && type != APTHOOK_ONSLEEP && type != APTHOOK_ONEXIT) return;
    g_quit = true;
    // give the camera thread a moment to stop capturing before the console goes to sleep
    for (int i = 0; i < 60 && g_running; i++) svcSleepThread(10 * 1000 * 1000LL);
}

void thread(void*) {
    const u32 bytes = W * H * 2;
    u16* frame = (u16*)malloc(bytes);
    u8* tiled = (u8*)malloc(TEX_W * TEX_H * 2);
    u16* work = (u16*)malloc(SCAN * SCAN * sizeof(u16));     // for the sharpening step
    u8* grey = (u8*)malloc(SCAN * SCAN);                     // the scanned square before it is enlarged
    u32 frames = 0;
    struct quirc* q = quirc_new();
    static struct quirc_code code; static struct quirc_data data;
    bool ok = frame && tiled && work && grey && q && quirc_resize(q, SCAN, SCAN) == 0 && R_SUCCEEDED(camInit());
    if (ok) {
        memset(tiled, 0, TEX_W * TEX_H * 2);
        u32 unit = 0;
        CAMU_SetSize(SELECT_OUT1, SIZE_CTR_TOP_LCD, CONTEXT_A);
        CAMU_SetOutputFormat(SELECT_OUT1, OUTPUT_RGB_565, CONTEXT_A);
        CAMU_SetFrameRate(SELECT_OUT1, FRAME_RATE_15);
        CAMU_SetNoiseFilter(SELECT_OUT1, true);
        CAMU_SetAutoExposure(SELECT_OUT1, true);
        CAMU_SetAutoWhiteBalance(SELECT_OUT1, true);
        CAMU_SetTrimming(PORT_CAM1, false);
        CAMU_GetMaxBytes(&unit, W, H);
        CAMU_SetTransferBytes(PORT_CAM1, unit, W, H);
        CAMU_Activate(SELECT_OUT1);
        Handle ev[2] = {0, 0};    // [0] buffer error, [1] frame received
        if (R_FAILED(CAMU_GetBufferErrorInterruptEvent(&ev[0], PORT_CAM1)) || !ev[0]) { LOG("qr: no camera event"); g_failed = true; g_quit = true; }
        CAMU_ClearBuffer(PORT_CAM1);
        CAMU_StartCapture(PORT_CAM1);
        while (!g_quit) {
            if (!ev[1] && R_FAILED(CAMU_SetReceiving(&ev[1], frame, PORT_CAM1, bytes, (s16)unit))) { svcSleepThread(50 * 1000 * 1000LL); continue; }
            s32 idx = -1;
            Result rc = svcWaitSynchronizationN(&idx, ev, 2, false, 300 * 1000 * 1000LL);
            if (R_DESCRIPTION(rc) == RD_TIMEOUT) continue;
            if (R_FAILED(rc)) { svcSleepThread(50 * 1000 * 1000LL); continue; }   // never spin: the other workers share this priority
            if (idx == 0) {       // frames were not collected fast enough: start again
                svcCloseHandle(ev[1]); ev[1] = 0;
                CAMU_ClearBuffer(PORT_CAM1);
                CAMU_StartCapture(PORT_CAM1);
                continue;
            }
            svcCloseHandle(ev[1]); ev[1] = 0;

            // for the screen
            u16* dst = (u16*)tiled;
            for (int y = 0; y < H; y++) {
                const u16* row = frame + y * W;
                for (int x = 0; x < W; x++) dst[fmt::tiledIndex(x, y, TEX_W)] = row[x];
            }
            LightLock_Lock(&g_lock);
            memcpy(g_tiled, tiled, TEX_W * TEX_H * 2);
            g_serial++;
            LightLock_Unlock(&g_lock);

            // look for a code: green is 6 of the 16 bits and close enough to brightness
            // Every other frame the middle of the square is enlarged first, for codes that are small on the
            // picture (see qrprep.h); the frames in between are read as they are, for codes that fill the frame.
            int w, h;
            uint8_t* img = quirc_begin(q, &w, &h);
            const bool closer = (frames++ & 1) != 0;
            for (int y = 0; y < SCAN; y++) {
                const u16* row = frame + (SCAN_Y + y) * W + SCAN_X;
                uint8_t* out = (closer ? grey : img) + y * SCAN;
                for (int x = 0; x < SCAN; x++) out[x] = (uint8_t)(((row[x] >> 5) & 0x3f) << 2);
            }
            if (closer) qrprep::zoomCentre(grey, img, SCAN);
            qrprep::sharpen(img, work, SCAN);      // a code held close is out of focus (see qrprep.h)
            quirc_end(q);
            int n = quirc_count(q);
            for (int i = 0; i < n; i++) {
                quirc_extract(q, i, &code);
                quirc_decode_error_t err = quirc_decode(&code, &data);
                if (err == QUIRC_ERROR_DATA_ECC) { quirc_flip(&code); err = quirc_decode(&code, &data); }
                if (err) continue;
                LightLock_Lock(&g_lock);
                g_text.assign((const char*)data.payload, data.payload_len);
                g_haveText = true;
                LightLock_Unlock(&g_lock);
                break;
            }
        }
        CAMU_StopCapture(PORT_CAM1);
        if (ev[1]) svcCloseHandle(ev[1]);
        if (ev[0]) svcCloseHandle(ev[0]);
        CAMU_Activate(SELECT_NONE);
        camExit();
    } else { g_failed = true; LOG("qr: camera could not be started"); }
    if (q) quirc_destroy(q);
    free(frame); free(tiled); free(work); free(grey);
    g_running = false;
}

}  // namespace

bool start() {
    if (g_thread) stop(true);
    static bool once = false;
    if (!once) { LightLock_Init(&g_lock); once = true; }
    if (!g_tiled) g_tiled = (u8*)malloc(TEX_W * TEX_H * 2);
    if (!g_tiled) return false;
    memset(g_tiled, 0, TEX_W * TEX_H * 2);
    g_quit = false; g_running = true; g_failed = false; g_haveText = false; g_serial = 0;
    if (!g_hooked) { aptHook(&g_hook, onApt, nullptr); g_hooked = true; }
    s32 prio = 0x30;
    svcGetThreadPriority(&prio, CUR_THREAD_HANDLE);
    g_thread = threadCreate(thread, nullptr, 64 * 1024, prio + 2 > 0x3f ? 0x3f : prio + 2, -2, false);
    if (!g_thread) { g_running = false; return false; }
    return true;
}

void stop(bool wait) {
    if (!g_thread) return;
    g_quit = true;
    // The thread notices within one camera frame. Waiting for that here would hold up the screen for
    // several frames, so the caller normally does not wait and update() collects the thread once it has ended.
    if (!wait && g_running) return;
    threadJoin(g_thread, U64_MAX);
    threadFree(g_thread);
    g_thread = nullptr;
    g_running = false;
}

void update() { if (g_thread && g_quit && !g_running) stop(true); }

bool active() { return g_thread && g_running && !g_quit; }
bool failed() { return g_failed; }

bool picture(u8* dst, u32& serial) {
    if (!g_tiled) return false;
    LightLock_Lock(&g_lock);
    bool fresh = g_serial != serial;
    if (fresh) { memcpy(dst, g_tiled, TEX_W * TEX_H * 2); serial = g_serial; }
    LightLock_Unlock(&g_lock);
    return fresh;
}

bool take(std::string& text) {
    if (!g_thread) return false;
    LightLock_Lock(&g_lock);
    bool have = g_haveText;
    if (have) { text = g_text; g_haveText = false; }
    LightLock_Unlock(&g_lock);
    return have;
}

}  // namespace qr
