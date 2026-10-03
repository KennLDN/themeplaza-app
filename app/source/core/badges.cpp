// Writing badge sets into the HOME Menu's badge data (extdata 0x14D1). Layouts are documented on
// 3dbrew ("Home Menu", BadgeData.dat and BadgeMngFile.dat); see docs/badges-splashes.md.
//
// The badge data is rebuilt from the list of installed sets every time. Each badge gets an id made
// from its set and file name, so badges the user has already placed on the HOME Menu keep pointing at
// the right picture after a rebuild, and placements of badges that are gone are cleared.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include "extdata.h"
#include "formats.h"
#include "install.h"
#include "library.h"
#include "log.h"
#include "pack.h"
#include "appdir.h"

namespace inst {

namespace {

constexpr u32 DATA_SIZE = 0xF4DF80, MNG_SIZE = 0xD4A8;
constexpr u32 SET_NAMES = 0, BADGE_NAMES = 0x35E80, SET_ICONS = 0x250F80, IMG64 = 0x318F80, IMG32 = 0xCDCF80;
constexpr u32 NAME_SLOT = 0x8A, NAME_ENTRY = 16 * NAME_SLOT;          // 16 languages of 69 UTF-16 units
constexpr u32 IMG64_ENTRY = 0x2800, IMG32_ENTRY = 0xA00, SET_ICON_ENTRY = 0x2000;
constexpr u32 MNG_USED_BADGES = 0x358, MNG_USED_SETS = 0x3D8, MNG_BADGES = 0x3E8, MNG_SETS = 0xA028, MNG_LAYOUT = 0xB2E8;
constexpr u32 BADGE_INFO = 0x28, SET_INFO = 0x30, LAYOUT_SLOT = 0x18;
constexpr int MAX_BADGES = 1000, MAX_SETS = 100, LAYOUT_SLOTS = 360, MAX_TILES = 72;
constexpr int BATCH = 24;                                              // badges written per file write

inline u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
inline void wr32(u8* p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }
inline void wr16(u8* p, u16 v) { p[0] = v; p[1] = v >> 8; }

u32 hashId(const std::string& s, u32 salt) {
    u32 h = 2166136261u ^ salt;
    for (unsigned char c : s) { h ^= c; h *= 16777619u; }
    if (h == 0 || h == 0xFFFFFFFF) h = 0x5EED0001 + salt;
    return h;
}

// The same name in every language slot.
void putName(u8* entry, const std::string& name) {
    u16 units[69] = {};
    ssize_t n = utf8_to_utf16(units, (const u8*)name.c_str(), 68);
    if (n < 0) {        // not valid UTF-8: keep the plain letters
        n = 0;
        for (unsigned char c : name) if (n < 68) units[n++] = c < 0x80 ? c : '?';
    }
    units[n < 68 ? n : 68] = 0;
    for (int lang = 0; lang < 16; lang++) memcpy(entry + lang * NAME_SLOT, units, NAME_SLOT);
}

// One 64x64 tile of a decoded PNG -> RGB565 + A4 at 64x64 and at 32x32.
void convertTile(const fmt::Png& png, int ox, int oy, u8* img64, u8* img32) {
    memset(img64, 0, IMG64_ENTRY); memset(img32, 0, IMG32_ENTRY);
    u16* c64 = (u16*)img64; u8* a64 = img64 + 0x2000;
    u16* c32 = (u16*)img32; u8* a32 = img32 + 0x800;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            const u8* p = &png.rgba[4 * ((size_t)(oy + y) * png.w + ox + x)];
            u32 i = fmt::tiledIndex(x, y, 64);
            c64[i] = fmt::to565(p[0], p[1], p[2]);
            a64[i >> 1] |= (p[3] >> 4) << ((i & 1) * 4);
        }
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int s[4] = {0, 0, 0, 0};
            for (int k = 0; k < 4; k++) {
                const u8* p = &png.rgba[4 * ((size_t)(oy + 2 * y + (k >> 1)) * png.w + ox + 2 * x + (k & 1))];
                for (int c = 0; c < 4; c++) s[c] += p[c];
            }
            u32 i = fmt::tiledIndex(x, y, 32);
            c32[i] = fmt::to565(s[0] / 4, s[1] / 4, s[2] / 4);
            a32[i >> 1] |= ((s[3] / 4) >> 4) << ((i & 1) * 4);
        }
}

// Set icon: 64x64 RGB565 without alpha, of which the HOME Menu shows the top-left 48x48.
void convertSetIcon(const fmt::Png& png, u8* out) {
    u16* dst = (u16*)out;
    for (int y = 0; y < 64; y++)
        for (int x = 0; x < 64; x++) {
            int sx = (x < 48 ? x : 47) * png.w / 48, sy = (y < 48 ? y : 47) * png.h / 48;
            const u8* p = &png.rgba[4 * ((size_t)sy * png.w + sx)];
            // no alpha channel here: put the picture on white
            u8 r = (p[0] * p[3] + 255 * (255 - p[3])) / 255, g = (p[1] * p[3] + 255 * (255 - p[3])) / 255, b = (p[2] * p[3] + 255 * (255 - p[3])) / 255;
            dst[fmt::tiledIndex(x, y, 64)] = fmt::to565(r, g, b);
        }
}

// "Name.0002a000.png" -> the name before the first period, and a shortcut to system title 00040010-0002a000.
void nameAndShortcut(const std::string& file, std::string& name, u64& shortcut) {
    size_t dot = file.find('.');
    name = file.substr(0, dot);
    shortcut = 0xFFFFFFFFFFFFFFFFull;
    if (dot == std::string::npos) return;
    size_t dot2 = file.find('.', dot + 1);
    if (dot2 == std::string::npos || dot2 - dot - 1 != 8) return;
    u32 low = 0;
    for (size_t i = dot + 1; i < dot2; i++) {
        char c = file[i]; int v = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
        if (v < 0) return;
        low = (low << 4) | v;
    }
    shortcut = 0x0004001000000000ull | low;
}

struct Writer {
    Handle h = 0; Result rc = 0;
    void put(u64 off, const void* p, u32 n) { if (R_FAILED(rc)) return; u32 done = 0; rc = FSFILE_Write(h, &done, off, p, n, 0); if (R_SUCCEEDED(rc) && done != n) rc = -1; }
};

const char* const STATE_FILE = APP_DIR "/badges.txt";      // the sets this app installed last (written by the backend)

// Copies of badge data that came from somewhere else are kept in sdmc:/3ds/Theme Plaza/backup, "backup 2", ...
std::string backupDir(int i) { return i <= 1 ? std::string(APP_DIR "/backup") : APP_DIR "/backup " + std::to_string(i); }

bool backupComplete(const std::string& dir) {
    struct stat a, b;
    return stat((dir + "/BadgeData.dat").c_str(), &a) == 0 && a.st_size > 0 && stat((dir + "/BadgeMngFile.dat").c_str(), &b) == 0 && b.st_size > 0;
}

int newestBackup() {
    int newest = 0;
    for (int i = 1; i <= 40; i++) if (backupComplete(backupDir(i))) newest = i;
    return newest;
}

// A backup is "waiting" from the moment its data was replaced by this app's badges until it has been put
// back. Only a waiting backup is restored: one that was put back already must not return a second time,
// after the user has perhaps cleared their badges some other way.
std::string waitingMark(const std::string& dir) { return dir + "/replaced-by-theme-plaza.txt"; }
void setWaiting(const std::string& dir, bool on) {
    if (!on) { remove(waitingMark(dir).c_str()); return; }
    if (FILE* f = fopen(waitingMark(dir).c_str(), "w")) { fputs("The badge data in this folder was replaced by badges installed with Theme Plaza. It is put back when the last of those is removed.\n", f); fclose(f); }
}
bool isWaiting(const std::string& dir) { struct stat st; return stat(waitingMark(dir).c_str(), &st) == 0; }

// Does the list file hold badge sets this app did not put there? Its own sets have ids made from their paths.
bool foreignBadges(const std::vector<u8>& mng) {
    if (mng.empty()) return false;
    if (mng.size() != MNG_SIZE) return true;                   // not a file this app writes: keep a copy whatever it is
    if (rd32(&mng[8]) == 0) return false;                      // no badges in it
    std::vector<u32> ours;
    if (FILE* f = fopen(STATE_FILE, "r")) {
        char line[512];
        while (fgets(line, sizeof line, f)) { size_t n = strlen(line); while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0; if (n) ours.push_back(hashId(line, 0x5e7)); }
        fclose(f);
    }
    for (int i = 0; i < MAX_SETS; i++) {
        const u8* e = &mng[MNG_SETS + i * SET_INFO];
        u32 id = rd32(e + 0x10), count = rd32(e + 0x1C);
        if (id == 0xFFFFFFFF || count == 0 || count == 0xFFFFFFFF) continue;     // an unused entry
        if (std::find(ours.begin(), ours.end(), id) == ours.end()) return true;
    }
    return false;
}

// Before badge data from somewhere else (Badge Arcade, another tool) is replaced, both files are copied to a
// new backup folder. Returns false if that could not be done completely; nothing is replaced then.
bool backupForeign(ext::Archive& arc) {
    std::vector<u8> mng;
    Result rc = arc.read("/BadgeMngFile.dat", mng);
    if (R_FAILED(rc)) return ext::missing(rc);       // no badge data yet: nothing to keep. Anything else: do not go on without a copy
    if (!foreignBadges(mng)) return true;
    int newest = newestBackup();
    if (newest) {
        // the same data is already kept (it was put back from that backup and has not changed since)
        std::vector<u8> kept(mng.size() + 1);
        if (FILE* f = fopen((backupDir(newest) + "/BadgeMngFile.dat").c_str(), "rb")) {
            size_t got = fread(kept.data(), 1, kept.size(), f);
            fclose(f);
            if (got == mng.size() && !memcmp(kept.data(), mng.data(), got)) { setWaiting(backupDir(newest), true); return true; }
        }
    }
    u64 dataSize = 0;
    if (R_FAILED(arc.fileSize("/BadgeData.dat", dataSize)) || dataSize == 0 || dataSize > 64 * 1024 * 1024) return false;
    mkdir(APP_DIR, 0777);
    const std::string dir = backupDir(newest + 1);
    mkdir(dir.c_str(), 0777);
    const std::string dataPath = dir + "/BadgeData.dat", mngPath = dir + "/BadgeMngFile.dat";
    const std::string dataPart = dataPath + ".part", mngPart = mngPath + ".part";
    bool ok = false;
    if (FILE* f = fopen(dataPart.c_str(), "wb")) {
        std::vector<u8> buf(256 * 1024);
        ok = true;
        for (u64 off = 0; off < dataSize && ok; off += buf.size()) {
            u32 n = dataSize - off < buf.size() ? (u32)(dataSize - off) : (u32)buf.size();
            ok = R_SUCCEEDED(arc.readAt("/BadgeData.dat", off, buf.data(), n)) && fwrite(buf.data(), 1, n, f) == n;
        }
        if (fclose(f) != 0) ok = false;
    }
    if (ok) {
        ok = false;
        if (FILE* f = fopen(mngPart.c_str(), "wb")) { ok = fwrite(mng.data(), 1, mng.size(), f) == mng.size(); if (fclose(f) != 0) ok = false; }
    }
    // the copies get their real names only when both are complete
    if (ok) ok = rename(dataPart.c_str(), dataPath.c_str()) == 0 && rename(mngPart.c_str(), mngPath.c_str()) == 0;
    if (!ok) { remove(dataPart.c_str()); remove(mngPart.c_str()); remove(dataPath.c_str()); remove(mngPath.c_str()); LOG("badges: backup to %s failed", dir.c_str()); return false; }
    setWaiting(dir, true);
    LOG("badges: the badge data that was on this console is kept in %s", dir.c_str());
    return true;
}

// Puts a backup back into the HOME Menu's badge data (the data file first, the list last).
bool restoreBackup(ext::Archive& arc, const std::string& dir, Job* job, std::string& err) {
    std::vector<u8> mng;
    if (FILE* f = fopen((dir + "/BadgeMngFile.dat").c_str(), "rb")) {
        mng.resize(MNG_SIZE);
        size_t got = fread(mng.data(), 1, MNG_SIZE, f);
        bool more = fgetc(f) != EOF;
        fclose(f);
        if (got != MNG_SIZE || more) mng.clear();
    }
    FILE* f = mng.empty() ? nullptr : fopen((dir + "/BadgeData.dat").c_str(), "rb");
    if (!f) { err = "The backup of your earlier badges could not be read"; return false; }
    Result rc;
    Handle h = 0;
    bool ok = R_SUCCEEDED(rc = arc.ensure("/BadgeData.dat", DATA_SIZE)) && R_SUCCEEDED(rc = arc.ensure("/BadgeMngFile.dat", MNG_SIZE)) &&
              R_SUCCEEDED(rc = arc.canWrite("/BadgeMngFile.dat")) &&
              R_SUCCEEDED(rc = FSUSER_OpenFile(&h, arc.handle(), fsMakePath(PATH_ASCII, "/BadgeData.dat"), FS_OPEN_WRITE, 0));
    if (ok) {
        std::vector<u8> buf(256 * 1024);
        for (u32 off = 0; off < DATA_SIZE && ok; off += buf.size()) {
            u32 n = DATA_SIZE - off < buf.size() ? DATA_SIZE - off : (u32)buf.size(), put = 0;
            size_t got = fread(buf.data(), 1, n, f);
            if (got < n) memset(buf.data() + got, 0, n - got);
            ok = R_SUCCEEDED(rc = FSFILE_Write(h, &put, off, buf.data(), n, off + n >= DATA_SIZE ? FS_WRITE_FLUSH : 0)) && put == n;
            job->progress = 0.05f + 0.9f * (off + n) / DATA_SIZE;
        }
        FSFILE_Close(h);
    }
    fclose(f);
    if (ok) ok = R_SUCCEEDED(rc = arc.writeAt("/BadgeMngFile.dat", 0, mng.data(), MNG_SIZE));
    if (!ok) { LOG("badges: restore from %s failed %08lX", dir.c_str(), (unsigned long)rc); err = "Your earlier badges could not be put back"; }
    return ok;
}

u32 principalId(u32 existing, bool& failed) {
    u32 pid = existing;
    failed = true;
    // Asking for a service that no module has registered waits for it to appear, which would be for ever.
    // Whether it is registered can only be asked of Luma's service manager (the question is its extension).
    // Where it cannot be asked: the installed title lists the account module as a dependency, so it is
    // running; a .3dsx has no such guarantee and leaves the id as it is.
    bool there = false;
    if (R_FAILED(srvIsServiceRegistered(&there, "act:u"))) there = !envIsHomebrew();
    if (!there) { LOG("badges: no account service, keeping the existing id"); return pid; }
    Result rc = actInit(true);
    if (R_FAILED(rc)) { LOG("badges: actInit failed %08lX, keeping the existing id", (unsigned long)rc); return pid; }
    // The session is initialised first, as the system's own callers do (SDK version word, no shared memory).
    rc = ACT_Initialize(0xB0002C8, 0, 0);
    if (R_FAILED(rc)) LOG("badges: ACT_Initialize %08lX", (unsigned long)rc);
    u32 v = 0;
    rc = ACT_GetAccountInfo(&v, sizeof v, ACT_DEFAULT_ACCOUNT, INFO_TYPE_PRINCIPAL_ID);
    if (R_SUCCEEDED(rc)) { pid = v; failed = false; } else LOG("badges: account id lookup failed %08lX, keeping the existing id", (unsigned long)rc);
    actExit();
    return pid;
}

}  // namespace

bool badges(const std::vector<std::string>& sets, Job* job, std::string& err, std::string& note) {
    ext::Archive arc;
    Result rc = arc.open(ext::BADGE_ID);
    if (R_FAILED(rc)) {
        if (!ext::missing(rc)) { LOG("badge extdata: open %08lX", (unsigned long)rc); err = "The HOME Menu's badge data could not be opened"; return false; }
        if (sets.empty()) return true;                 // nothing there and nothing to put there
        Result made = ext::create(ext::BADGE_ID);
        rc = arc.open(ext::BADGE_ID);
        if (R_FAILED(rc)) { LOG("badge extdata: create %08lX open %08lX", (unsigned long)made, (unsigned long)rc); err = "The badge data could not be created on this console"; return false; }
    }
    auto busy = [&](Result r, const char* what) {
        LOG("badges: %s failed %08lX", what, (unsigned long)r);
        err = (u32)r == 0xC92044E6 ? "The HOME Menu is using its badge data. Wait a few seconds on the HOME Menu, then try again" : std::string("Could not write the badge data (") + what + ")";
        return false;
    };
    if (!backupForeign(arc)) { err = "Could not back up the badges already on this console (is the SD card full?)"; return false; }
    job->progress = 0.03f;
    // Removing the last set this app installed: if the console had other badges before, they come back.
    if (sets.empty()) {
        int newest = newestBackup();
        if (newest && isWaiting(backupDir(newest))) {
            if (!restoreBackup(arc, backupDir(newest), job, err)) return false;
            setWaiting(backupDir(newest), false);
            note = "Badges removed. The ones you had before are back";
            job->progress = 1;
            LOG("badges: restored from %s", backupDir(newest).c_str());
            return true;
        }
    }

    std::vector<u8> old, mng(MNG_SIZE, 0);
    bool hadOld = R_SUCCEEDED(arc.read("/BadgeMngFile.dat", old)) && old.size() == MNG_SIZE;
    // Entries that are not in use are not left as zeros. The HOME Menu takes a set's badges to run up to the
    // next set's first badge, so an all-zero set entry after the last real one ("first badge: 0") makes every
    // set look empty. Found by testing against the HOME Menu; the fill values are the ones a blank file made
    // by the PC tool Advanced Badge Editor has.
    for (int i = 0; i < MAX_BADGES; i++) {
        u8* e = &mng[MNG_BADGES + i * BADGE_INFO];
        memset(e + 0x04, 0xff, 10); memset(e + 0x18, 0xff, 8);
    }
    for (int i = 0; i < MAX_SETS; i++) {
        u8* e = &mng[MNG_SETS + i * SET_INFO];
        memset(e, 0xff, 8); wr32(e + 0x0C, 0x2710); memset(e + 0x10, 0xff, 12); memset(e + 0x24, 0xff, 4);
    }
    for (int i = 0; i < LAYOUT_SLOTS; i++) {
        u8* e = &mng[MNG_LAYOUT + i * LAYOUT_SLOT];
        memset(e + 0x04, 0xff, 10); memset(e + 0x10, 0xff, 8);
    }
    if (R_FAILED(rc = arc.ensure("/BadgeData.dat", DATA_SIZE))) return busy(rc, "data file");
    if (R_FAILED(rc = arc.ensure("/BadgeMngFile.dat", MNG_SIZE)) || R_FAILED(rc = arc.canWrite("/BadgeMngFile.dat"))) return busy(rc, "list file");
    job->progress = 0.06f;

    Writer out;
    if (R_FAILED(rc = FSUSER_OpenFile(&out.h, arc.handle(), fsMakePath(PATH_ASCII, "/BadgeData.dat"), FS_OPEN_WRITE, 0))) return busy(rc, "data file");

    // how many badges there are in all, for the progress bar
    int expected = 0;
    {
        // a cheap estimate: one per PNG; multi-tile PNGs only make the bar run slightly unevenly
        for (auto& path : sets) { Pack pk; if (pk.open(path)) expected += (int)lib::badgeEntries(pk, path).size(); }
        if (expected < 1) expected = 1;
    }

    struct Placed { u32 badgeId, setId; u16 slot; };
    std::vector<Placed> all;            // every badge written, for remapping the HOME Menu layout
    std::vector<u8> names(BATCH * NAME_ENTRY), big(BATCH * IMG64_ENTRY), small(BATCH * IMG32_ENTRY), file;
    alignas(4) static u8 setIcon[SET_ICON_ENTRY]; alignas(4) static u8 setName[NAME_ENTRY];   // read and written as 16-bit values
    int slot = 0, setCount = 0, batchStart = 0, inBatch = 0, done = 0;
    bool full = false;
    auto flush = [&] {
        if (!inBatch) return;
        out.put(BADGE_NAMES + (u64)batchStart * NAME_ENTRY, names.data(), inBatch * NAME_ENTRY);
        out.put(IMG64 + (u64)batchStart * IMG64_ENTRY, big.data(), inBatch * IMG64_ENTRY);
        out.put(IMG32 + (u64)batchStart * IMG32_ENTRY, small.data(), inBatch * IMG32_ENTRY);
        batchStart = slot; inBatch = 0;
    };

    bool tooManySets = false;
    for (auto& path : sets) {
        if (setCount >= MAX_SETS) { tooManySets = true; break; }
        if (full) break;
        Pack pk;
        if (!pk.open(path)) continue;
        const bool loose = path == lib::DIRS[K_BADGE];
        std::string setTitle = loose ? "Other Badges" : lib::baseName(path);
        if (setTitle.size() > 4 && !strcasecmp(setTitle.c_str() + setTitle.size() - 4, ".zip")) setTitle.resize(setTitle.size() - 4);
        // "Title by Author (id)" -> Title
        size_t by = setTitle.rfind(" by ");
        if (by != std::string::npos && by > 0 && setTitle.back() == ')') setTitle.resize(by);
        const u32 setId = hashId(path, 0x5e7);
        const int first = slot;
        bool iconDone = false;
        fmt::Png png;
        if (const PackEntry* e = loose ? nullptr : pk.find("_seticon.png"))
            if (pk.read(*e, file, 1024 * 1024) && fmt::decodePng(file.data(), file.size(), png, 256 * 256)) { convertSetIcon(png, setIcon); iconDone = true; }

        for (const PackEntry* e : lib::badgeEntries(pk, path)) {
            if (full) break;
            done++;
            job->progress = 0.06f + 0.86f * done / expected;
            if (!pk.read(*e, file, 4 * 1024 * 1024) || !fmt::decodePng(file.data(), file.size(), png, 768 * 768)) continue;
            int tw = png.w / 64, th = png.h / 64;
            if (png.w % 64 || png.h % 64 || tw * th < 1 || tw * th > MAX_TILES) continue;
            if (!iconDone) { convertSetIcon(png, setIcon); iconDone = true; }
            std::string title; u64 shortcut;
            nameAndShortcut(lib::baseName(e->name), title, shortcut);
            // larger pictures are cut into badges column by column, as the existing community tools do
            for (int tx = 0; tx < tw && !full; tx++)
                for (int ty = 0; ty < th; ty++) {
                    if (slot >= MAX_BADGES) { full = true; break; }
                    putName(&names[inBatch * NAME_ENTRY], title);
                    convertTile(png, tx * 64, ty * 64, &big[inBatch * IMG64_ENTRY], &small[inBatch * IMG32_ENTRY]);
                    u32 badgeId = hashId(e->name, setId + tx * th + ty);
                    while (std::find_if(all.begin(), all.end(), [&](const Placed& p) { return p.badgeId == badgeId; }) != all.end()) badgeId = badgeId * 31 + 7;
                    u8* info = &mng[MNG_BADGES + slot * BADGE_INFO];
                    memset(info, 0, BADGE_INFO);
                    wr32(info + 0x04, badgeId); wr32(info + 0x08, setId); wr16(info + 0x0C, (u16)slot); wr16(info + 0x0E, 0);
                    wr16(info + 0x12, 0xFFFF);                           // quantity: as many as the user wants to place
                    for (int k = 0; k < 2; k++) { wr32(info + 0x18 + 8 * k, (u32)shortcut); wr32(info + 0x1C + 8 * k, (u32)(shortcut >> 32)); }
                    mng[MNG_USED_BADGES + slot / 8] |= 1 << (slot % 8);
                    all.push_back({badgeId, setId, (u16)slot});
                    slot++;
                    if (++inBatch == BATCH) flush();
                }
        }
        int count = slot - first;
        if (!count) continue;
        putName(setName, setTitle);
        out.put(SET_NAMES + (u64)setCount * NAME_ENTRY, setName, NAME_ENTRY);
        if (!iconDone) memset(setIcon, 0xff, sizeof setIcon);
        out.put(SET_ICONS + (u64)setCount * SET_ICON_ENTRY, setIcon, SET_ICON_ENTRY);
        u8* s = &mng[MNG_SETS + setCount * SET_INFO];
        memset(s, 0, SET_INFO);
        wr32(s + 0x00, 0xFFFFFFFF); wr32(s + 0x04, 0xFFFFFFFF); wr32(s + 0x08, 0); wr32(s + 0x0C, 0x2710);
        wr32(s + 0x10, setId); wr32(s + 0x14, setCount); wr32(s + 0x18, 0xFFFFFFFF);
        wr32(s + 0x1C, count); wr32(s + 0x20, (u32)count * 0xFFFF); wr32(s + 0x24, first);
        mng[MNG_USED_SETS + setCount / 8] |= 1 << (setCount % 8);
        setCount++;
    }
    flush();
    { Result fr = FSFILE_Flush(out.h); if (R_SUCCEEDED(out.rc)) out.rc = fr; }
    FSFILE_Close(out.h);
    if (R_FAILED(out.rc)) return busy(out.rc, "pictures");

    // Badges the user has placed on the HOME Menu: keep the ones that still exist, pointing at their new slot.
    int placedTotal = 0;
    if (hadOld) {
        for (int i = 0; i < LAYOUT_SLOTS; i++) {
            const u8* o = &old[MNG_LAYOUT + i * LAYOUT_SLOT];
            u8* n = &mng[MNG_LAYOUT + i * LAYOUT_SLOT];
            u32 badgeId = rd32(o + 4), setId = rd32(o + 8);
            if (badgeId == 0xFFFFFFFF || (badgeId == 0 && setId == 0)) continue;   // nothing placed here: stays an unused slot
            auto f = std::find_if(all.begin(), all.end(), [&](const Placed& p) { return p.badgeId == badgeId && p.setId == setId; });
            if (f == all.end()) continue;                                 // its badge is gone: the slot becomes unused
            memcpy(n, o, LAYOUT_SLOT);
            wr16(n + 0x0C, f->slot);
            u8* info = &mng[MNG_BADGES + f->slot * BADGE_INFO];
            wr16(info + 0x10, (u16)(info[0x10] | (info[0x11] << 8)) + 1);
            placedTotal++;
        }
    }
    wr32(&mng[0x00], 0); wr32(&mng[0x04], setCount); wr32(&mng[0x08], slot); wr32(&mng[0x0C], placedTotal);
    wr32(&mng[0x10], 0xFFFFFFFF); wr32(&mng[0x14], 0); wr32(&mng[0x18], (u32)slot * 0xFFFF);
    bool noAccount = false;
    wr32(&mng[0x1C], principalId(hadOld ? rd32(&old[0x1C]) : 0, noAccount));
    if (R_FAILED(rc = arc.writeAt("/BadgeMngFile.dat", 0, mng.data(), MNG_SIZE))) return busy(rc, "list file");
    if (full) note = "Only the first 1000 badges fit on the HOME Menu";
    else if (tooManySets) note = "Only the first 100 badge sets fit on the HOME Menu";
    else if (noAccount && slot > 0) note = "Badges added, but the console's Nintendo Network ID could not be read. They may not show";
    job->progress = 1;
    LOG("badges: %d sets, %d badges, %d placed kept", setCount, slot, placedTotal);
    return true;
}

}  // namespace inst
