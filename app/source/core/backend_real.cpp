// The real backend: SD card, HOME Menu data and Theme Plaza. Slow work runs on the two workers
// (core/worker.h); Items are only ever touched on the main thread, in callbacks passed to worker::toMain.
#include "backend.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <sys/stat.h>
#include <unistd.h>
#include "extdata.h"
#include "formats.h"
#include "http.h"
#include "install.h"
#include "library.h"
#include "log.h"
#include "pack.h"
#include "plaza.h"
#include "pvcache.h"
#include "rowcache.h"
#include "worker.h"
#include "appdir.h"

namespace backend {

namespace {

const char* const SETTINGS_FILE = APP_DIR "/settings.txt";
const char* const BADGE_STATE_FILE = APP_DIR "/badges.txt";
constexpr int MAX_ICONS = 160, KEEP_ICONS = 110, MAX_PREVIEWS = 24;

std::vector<Item*> g_home[3];
std::vector<Item*> g_installed;
Store g_store;
int g_rev = 0;
bool g_scanning = true, g_reboot = false, g_netUp = false, g_offline = false;
int g_scanKind = 0, g_scanDone = 0, g_scanTotal = 0;
u32 g_frame = 0, g_seq = 0;
std::map<std::string, int> g_settings;
std::vector<std::string> g_badgePaths;        // badge sets this app has put on the HOME Menu

std::vector<Item*> g_iconLoaded, g_previewLoaded;
Item* g_lastAdded = nullptr;

// Theme Plaza state
std::map<long, Item*> g_plazaItems;           // every catalogue item seen this session
LightLock g_plazaLock;                        // guards g_plazaItems for the lookup made on the network worker
int g_storeRowFails = 0;             // rows of the page being read that could not be had
int g_storeGen = 0;                  // goes up with every new search; older requests then stop
Kind g_storeKind = K_THEME; std::string g_storeQuery, g_storeRaw; std::vector<std::string> g_storeTags;
int g_storePage = 0, g_storePages = 0, g_storePending = 0, g_storeOrder = 0;
char g_countText[3][40];

// ---------- small helpers ----------
void bump() { g_rev++; }

Item* homeByPath(const std::string& path) {
    for (auto& v : g_home) for (Item* it : v) if (it->path == path) return it;
    return nullptr;
}
Item* homeByPlazaId(long id) {
    if (id) for (auto& v : g_home) for (Item* it : v) if (it->plazaId == id) return it;
    return nullptr;
}

void finish(Job* job, bool ok, const std::string& err, const std::string& note = "") {
    job->ok = ok; job->error = err; job->note = note; job->progress = 1; job->done = true;
}

void upload(Tex& t, const fmt::Image& img) {
    if (img.ok()) texutil::fromTiled(t, img.data.data(), img.texW, img.texH, img.rgba ? GPU_RGBA8 : GPU_RGB565, img.w, img.h);
}

// ---------- settings ----------
void loadSettings() {
    FILE* f = fopen(SETTINGS_FILE, "r");
    if (!f) return;
    char line[128];
    while (fgets(line, sizeof line, f)) {
        char* eq = strchr(line, '=');
        if (eq) { *eq = 0; g_settings[line] = atoi(eq + 1); }
    }
    fclose(f);
}

void saveSettings() {
    auto copy = std::make_shared<std::map<std::string, int>>(g_settings);
    worker::disk.post([copy] {
        FILE* f = fopen(SETTINGS_FILE, "w");
        if (!f) return;
        for (auto& kv : *copy) fprintf(f, "%s=%d\n", kv.first.c_str(), kv.second);
        fclose(f);
    });
}

void countText(Kind k, int pages) {
    static const char* names[3] = {"themes", "splashes", "badge sets"};
    int n = pages * 24;
    n = n >= 10000 ? (n + 500) / 1000 * 1000 : n >= 1000 ? (n + 50) / 100 * 100 : n;
    if (n >= 1000) snprintf(g_countText[k], sizeof g_countText[k], "%d,%03d %s", n / 1000, n % 1000, names[k]);
    else snprintf(g_countText[k], sizeof g_countText[k], "%d %s", n, names[k]);
}

// ---------- Collection ----------
Item* itemFromRec(const lib::Rec& r) {
    Item* it = homeByPath(r.path);
    bool fresh = !it;
    if (fresh) it = new Item;
    else { it->icon48.free(); it->icon24.free(); it->iconWanted = false; }
    it->kind = (Kind)r.kind; it->name = r.name; it->author = r.author; it->desc = r.desc; it->path = r.path;
    it->bgm = (r.flags & 1) != 0; it->badgeCount = r.badgeCount; it->iconSlot = r.iconSlot;
    it->fileSize = r.fileSize; it->bodySize = r.bodySize; it->bodyCrc = r.bodyCrc; it->auxSize = r.auxSize; it->auxCrc = r.auxCrc;
    it->plazaId = plaza::idFromFileName(lib::baseName(r.path));
    it->onSD = true; it->gone = false;
    it->order = r.mtime ? r.mtime : ++g_seq;
    if (fresh) g_home[r.kind].push_back(it);
    // a Theme Plaza row for the same thing may have been listed before the scan reached this file
    if (it->plazaId > 0) {
        auto f = g_plazaItems.find(it->plazaId);
        if (f != g_plazaItems.end()) { f->second->onSD = true; f->second->path = it->path; f->second->badgeCount = it->badgeCount; }
    }
    return it;
}

std::vector<std::string> g_lastInstall;     // the theme files of the last install in this session

void markApplied(const inst::Applied& a) {
    SLOW("markApplied");
    g_installed.clear();
    for (auto& b : a.bodies) {
        // two files with the same contents cannot be told apart from the HOME Menu's data: prefer the one
        // installed in this session, and one not already counted
        Item* found = nullptr; int best = -1;
        for (Item* it : g_home[K_THEME]) {
            if (!((b.repacked || it->bodySize == b.size) && it->bodyCrc == b.crc)) continue;
            int score = (std::find(g_lastInstall.begin(), g_lastInstall.end(), it->path) != g_lastInstall.end() ? 2 : 0) +
                        (std::find(g_installed.begin(), g_installed.end(), it) == g_installed.end() ? 1 : 0);
            if (score > best) { best = score; found = it; }
        }
        if (found) g_installed.push_back(found);
    }
    for (Item* it : g_home[K_SPLASH])
        it->installed = (a.splashTop || a.splashBottom) && (it->bodySize ? it->bodyCrc : 0) == (a.splashTop ? a.splashTopCrc : 0) &&
                        (it->auxSize ? it->auxCrc : 0) == (a.splashBottom ? a.splashBottomCrc : 0);
    for (Item* it : g_home[K_BADGE])
        it->installed = std::find(g_badgePaths.begin(), g_badgePaths.end(), it->path) != g_badgePaths.end();
    bump();
}

// Set when the app is closing. Queued disk work is still finished then (it may be a write the user asked
// for), but work that only fills the screen is skipped, or leaving during a long first scan would take as
// long as the scan.
volatile bool g_closing = false;

void scanNext(int k, lib::Scan* scan) {
    worker::disk.post([=] {
        if (g_closing) { lib::save(); return; }      // what was read so far is kept; the rest is read next time
        lib::Scan* s = scan ? scan : lib::scanBegin((Kind)k);
        auto recs = std::make_shared<std::vector<lib::Rec>>();
        int done = 0, total = 0;
        bool finished = lib::scanStep(s, 6, *recs, done, total);
        worker::toMain([=] {
            for (auto& r : *recs) itemFromRec(r);
            g_scanKind = k; g_scanDone = done; g_scanTotal = total;
            if (!recs->empty()) bump();
        });
        if (!finished) return scanNext(k, s);
        if (k < 2) return scanNext(k + 1, nullptr);
        lib::dropUnseen();
        lib::save();
        auto a = std::make_shared<inst::Applied>();
        inst::readApplied(*a);
        worker::toMain([a] { g_scanning = false; markApplied(*a); });
    });
}

// A few lines at the top of the log about the console the app finds itself on. They cost a moment at
// start-up on the disk worker and make a report from a real console much easier to read.
void logConsole(u32 linearFreeKB) {
    u8 model = 0, region = 0; bool isNew = false;
    CFGU_GetSystemModel(&model); CFGU_SecureInfoGetRegion(&region); APT_CheckNew3DS(&isNew);
    char ver[64] = "unknown";
    OS_VersionBin nver, cver;
    Result vr = osGetSystemVersionDataString(&nver, &cver, ver, sizeof ver);
    if (R_FAILED(vr)) snprintf(ver, sizeof ver, "unknown (%08lX)", (unsigned long)vr);
    static const char* const models[] = {"3DS", "3DS XL", "New 3DS", "2DS", "New 3DS XL", "New 2DS XL"};
    LOG("console: %s (model %d, %s), region %d, system %s, started %s", model < 6 ? models[model] : "?", model, isNew ? "New" : "original",
        region, ver, envIsHomebrew() ? "as a 3dsx" : "as an installed title");
    LOG("memory: linear %lu KB free, application heap %lu KB", (unsigned long)linearFreeKB, (unsigned long)(envGetHeapSize() / 1024));
    struct stat st;
    LOG("sound firmware (sdmc:/3ds/dspfirm.cdc): %s", stat("sdmc:/3ds/dspfirm.cdc", &st) == 0 ? "present" : "MISSING, the app is silent without it");
    LOG("network: sockets %s, Wi-Fi %s", g_netUp ? "up" : "could not be started", http::wifi() ? "connected" : "not connected");
    auto files = [](const char* what, u32 id, std::initializer_list<const char*> names) {
        ext::Archive a;
        Result rc = id ? a.open(id) : -1;
        if (R_FAILED(rc)) { LOG("%s (extdata %lX): not there (%08lX)", what, (unsigned long)id, (unsigned long)rc); return; }
        std::string line;
        for (const char* n : names) {
            u64 size = 0; Result fr = a.fileSize(n, size);
            char part[64];
            if (R_SUCCEEDED(fr)) snprintf(part, sizeof part, " %s %lu", n + 1, (unsigned long)size); else snprintf(part, sizeof part, " %s none", n + 1);
            line += part;
        }
        LOG("%s (extdata %lX):%s", what, (unsigned long)id, line.c_str());
    };
    files("theme data", ext::themeId(), {"/ThemeManage.bin", "/BodyCache.bin", "/BgmCache.bin", "/BodyCache_rd.bin"});
    files("HOME Menu data", ext::homeMenuId(), {"/SaveData.dat"});
    files("badge data", ext::BADGE_ID, {"/BadgeData.dat", "/BadgeMngFile.dat"});
    std::string splash = "no /luma/config.ini";
    if (FILE* f = fopen("sdmc:/luma/config.ini", "r")) {
        splash = "config.ini has no splash_position line";
        char line[200];
        while (fgets(line, sizeof line, f)) if (strstr(line, "splash_position")) { size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0; splash = line; break; }
        fclose(f);
    }
    LOG("Luma: %s; splash.bin %s", splash.c_str(), stat("sdmc:/luma/splash.bin", &st) == 0 ? "present" : "none");
}

void startScan() {
    u32 linearFreeKB = linearSpaceFree() / 1024;       // read here: the linear heap is the main thread's
    worker::disk.post([linearFreeKB] {
        // A splash install swaps Luma's settings file by renaming; if the power went between the two renames,
        // the file is still there under its temporary name.
        struct stat st;
        if (stat("sdmc:/luma/config.ini", &st) != 0 && stat("sdmc:/luma/config.ini.old", &st) == 0) {
            bool back = rename("sdmc:/luma/config.ini.old", "sdmc:/luma/config.ini") == 0;
            LOG("Luma: config.ini was missing, its copy config.ini.old %s", back ? "was put back" : "could not be put back");
        }
        logConsole(linearFreeKB);
    });
    worker::disk.post([] {
        // which badge sets this app installed
        auto paths = std::make_shared<std::vector<std::string>>();
        if (FILE* f = fopen(BADGE_STATE_FILE, "r")) {
            char line[512];
            while (fgets(line, sizeof line, f)) { size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0; if (n) paths->push_back(line); }
            fclose(f);
        }
        // the badge data may have been deleted since (Data Management, a new SD card): then nothing is installed
        { ext::Archive a; if (R_FAILED(a.open(ext::BADGE_ID))) paths->clear(); }
        worker::toMain([paths] { g_badgePaths = *paths; });
    });
    scanNext(0, nullptr);
}

// ---------- icons ----------
void trimIcons() {
    if ((int)g_iconLoaded.size() <= MAX_ICONS) return;
    std::sort(g_iconLoaded.begin(), g_iconLoaded.end(), [](Item* a, Item* b) { return a->iconUsed > b->iconUsed; });
    while ((int)g_iconLoaded.size() > KEEP_ICONS) {
        Item* it = g_iconLoaded.back();
        if (g_frame - it->iconUsed < 90) break;
        it->icon48.free(); it->icon24.free();
        g_iconLoaded.pop_back();
    }
}

struct IconPair { fmt::Image big, small; bool ok = false; };

void iconDone(Item* it, const std::shared_ptr<IconPair>& p) {
    SLOW("icon upload");
    it->iconWanted = false;
    if (g_closing) return;
    // an icon that cannot be read (a damaged cache file) is not asked for again on every frame
    if (!p->ok && !it->plaza) it->iconSlot = -1;
    if (!p->ok || it->gone) return;
    upload(it->icon48, p->big); upload(it->icon24, p->small);
    if (std::find(g_iconLoaded.begin(), g_iconLoaded.end(), it) == g_iconLoaded.end()) g_iconLoaded.push_back(it);
    trimIcons();
}

// ---------- previews ----------
struct Look {
    fmt::Image top, bottom;            // full size
    fmt::Image halfTop, halfBottom;
    bool keepFull = false;             // the caller wants the full-size pictures too
    std::vector<fmt::Image> badges;
    u32 colBar = 0, colTile = 0, colGlyph = 0;
    bool colours = false, ok = false;
    int badgeCount = -1;
};

Item* g_fullItem = nullptr;        // the one item whose full-size wallpapers are loaded

// Runs on the worker once the pictures are decoded: make the half-size copies, drop what is not needed.
void finishLook(Look& l) {
    fmt::halve(l.top, l.halfTop); fmt::halve(l.bottom, l.halfBottom);
    if (!l.keepFull) { l.top = fmt::Image(); l.bottom = fmt::Image(); }
}

void freePreview(Item* it) {
    it->wallTop.free(); it->wallBot.free();
    if (it == g_fullItem) { it->fullTop.free(); it->fullBot.free(); g_fullItem = nullptr; }
    for (auto& t : it->badgeArt) t.free();
    it->badgeArt.clear();
    it->previewReady = false; it->previewWanted = false;
}

void previewDone(Item* it, const std::shared_ptr<Look>& look) {
    SLOW("preview upload");
    it->previewWanted = false; it->previewAskedAgain = false;
    if (it->gone || g_closing) return;
    // a catalogue item whose pictures did not arrive (a request that failed) is tried again a few times
    // before its card is left plain; the UI asks again while the cursor rests on it
    bool nothing = !look->halfTop.ok() && !look->halfBottom.ok() && look->badges.empty();
    if (nothing && it->plaza && it->path.empty() && ++it->previewTries < 3) return;
    if (!nothing) it->previewTries = 0;
    upload(it->wallTop, look->halfTop); upload(it->wallBot, look->halfBottom);
    if (look->keepFull && it == g_fullItem) { upload(it->fullTop, look->top); upload(it->fullBot, look->bottom); }
    bool again = it == g_fullItem && !look->keepFull;   // became the full-screen item while this load was running
    for (auto& t : it->badgeArt) t.free();
    it->badgeArt.clear();
    it->badgeArt.resize(look->badges.size());
    for (size_t i = 0; i < look->badges.size(); i++) upload(it->badgeArt[i], look->badges[i]);
    if (look->colours) { it->colBar = look->colBar; it->colTile = look->colTile; it->colGlyph = look->colGlyph; }
    if (look->badgeCount > 0 && it->plaza && it->path.empty()) it->badgeCount = look->badgeCount;   // an estimate from the site's preview picture; a set on the SD card has its real count
    it->previewReady = true;   // also when loading failed: the card then shows a plain colour instead of spinning for ever
    g_previewLoaded.erase(std::remove(g_previewLoaded.begin(), g_previewLoaded.end(), it), g_previewLoaded.end());
    g_previewLoaded.push_back(it);
    while ((int)g_previewLoaded.size() > MAX_PREVIEWS) {
        auto victim = g_previewLoaded.end();
        for (auto i = g_previewLoaded.begin(); i != g_previewLoaded.end(); ++i)
            if (*i != it && (victim == g_previewLoaded.end() || (*i)->previewUsed < (*victim)->previewUsed)) victim = i;
        if (victim == g_previewLoaded.end()) break;
        freePreview(*victim); g_previewLoaded.erase(victim);
    }
    if (again) { g_fullItem = nullptr; wantFullPreview(it); }
}

// From the files on the SD card (disk worker).
void lookFromPack(Kind kind, const std::string& path, Look& out) {
    Pack pk; std::vector<u8> buf;
    if (!pk.open(path)) return;
    if (kind == K_THEME) {
        const PackEntry* e = pk.find("body_LZ.bin");
        fmt::ThemeLook t;
        if (!e || !pk.read(*e, buf, inst::BODY_MAX) || !fmt::decodeTheme(buf.data(), buf.size(), t)) return;
        out.top = std::move(t.top); out.bottom = std::move(t.bottom);
        out.colBar = t.colBar; out.colTile = t.colTile; out.colGlyph = t.colGlyph; out.colours = true;
    } else if (kind == K_SPLASH) {
        if (const PackEntry* e = pk.find("splash.bin")) if (pk.read(*e, buf, 300000)) fmt::decodeSplash(buf.data(), buf.size(), 400, out.top);
        if (const PackEntry* e = pk.find("splashbottom.bin")) if (pk.read(*e, buf, 300000)) fmt::decodeSplash(buf.data(), buf.size(), 320, out.bottom);
        fmt::ThemeLook t; fmt::coloursFromImage(out.top.ok() ? out.top : out.bottom, t);
        out.colBar = t.colBar; out.colTile = t.colTile; out.colGlyph = t.colGlyph; out.colours = true;
        // a splash for one screen only leaves the other one black
        auto black = [](fmt::Image& img, int w) { img.texW = 512; img.texH = 256; img.w = w; img.h = 240; img.rgba = false; img.data.assign(512 * 256 * 2, 0); };
        if (!out.top.ok()) black(out.top, 400);
        if (!out.bottom.ok()) black(out.bottom, 320);
    } else {
        for (const PackEntry* e : lib::badgeEntries(pk, path)) {
            if (out.badges.size() >= 6) break;
            fmt::Png png;
            if (!pk.read(*e, buf, 2 * 1024 * 1024) || !fmt::decodePng(buf.data(), buf.size(), png, 768 * 768) || png.w < 64 || png.h < 64) continue;
            out.badges.emplace_back();
            fmt::pngToRGBA(png, 0, 0, 64, 64, out.badges.back());
        }
    }
    out.ok = true;
}

// From Theme Plaza's preview images (network worker).
void lookFromPlaza(Kind kind, long id, Look& out) {
    fmt::Png png;
    auto fetch = [&](const char* part) {
        http::Response r = http::get(plaza::urlPreview(id, part), 2 * 1024 * 1024);
        return r.ok() && fmt::decodePng(r.body.data(), r.body.size(), png, 1024 * 1024);
    };
    if (kind == K_THEME) {
        // "top" and "bottom" are the bare wallpapers; when a theme has none the site sends the full screenshot instead
        if (fetch("top")) fmt::pngTo565(png, png.w > 412 ? (png.w - 412) / 2 : 0, 0, png.w < 412 ? png.w : 412, 240, out.top);
        if (fetch("bottom")) {
            if (png.h > 240) fmt::pngTo565(png, (png.w - 320) / 2, png.h - 240, 320, 240, out.bottom);
            else fmt::pngTo565(png, 0, 0, 320, 240, out.bottom);
        }
    } else if (kind == K_SPLASH) {
        if (fetch("")) {
            fmt::pngTo565(png, (png.w - 400) / 2, 0, 400, 240, out.top);
            fmt::pngTo565(png, (png.w - 320) / 2, 240, 320, 240, out.bottom);
        }
    } else {
        // the badge preview is a sheet of 64x64 cells, 8 across
        if (fetch("")) {
            int count = 0;
            for (int y = 0; y + 64 <= png.h; y += 64)
                for (int x = 0; x + 64 <= png.w; x += 64) {
                    if (fmt::pngEmpty(png, x, y, 64, 64)) continue;
                    if (out.badges.size() < 6) { out.badges.emplace_back(); fmt::pngToRGBA(png, x, y, 64, 64, out.badges.back()); }
                    count++;
                }
            out.badgeCount = count;
        }
    }
    if (out.top.ok() || out.bottom.ok()) {
        fmt::ThemeLook t; fmt::coloursFromImage(out.top.ok() ? out.top : out.bottom, t);
        out.colBar = t.colBar; out.colTile = t.colTile; out.colGlyph = t.colGlyph; out.colours = true;
    }
    out.ok = out.top.ok() || out.bottom.ok() || !out.badges.empty();
}

// ---------- Theme Plaza list ----------
// A later page that cannot be had: the rows already shown stay, the list just ends there (the UI says so once).
bool keepListAfterFailure() {
    if (!g_store.ready || g_store.list.empty() || g_storePage < 1) return false;
    g_store.loading = false; g_store.more = false; g_store.moreFailed = true;
    return true;
}

void netFailed(int gen) {
    if (gen != g_storeGen) return;
    if (keepListAfterFailure()) return;
    g_offline = true; g_store.loading = false; g_store.failed = true; g_store.ready = false;
}

void fetchIcons(int gen, std::shared_ptr<std::vector<Item*>> need);

// Rows keep the order the site gave them even though they arrive as their requests finish.
void addRow(Item* it, int rank) {
    it->storeRank = rank;
    if (std::find(g_store.list.begin(), g_store.list.end(), it) != g_store.list.end()) return;
    auto pos = g_store.list.begin();
    while (pos != g_store.list.end() && (*pos)->storeRank <= rank) ++pos;
    g_store.list.insert(pos, it);
}

void rowArrived(int gen, long id, const std::shared_ptr<fmt::Smdh>& s, int rank) {
    if (gen != g_storeGen) return;
    Item* it = nullptr;
    auto f = g_plazaItems.find(id);
    if (f != g_plazaItems.end()) it = f->second;
    else if (s) {
        it = new Item;
        it->kind = g_storeKind; it->plaza = true; it->plazaId = id;
        it->name = s->name.empty() ? "Untitled" : s->name; it->author = s->author; it->desc = s->desc;
        if (s->hasIcon) it->icon565.assign(s->icon, s->icon + fmt::SMDH_ICON_SIZE);
        LightLock_Lock(&g_plazaLock);
        g_plazaItems[id] = it;
        LightLock_Unlock(&g_plazaLock);
    }
    if (it) {
        if (Item* h = homeByPlazaId(id)) { it->onSD = true; it->path = h->path; it->badgeCount = h->badgeCount; } else { it->onSD = false; it->path.clear(); }
        addRow(it, rank);
        // known from an earlier list whose icon request was dropped by a new search
        if (!s && it->icon565.empty()) fetchIcons(gen, std::make_shared<std::vector<Item*>>(1, it));
    } else g_storeRowFails++;
    bool last = --g_storePending <= 0;
    // show the list once the first screenful is there; the rest fills in underneath
    if (!g_store.ready && (g_store.list.size() >= 5 || last)) g_store.ready = true;
    if (last) {
        g_store.loading = false; g_store.more = g_storePage < g_storePages;
        // every row failed: that is "the site is not answering", not "there is nothing here"
        if (g_store.list.empty() && g_storeRowFails > 0) { g_store.failed = true; g_store.more = false; g_store.error = "The list arrived, but its entries could not be read."; }
    }
}

// Rows that came complete from the site's sorted listing; only their icons are fetched afterwards.
void rowsArrived(int gen, int page, int pages, const std::shared_ptr<plaza::RowList>& rl) {
    if (gen != g_storeGen) return;
    g_offline = false;
    g_storePage = page; if (pages) g_storePages = pages;
    auto need = std::make_shared<std::vector<Item*>>();
    int rank = page * 1000;
    for (auto& r : rl->rows) {
        Item* it = nullptr;
        auto f = g_plazaItems.find(r.id);
        if (f != g_plazaItems.end()) it = f->second;
        else {
            it = new Item;
            it->kind = g_storeKind; it->plaza = true; it->plazaId = r.id;
            LightLock_Lock(&g_plazaLock);
            g_plazaItems[r.id] = it;
            LightLock_Unlock(&g_plazaLock);
        }
        it->name = r.title.empty() ? "Untitled" : r.title; it->author = r.author; if (!r.desc.empty()) it->desc = r.desc;
        it->downloads = r.downloads; it->likes = r.likes; it->bgm = r.bgm;
        it->statsReady = true;
        if (Item* h = homeByPlazaId(r.id)) { it->onSD = true; it->path = h->path; it->badgeCount = h->badgeCount; } else { it->onSD = false; it->path.clear(); }
        addRow(it, rank++);
        if (it->icon565.empty()) need->push_back(it);
    }
    g_store.ready = true; g_store.loading = false; g_store.more = g_storePage < g_storePages;
    if (page == 1 && pages && rl->rows.size() == 24 && g_storeQuery.empty() && g_storeTags.empty()) countText(g_storeKind, pages);
    fetchIcons(gen, need);
}

void fetchIcons(int gen, std::shared_ptr<std::vector<Item*>> need) {
    if (need->empty()) return;
    if (plaza::v2()) {
        // one request brings every icon of the page
        worker::net.post([=] {
            if (gen != g_storeGen) return;
            std::vector<long> ids;
            for (Item* it : *need) ids.push_back(it->plazaId);
            auto all = std::make_shared<std::vector<u8>>();
            if (!plaza::iconsV2(ids, *all)) return;
            worker::toMain([=] {
                for (size_t i = 0; i < need->size(); i++) {
                    const u8* p = all->data() + i * fmt::SMDH_ICON_SIZE;
                    bool blank = true;
                    for (size_t k = 0; k < fmt::SMDH_ICON_SIZE && blank; k++) blank = p[k] == 0;
                    if (!blank && (*need)[i]->icon565.empty()) (*need)[i]->icon565.assign(p, p + fmt::SMDH_ICON_SIZE);
                }
            });
        });
        return;
    }
    // What the rows say, copied here on the main thread: the network thread keeps each row with its icon
    // (core/rowcache.h) and must not read the items themselves.
    struct Meta { long id; std::string name, author, desc; };
    auto meta = std::make_shared<std::vector<Meta>>();
    for (Item* it : *need) meta->push_back({it->plazaId, it->name, it->author, it->desc});
    // icons: a few at a time side by side, in tasks small enough for a preview or download to slip in between
    for (size_t at = 0; at < need->size(); at += 6) {
        worker::net.post([=] {
            if (gen != g_storeGen) return;
            std::vector<http::BatchItem> items; std::vector<size_t> index;
            for (size_t i = at; i < need->size() && i < at + 6; i++) {
                // an icon kept from an earlier visit needs no request
                auto kept = std::make_shared<fmt::Smdh>();
                if (rowcache::get((*meta)[i].id, *kept)) {
                    Item* it = (*need)[i];
                    worker::toMain([=] { if (it->icon565.empty()) it->icon565.assign(kept->icon, kept->icon + fmt::SMDH_ICON_SIZE); });
                    continue;
                }
                http::BatchItem b; b.url = plaza::urlSmdh((*meta)[i].id); b.range = "9408-14015"; items.push_back(std::move(b)); index.push_back(i);
            }
            http::getBatch(items, 3, [&](size_t k) {
                http::Response& r = items[k].response;
                auto icon = std::make_shared<std::vector<u8>>();
                if (r.status == 206 && r.body.size() == fmt::SMDH_ICON_SIZE) *icon = std::move(r.body);
                else if (r.status == 200 && r.body.size() >= fmt::SMDH_SIZE) icon->assign(r.body.begin() + fmt::SMDH_ICON_OFFSET, r.body.begin() + fmt::SMDH_ICON_OFFSET + fmt::SMDH_ICON_SIZE);
                else return;
                size_t i = index[k];
                const Meta& m = (*meta)[i];
                auto row = std::make_unique<fmt::Smdh>();
                row->name = m.name; row->author = m.author; row->desc = m.desc; row->hasIcon = true;
                memcpy(row->icon, icon->data(), fmt::SMDH_ICON_SIZE);
                rowcache::put(m.id, *row);
                Item* it = (*need)[i];
                worker::toMain([=] { if (it->icon565.empty()) it->icon565 = *icon; });
            });
        });
    }
}

void fetchPage(int gen, Kind kind, int page, std::string query, std::vector<std::string> tags, int order) {
    std::string raw = g_storeRaw;
    worker::net.post([=] {
        if (gen != g_storeGen) return;
        // with Wi-Fi off, say so at once instead of waiting for a connection attempt to time out
        if (!g_netUp || !http::wifi()) { worker::toMain([gen] { netFailed(gen); }); return; }
        if (plaza::v2Possible()) {
            auto rl = std::make_shared<plaza::RowList>(plaza::listV2(kind, page, query, tags, order));
            if (rl->ok) { int pages = rl->pages; worker::toMain([=] { rowsArrived(gen, page, pages, rl); }); return; }
            if (!rl->reached) { worker::toMain([gen] { netFailed(gen); }); return; }
            // answered, but not with a list: for a later page the list ends there; for the first, the older ways are tried
            if (page > 1) { worker::toMain([gen] { if (gen == g_storeGen && !keepListAfterFailure()) g_store.loading = false; }); return; }
        }
        if (order != 0) {
            auto rl = std::make_shared<plaza::RowList>(plaza::listSorted(kind, page, query, tags, order));
            bool nothing = rl->ok && rl->rows.empty() && page == 1 && !query.empty();
            if (rl->ok && !nothing) { int pages = rl->pages; worker::toMain([=] { rowsArrived(gen, page, pages, rl); }); return; }
            if (page > 1) { worker::toMain([gen] { if (gen == g_storeGen && !keepListAfterFailure()) g_store.loading = false; }); return; }
            // No sorted list (https not working, or the site changed): show the plain newest-first list instead.
            // A sorted search that found nothing also goes to the plain search, which tries the text as a
            // creator and as a tag too; that one is not an error, so nothing is said.
            worker::toMain([gen, nothing] { if (gen == g_storeGen) { if (!nothing) g_store.sortFailed = true; g_storeOrder = 0; } });
        }
        // page 1 may have turned the text into a creator or tag search; later pages must ask the same question
        plaza::List l = page == 1 ? plaza::list(kind, page, query, tags) : plaza::listRaw(kind, page, raw);
        if (!l.reached) { worker::toMain([gen] { netFailed(gen); }); return; }
        auto ids = std::make_shared<std::vector<long>>(l.ids);
        int pages = l.pages; bool ok = l.ok; std::string err = l.error, used = l.usedQuery;
        worker::toMain([=] {
            if (gen != g_storeGen) return;
            g_offline = false;
            if (page == 1) g_storeRaw = used;
            if (!ok) {
                if (keepListAfterFailure()) return;
                g_store.loading = false; g_store.failed = true; g_store.error = err; g_store.ready = true; g_store.more = false; return;
            }
            g_storePage = page; if (pages) g_storePages = pages;
            if (page == 1 && pages && query.empty() && tags.empty()) { countText(kind, pages); setSetting(kind == K_THEME ? "pages0" : kind == K_SPLASH ? "pages1" : "pages2", pages); }
            if (ids->empty()) { g_store.loading = false; g_store.ready = true; g_store.more = false; return; }
            g_storePending = (int)ids->size(); g_storeRowFails = 0;
            // The list only has ids; each row's name, creator and icon come from its SMDH. They are fetched a
            // few side by side, in tasks of six so that a preview or a download asked for meanwhile can go in between.
            for (size_t at = 0; at < ids->size(); at += 6) {
                worker::net.post([=] {
                    if (gen != g_storeGen) return;
                    std::vector<http::BatchItem> items; std::vector<size_t> index;
                    for (size_t i = at; i < ids->size() && i < at + 6; i++) {
                        long id = (*ids)[i]; int rank = page * 1000 + (int)i;
                        LightLock_Lock(&g_plazaLock);
                        bool known = g_plazaItems.count(id) != 0;
                        LightLock_Unlock(&g_plazaLock);
                        if (known) { worker::toMain([=] { rowArrived(gen, id, nullptr, rank); }); continue; }
                        // a row kept from an earlier visit (core/rowcache.h) needs no request
                        auto kept = std::make_shared<fmt::Smdh>();
                        if (rowcache::get(id, *kept)) { worker::toMain([=] { rowArrived(gen, id, kept, rank); }); continue; }
                        http::BatchItem b; b.url = plaza::urlSmdh(id); b.maxBody = 32 * 1024;
                        items.push_back(std::move(b)); index.push_back(i);
                    }
                    http::getBatch(items, 3, [&](size_t k) {
                        size_t i = index[k]; long id = (*ids)[i]; int rank = page * 1000 + (int)i;
                        http::Response& r = items[k].response;
                        auto s = std::make_shared<fmt::Smdh>();
                        if (!r.ok() || !fmt::parseSmdh(r.body.data(), r.body.size(), *s)) s.reset();
                        else rowcache::put(id, *s);
                        worker::toMain([=] { rowArrived(gen, id, s, rank); });
                    });
                });
            }
        });
    });
}

// Downloads a zip into the right folder and adds it to the Collection. Network worker, then disk worker.
void runDownload(Job* job, long id, Item* plazaItem, std::string title, std::string author, int kindOrMinus1, bool announce) {
    worker::net.post([=] {
        std::string name = title, by = author; int kind = kindOrMinus1;
        if (!g_netUp) { worker::toMain([=] { g_offline = true; finish(job, false, "No internet connection"); }); return; }
        if (name.empty() || kind < 0) {
            // from a QR code only the id is known
            fmt::Smdh s;
            if (plaza::smdh(id, s)) { name = s.name; by = s.author; }
        }
        mkdir(APP_DIR "/cache", 0777);
        const char* tmp = APP_DIR "/cache/download.part";
        FILE* f = fopen(tmp, "wb");
        if (!f) { worker::toMain([=] { finish(job, false, "Could not write to the SD card"); }); return; }
        setvbuf(f, nullptr, _IOFBF, 64 * 1024);
        http::Response r = http::download(plaza::urlDownload(id), f, [job](size_t now, size_t total) { if (total) job->progress = 0.92f * now / total; return true; });
        bool wrote = fclose(f) == 0;
        // For an item it no longer has, the site answers with one of its pages (status 200) rather than an error.
        bool isZip = false;
        if (r.ok() && wrote) if (FILE* z = fopen(tmp, "rb")) { u8 sig[2]; isZip = fread(sig, 1, 2, z) == 2 && sig[0] == 'P' && sig[1] == 'K'; fclose(z); }
        if (r.ok() && wrote && !isZip) {
            LOG("download of item %ld: the answer is not a zip file", id);
            remove(tmp);
            worker::toMain([=] { finish(job, false, "Theme Plaza did not send the file"); });
            return;
        }
        if (!r.ok() || !wrote) {
            remove(tmp);
            bool full = r.writeFailed || (r.ok() && !wrote), reached = r.status != 0 || full;
            worker::toMain([=] { if (!reached) g_offline = true; finish(job, false, full ? "The SD card is full or cannot be written" : !reached ? "The download was interrupted" : "Theme Plaza did not send the file"); });
            return;
        }
        worker::disk.postOp([=] {
            // find out what it is from its contents when the caller did not know
            int k = kind;
            if (k < 0) {
                Pack pk;
                if (pk.open(tmp)) k = pk.find("body_LZ.bin") ? K_THEME : (pk.find("splash.bin") || pk.find("splashbottom.bin")) ? K_SPLASH : K_BADGE;
                else k = K_THEME;
            }
            mkdir(lib::DIRS[k], 0777);
            std::string dst = std::string(lib::DIRS[k]) + "/" + plaza::fileName(name, by, id);
            remove(dst.c_str());
            auto rec = std::make_shared<lib::Rec>();
            bool ok = rename(tmp, dst.c_str()) == 0 && lib::add((Kind)k, dst, *rec);
            if (ok) lib::save(); else { remove(tmp); remove(dst.c_str()); }
            worker::toMain([=] {
                if (!ok) return finish(job, false, "The downloaded file is not something this app can use");
                Item* h = itemFromRec(*rec);
                g_lastAdded = h;
                if (h->author.empty()) h->author = by;
                if (plazaItem) { plazaItem->onSD = true; plazaItem->path = h->path; plazaItem->badgeCount = h->badgeCount; if (h->desc.empty()) h->desc = plazaItem->desc; }
                bump();
                static const char* const where[3] = {"themes", "splashes", "badges"};
                finish(job, true, "", announce ? h->name + " saved to your " + where[k] : "");
            });
        });
    }, true);
}

}  // namespace

// ---------- public ----------
void init() {
    mkdir("sdmc:/3ds", 0777); mkdir(APP_DIR, 0777);
    LightLock_Init(&g_plazaLock);
    loadSettings();
    static const int defPages[3] = {3040, 263, 137};
    for (int k = 0; k < 3; k++) countText((Kind)k, setting(k == 0 ? "pages0" : k == 1 ? "pages1" : "pages2", defPages[k]));
    // a file naming another server (a staging copy, the test server) overrides the real one
    if (FILE* f = fopen(APP_DIR "/plaza_server.txt", "r")) {
        char line[200];
        if (fgets(line, sizeof line, f)) plaza::setBase(line);
        fclose(f);
    }
    worker::init();
    g_netUp = http::init();
    startScan();
}

void fini() {
    g_closing = true;
    g_storeGen++;
    http::cancelAll();
    // A request can sit in a name lookup or a handshake for longer than the exit waits. That thread is then
    // left as it is, and so is everything it uses; the process ends a moment later anyway.
    if (worker::fini()) http::fini();
    else LOG("exit: the network thread was still busy; left running");
}

void update(float) {
    g_frame++;
    worker::pump();
}

std::vector<Item*>& home(Kind k) { return g_home[k]; }
int revision() { return g_rev; }
bool scanning() { return g_scanning; }
bool scanProgress(Kind k, int& done, int& total) {
    if (!g_scanning || g_scanKind != (int)k || g_scanTotal <= 0) return false;
    done = g_scanDone; total = g_scanTotal;
    return true;
}
const std::vector<Item*>& installedThemes() { return g_installed; }

Store& store() { return g_store; }

void storeLoad(Kind k, const std::string& query, const std::vector<std::string>& tags, int order) {
    g_storeGen++;
    g_store.list.clear();
    g_store.ready = false; g_store.loading = true; g_store.failed = false; g_store.more = false; g_store.sortFailed = false; g_store.moreFailed = false; g_store.error.clear();
    g_storeKind = k; g_storeQuery = query; g_storeTags = tags; g_storePage = 0; g_storePages = 0; g_storeOrder = order;
    fetchPage(g_storeGen, k, 1, query, tags, order);
}

void storeInvalidate() {
    g_storeGen++;
    g_store.list.clear();
    g_store.ready = g_store.loading = g_store.failed = g_store.more = g_store.sortFailed = g_store.moreFailed = false;
    g_store.error.clear();
}

void storeMore() {
    if (g_store.loading || !g_store.ready || !g_store.more) return;
    g_store.loading = true;
    fetchPage(g_storeGen, g_storeKind, g_storePage + 1, g_storeQuery, g_storeTags, g_storeOrder);
}

bool online() { return !g_offline; }
void retryOnline() { if (g_netUp && http::wifi()) g_offline = false; }
void setOffline(bool off) { g_offline = off; }
const char* storeCount(Kind k) { return g_countText[k]; }

void wantStats(Item* it) {
    if (!it || !it->plaza || it->statsReady || it->statsWanted) return;
    it->statsWanted = true;
    long id = it->plazaId;
    worker::net.post([=] {
        // figures kept from the last day (core/rowcache.h) need no request
        auto kept = std::make_shared<rowcache::Stats>();
        if (rowcache::getStats(id, *kept)) {
            worker::toMain([=] {
                it->statsWanted = false;
                it->downloads = kept->downloads; it->likes = kept->likes; it->bgm = kept->bgm;
                if (!kept->desc.empty()) it->desc = kept->desc;
                it->statsReady = true;
            });
            return;
        }
        auto d = std::make_shared<plaza::Details>(plaza::details(id));
        if (d->ok) { rowcache::Stats st; st.downloads = d->downloads; st.likes = d->likes; st.bgm = d->bgm; st.desc = d->desc; rowcache::putStats(id, st); }
        worker::toMain([=] {
            it->statsWanted = false;
            if (!d->ok) return;
            it->downloads = d->downloads; it->likes = d->likes; it->bgm = d->bgm; it->tags = d->tags;
            if (!d->desc.empty()) it->desc = d->desc;
            if (it->author.empty()) it->author = d->author;
            it->statsReady = true;
        });
    }, true);
}

void wantIcon(Item* it) {
    if (!it || it->more) return;
    it->iconUsed = g_frame;
    if (it->icon48.ok || it->iconWanted) return;
    if (it->plaza ? it->icon565.empty() : it->iconSlot < 0) return;
    it->iconWanted = true;
    int slot = it->iconSlot;
    auto raw = it->plaza ? std::make_shared<std::vector<u8>>(it->icon565) : nullptr;
    worker::disk.post([=] {
        if (g_closing) return;
        auto p = std::make_shared<IconPair>();
        alignas(4) u8 icon[fmt::SMDH_ICON_SIZE];
        if (raw) memcpy(icon, raw->data(), sizeof icon);
        if (raw || lib::readIcon(slot, icon)) { fmt::makeIcon(icon, 48, p->big); fmt::makeIcon(icon, 24, p->small); p->ok = true; }
        worker::toMain([=] { iconDone(it, p); });
    }, true);
}

volatile int g_preloadGen = 0;
void dropPreloads() { g_preloadGen = g_preloadGen + 1; }

// A queued request that was not worth carrying out (main thread). If the item was asked for in earnest
// meanwhile, that request is made now.
static void giveUpPreview(Item* it) {
    it->previewWanted = false;
    if (it->previewAskedAgain) { it->previewAskedAgain = false; wantPreview(it, true); }
}

void wantPreview(Item* it, bool urgent, bool cacheOnly) {
    if (!it || it->more) return;
    it->previewUsed = g_frame;
    if (it->previewReady) return;
    if (it->previewWanted) {
        // A request is already queued. If that one was weaker (made ahead of time, or for the cache only) it may
        // give up; remember that the item is now really wanted, so it is asked for again when that happens.
        if (urgent && !cacheOnly) it->previewAskedAgain = true;
        return;
    }
    // only a catalogue theme's or splash's pictures are worth asking the cache for separately
    if (cacheOnly && (!it->plaza || !it->path.empty() || it == g_fullItem || it->kind == K_BADGE)) return;
    it->previewWanted = true; it->previewAskedAgain = false;
    Kind kind = it->kind; std::string path = it->path; long id = it->plazaId;
    auto look = std::make_shared<Look>();
    look->keepFull = it == g_fullItem;
    // A preview asked for ahead of time (a neighbour of the selected item) is not worth decoding once the
    // cursor has moved on: on an original 3DS each one takes a noticeable part of a second.
    int gen = g_preloadGen;
    // what the card shows is kept on the SD card once made (core/pvcache.h); the full-size pictures are not
    // (a catalogue item that is on the SD card uses the record of its Collection entry)
    Item* file = it->plaza && !it->path.empty() ? homeByPath(it->path) : it;
    u32 key = !file || file->plaza ? 0 : pvcache::key((int)kind, file->bodySize, file->bodyCrc, file->auxSize, file->auxCrc);
    if (!path.empty()) worker::disk.post([=] {
        if (g_closing) return;
        if (!urgent && gen != g_preloadGen) { worker::toMain([it] { giveUpPreview(it); }); return; }
        pvcache::Colours col;
        if (!look->keepFull && pvcache::get(key, look->halfTop, look->halfBottom, col)) {
            if (col.set) { look->colBar = col.bar; look->colTile = col.tile; look->colGlyph = col.glyph; look->colours = true; }
        } else {
            lookFromPack(kind, path, *look); finishLook(*look);
            col.bar = look->colBar; col.tile = look->colTile; col.glyph = look->colGlyph; col.set = look->colours;
            pvcache::put(key, look->halfTop, look->halfBottom, col);
        }
        worker::toMain([=] { previewDone(it, look); });
    }, urgent);
    else {
        // A catalogue item: its pictures come from Theme Plaza, and are kept in the same cache afterwards, so
        // the lists a user sees at every start (the first pages) show their previews without the network.
        u32 pkey = look->keepFull ? 0 : pvcache::plazaKey((int)kind, id);
        auto fetch = [=] {
            lookFromPlaza(kind, id, *look); finishLook(*look);
            worker::toMain([=] { previewDone(it, look); });
            // only complete results are kept: one picture missing may just be a request that failed
            if (pkey && look->halfTop.ok() && look->halfBottom.ok()) worker::disk.post([=] {
                if (g_closing) return;
                pvcache::Colours col;
                col.bar = look->colBar; col.tile = look->colTile; col.glyph = look->colGlyph; col.set = look->colours;
                pvcache::put(pkey, look->halfTop, look->halfBottom, col);
            });
        };
        if (!pkey) worker::net.post(fetch, urgent);
        else worker::disk.post([=] {
            if (g_closing) return;
            pvcache::Colours col;
            if (pvcache::get(pkey, look->halfTop, look->halfBottom, col)) {
                if (col.set) { look->colBar = col.bar; look->colTile = col.tile; look->colGlyph = col.glyph; look->colours = true; }
                worker::toMain([=] { previewDone(it, look); });
            } else if (cacheOnly) worker::toMain([it] { giveUpPreview(it); });      // not kept: the caller asks again, for the network this time
            else worker::net.post(fetch, urgent);
        }, true);
    }
}

void wantFullPreview(Item* it) {
    if (!it || it->more || it->kind == K_BADGE || it == g_fullItem) return;
    if (g_fullItem) { g_fullItem->fullTop.free(); g_fullItem->fullBot.free(); }
    g_fullItem = it;
    // decode again, this time keeping the full-size pictures; the half-size ones stay on screen meanwhile
    if (it->previewWanted) return;     // a load is under way; it was started before this item became the full one, so ask again when it lands
    bool had = it->previewReady;
    it->previewReady = false;
    wantPreview(it);
    it->previewReady = had;
}

namespace { int g_bgmGen = 0; }

void cancelBgmPreview() { g_bgmGen++; }

void fetchBgmPreview(Item* it, std::function<void(const std::shared_ptr<StreamBuf>& ogg)> done) {
    int mine = ++g_bgmGen;
    long id = it->plazaId;
    worker::net.post([=] {
        if (mine != g_bgmGen || !g_netUp) { worker::toMain([=] { done(nullptr); }); return; }
        auto buf = std::make_shared<StreamBuf>();
        bool announced = false, bad = false;
        // playing can start once the beginning is here; the rest keeps arriving underneath
        http::Response r = http::stream(plaza::urlBgm(id), [&](const u8* p, size_t n) {
            if (mine != g_bgmGen || bad) return false;
            LightLock_Lock(&buf->lock);
            buf->data.insert(buf->data.end(), p, p + n);
            size_t have = buf->data.size();
            bad = have >= 4 && memcmp(buf->data.data(), "OggS", 4) != 0;
            LightLock_Unlock(&buf->lock);
            if (!announced && !bad && have >= 48 * 1024) { announced = true; worker::toMain([=] { done(mine == g_bgmGen ? buf : nullptr); }); }
            return !bad;
        }, [&](size_t, size_t) { return mine == g_bgmGen; });
        buf->finished = true;
        if (!announced) {
            bool ok = r.ok() && !bad && buf->data.size() > 4;
            worker::toMain([=] { done(ok && mine == g_bgmGen ? buf : nullptr); });
        }
    }, true);
}

Job* installThemes(const std::vector<Item*>& items, InstallMode mode) {
    Job* job = new Job;
    auto paths = std::make_shared<std::vector<std::string>>();
    for (Item* it : items) paths->push_back(it->path);
    worker::disk.postOp([=] {
        std::string err;
        u64 t0 = svcGetSystemTick();
        bool touched = false;
        bool ok = inst::themes(*paths, mode, job, err, touched);
        // An install that failed part-way has left the HOME Menu set to "no theme" (never to half-written
        // data); that too only takes effect, and is only safe from being written over, after a restart.
        if (!ok && touched) err += ". The HOME Menu is on its default theme now";
        static const char* const modes[] = {"with its music", "music only", "without music"};
        LOG("theme install: %u file(s), %s: %s%s (%.1f s)", (unsigned)paths->size(), modes[mode], ok ? "done" : "FAILED: ", ok ? "" : err.c_str(), (double)(svcGetSystemTick() - t0) / SYSCLOCK_ARM11);
        auto a = std::make_shared<inst::Applied>();
        if (ok || touched) inst::readApplied(*a);
        worker::toMain([=] {
            if (ok) g_lastInstall = *paths;
            if (ok || touched) { g_reboot = true; markApplied(*a); }
            finish(job, ok, err);
        });
    });
    return job;
}

Job* installSplash(Item* it) {
    Job* job = new Job;
    std::string path = it->path;
    worker::disk.postOp([=] {
        std::string err, note;
        bool ok = inst::splash(path, job, err, note);
        LOG("splash install: %s%s", ok ? "done. " : "FAILED: ", ok ? note.c_str() : err.c_str());
        auto a = std::make_shared<inst::Applied>();
        inst::readApplied(*a);
        worker::toMain([=] { markApplied(*a); finish(job, ok, err, note); });
    });
    return job;
}

void removeSplash() {
    for (Item* it : g_home[K_SPLASH]) it->installed = false;
    bump();
    worker::disk.postOp([] {
        inst::removeSplash();
        // report what is there now: a scan result read before the removal may have marked the splash again
        auto a = std::make_shared<inst::Applied>();
        inst::readApplied(*a);
        worker::toMain([a] { markApplied(*a); });
    });
}

static Job* applyBadges(std::vector<std::string> paths) {
    Job* job = new Job;
    auto before = std::make_shared<std::vector<std::string>>(g_badgePaths);
    g_badgePaths = paths;
    auto list = std::make_shared<std::vector<std::string>>(std::move(paths));
    worker::disk.postOp([=] {
        std::string err, note;
        bool ok = inst::badges(*list, job, err, note);
        if (ok) {
            if (FILE* f = fopen(BADGE_STATE_FILE, "w")) { for (auto& p : *list) fprintf(f, "%s\n", p.c_str()); fclose(f); }
        }
        worker::toMain([=] {
            if (ok) g_reboot = true;
            else if (g_badgePaths == *list) g_badgePaths = *before;   // nothing was written: the old list still holds
            for (Item* it : g_home[K_BADGE]) it->installed = std::find(g_badgePaths.begin(), g_badgePaths.end(), it->path) != g_badgePaths.end();
            bump();
            finish(job, ok, err, note);
        });
    });
    return job;
}

Job* installBadges(const std::vector<Item*>& sets) {
    std::vector<std::string> paths = g_badgePaths;
    for (Item* it : sets) if (std::find(paths.begin(), paths.end(), it->path) == paths.end()) paths.push_back(it->path);
    return applyBadges(paths);
}

Job* removeBadges(Item* set) {
    std::vector<std::string> paths = g_badgePaths;
    paths.erase(std::remove(paths.begin(), paths.end(), set->path), paths.end());
    return applyBadges(paths);
}

Job* download(Item* it) {
    Job* job = new Job;
    runDownload(job, it->plazaId, it, it->name, it->author, (int)it->kind, false);
    return job;
}

Job* downloadId(long id) {
    Job* job = new Job;
    Item* known = nullptr;
    auto f = g_plazaItems.find(id);
    if (f != g_plazaItems.end()) known = f->second;
    runDownload(job, id, known, known ? known->name : "", known ? known->author : "", known ? (int)known->kind : -1, true);
    return job;
}

Job* dumpTheme() {
    Job* job = new Job;
    worker::disk.postOp([=] {
        std::string err, dir;
        bool ok = inst::dumpTheme(dir, job, err);
        auto rec = std::make_shared<lib::Rec>();
        if (ok) { ok = lib::add(K_THEME, dir, *rec); if (ok) lib::save(); else err = "The copy could not be read back"; }
        auto a = std::make_shared<inst::Applied>();
        if (ok) inst::readApplied(*a);
        worker::toMain([=] {
            if (ok) { g_lastAdded = itemFromRec(*rec); markApplied(*a); }
            finish(job, ok, err);
        });
    });
    return job;
}

Item* lastAdded() { return g_lastAdded; }
Item* collectionItem(long plazaId) { return plazaId > 0 ? homeByPlazaId(plazaId) : nullptr; }

// the loose-badges set is the /Badges folder itself: never remove that
bool deletable(Item* it) { return it && !it->more && !it->plaza && !it->path.empty() && it->path != lib::DIRS[K_BADGE]; }

void deleteFromSD(Item* it) {
    if (!deletable(it)) return;
    if (it == g_lastAdded) g_lastAdded = nullptr;
    auto& v = g_home[it->kind];
    v.erase(std::remove(v.begin(), v.end(), it), v.end());
    g_installed.erase(std::remove(g_installed.begin(), g_installed.end(), it), g_installed.end());
    it->onSD = false; it->gone = true;
    for (auto& kv : g_plazaItems) if (kv.second->path == it->path) { kv.second->onSD = false; kv.second->path.clear(); }
    std::string path = it->path;
    bump();
    worker::disk.postOp([path] {
        struct stat st;
        if (stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode)) {
            // a folder: remove the files the app reads from it, then the folders they were in (deepest first), then the folder
            Pack pk;
            if (pk.open(path)) {
                std::vector<std::string> names, dirs;
                for (auto& e : pk.entries()) names.push_back(e.name);
                pk.close();
                for (auto& n : names) {
                    remove((path + "/" + n).c_str());
                    for (size_t s = n.find('/'); s != std::string::npos; s = n.find('/', s + 1)) {
                        std::string d = n.substr(0, s);
                        if (std::find(dirs.begin(), dirs.end(), d) == dirs.end()) dirs.push_back(d);
                    }
                }
                std::sort(dirs.begin(), dirs.end(), [](const std::string& a, const std::string& b) { return a.size() > b.size(); });
                for (auto& d : dirs) rmdir((path + "/" + d).c_str());
            }
            rmdir(path.c_str());
        } else {
            // the music thread may still have the zip open for a moment after its preview was stopped
            for (int i = 0; i < 20 && remove(path.c_str()) != 0; i++) svcSleepThread(100 * 1000 * 1000LL);
        }
        lib::forget(path);
        lib::save();
    });
}

void freeJob(Job* j) { delete j; }
bool needsReboot() { return g_reboot; }

int setting(const char* key, int fallback) {
    auto f = g_settings.find(key);
    return f == g_settings.end() ? fallback : f->second;
}

void setSetting(const char* key, int value) {
    auto f = g_settings.find(key);
    if (f != g_settings.end() && f->second == value) return;
    g_settings[key] = value;
    saveSettings();
}

}  // namespace backend
