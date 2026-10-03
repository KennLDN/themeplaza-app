#include "library.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <strings.h>
#include <sys/stat.h>
#include "log.h"
#include "plaza.h"
#include "appdir.h"

namespace lib {

const char* const DIRS[3] = {"sdmc:/Themes", "sdmc:/Splashes", "sdmc:/Badges"};

namespace {

const char* const INDEX_FILE = APP_DIR "/cache/library.bin";
const char* const ICONS_FILE = APP_DIR "/cache/icons.bin";
constexpr u32 INDEX_MAGIC = 0x424c4e41, INDEX_VERSION = 5;      // "ANLB"
constexpr u8 UNUSABLE = 0x80;      // flag: the file was looked at and is not a theme / splash / badge set

std::vector<Rec> g_recs;
bool g_loaded = false, g_dirty = false;
FILE* g_icons = nullptr;
FILE* g_iconsRead = nullptr;    // reading goes through a handle of its own: one icon per SD read, not the write buffer's 64 KB
bool g_iconsUnflushed = false;

bool endsWith(const std::string& s, const char* ext) {
    size_t n = strlen(ext);
    return s.size() >= n && strcasecmp(s.c_str() + s.size() - n, ext) == 0;
}

void putStr(FILE* f, const std::string& s) { u16 n = (u16)(s.size() > 0xffff ? 0xffff : s.size()); fwrite(&n, 2, 1, f); fwrite(s.data(), 1, n, f); }
bool getStr(FILE* f, std::string& s) { u16 n; if (fread(&n, 2, 1, f) != 1) return false; s.resize(n); return n == 0 || fread(&s[0], 1, n, f) == n; }

struct Fixed { u8 kind, flags; u16 badgeCount; u32 fileSize, mtime, bodySize, bodyCrc, auxSize, auxCrc; s32 iconSlot; };

int freeSlot() {
    std::vector<bool> used;
    for (auto& r : g_recs) if (r.iconSlot >= 0) { if ((size_t)r.iconSlot >= used.size()) used.resize(r.iconSlot + 1, false); used[r.iconSlot] = true; }
    for (size_t i = 0; i < used.size(); i++) if (!used[i]) return (int)i;
    return (int)used.size();
}

bool openIcons() {
    if (g_icons) return true;
    g_icons = fopen(ICONS_FILE, "r+b");
    if (!g_icons) g_icons = fopen(ICONS_FILE, "w+b");
    if (g_icons) setvbuf(g_icons, nullptr, _IOFBF, 64 * 1024);
    return g_icons != nullptr;
}

// Written through stdio's buffer; save() flushes it. A scan of many new files then costs few SD card writes.
void writeIcon(int slot, const u8* icon) {
    if (!openIcons()) return;
    fseek(g_icons, (long)slot * fmt::SMDH_ICON_SIZE, SEEK_SET);
    fwrite(icon, 1, fmt::SMDH_ICON_SIZE, g_icons);
    g_iconsUnflushed = true;
}

// "Title by Author (123).zip" -> title, author. Anything else: the file name without its extension.
void nameFromFile(const std::string& file, std::string& name, std::string& author) {
    std::string s = file;
    if (endsWith(s, ".zip")) s.resize(s.size() - 4);
    if (plaza::idFromFileName(s)) { s.resize(s.rfind('(')); while (!s.empty() && s.back() == ' ') s.pop_back(); }
    size_t by = s.rfind(" by ");
    if (by != std::string::npos && by > 0) { name = s.substr(0, by); author = s.substr(by + 4); }
    else { name = s; author.clear(); }
}

u32 entryCrc(Pack& pk, const PackEntry& e, size_t maxSize) {
    if (pk.isZip()) return e.crc;
    std::vector<u8> buf;
    return pk.read(e, buf, maxSize) ? crc32Of(buf.data(), buf.size()) : 0;
}

bool iconFromPng(Pack& pk, const PackEntry& e, u8* icon) {
    std::vector<u8> buf; fmt::Png png;
    if (!pk.read(e, buf, 2 * 1024 * 1024) || !fmt::decodePng(buf.data(), buf.size(), png, 512 * 512)) return false;
    fmt::iconFromRGBA(png.rgba.data(), png.w, png.h, icon);
    return true;
}

bool build(Kind kind, const std::string& path, u32 fileSize, bool isDir, Rec& r, u8* icon) {
    Pack pk;
    if (!pk.open(path, isDir ? 1 : 0)) return false;
    r = Rec();
    r.kind = (u8)kind; r.path = path; r.fileSize = fileSize;
    const bool looseSet = path == DIRS[K_BADGE];
    if (looseSet) r.name = "Other Badges"; else nameFromFile(baseName(path), r.name, r.author);
    // "Recently added" goes by when the app first saw the file. Asking the card for each file's date would
    // cost an extra call per file, and a date set on a PC says little about when the file reached this card.
    r.mtime = (u32)time(nullptr);

    std::vector<u8> buf;
    bool haveIcon = false;
    if (const PackEntry* e = looseSet ? nullptr : pk.find("info.smdh")) {
        fmt::Smdh s;
        if (pk.read(*e, buf, 0x4000) && fmt::parseSmdh(buf.data(), buf.size(), s)) {
            if (!s.name.empty()) { r.name = s.name; r.author = s.author; r.desc = s.desc; }
            if (s.hasIcon) { memcpy(icon, s.icon, fmt::SMDH_ICON_SIZE); haveIcon = true; }
        }
    }
    if (kind == K_THEME) {
        const PackEntry* body = pk.find("body_LZ.bin");
        if (!body || body->size < 8 || body->size > 0x150000) return false;
        r.bodySize = body->size;
        if (pk.isZip()) r.bodyCrc = body->crc;
        if (const PackEntry* bgm = pk.find("bgm.bcstm")) {
            // the theme's header says whether the HOME Menu plays the music at all; its first bytes are enough to tell
            u8 packed[64], head[8];
            size_t got = pk.readHead(*body, packed, sizeof packed);
            bool on = fmt::lz11Head(packed, got, head, 6) == 6 ? head[5] != 0 : true;
            if (on) r.flags |= 1;
            r.auxSize = bgm->size;
        }
        if (!haveIcon || !pk.isZip()) {
            if (!pk.read(*body, buf, 0x150000)) return false;
            if (!pk.isZip()) r.bodyCrc = crc32Of(buf.data(), buf.size());
            if (!haveIcon) {
                fmt::ThemeLook look;
                if (fmt::decodeTheme(buf.data(), buf.size(), look)) { fmt::iconFromTiled(look.top.data.data(), look.top.texW, look.top.w, look.top.h, icon); haveIcon = true; }
            }
        }
    } else if (kind == K_SPLASH) {
        const PackEntry* top = pk.find("splash.bin"); const PackEntry* bot = pk.find("splashbottom.bin");
        if (top && top->size != 288000) top = nullptr;
        if (bot && bot->size != 230400) bot = nullptr;
        if (!top && !bot) return false;
        if (top) { r.bodySize = top->size; r.bodyCrc = entryCrc(pk, *top, 300000); }
        if (bot) { r.auxSize = bot->size; r.auxCrc = entryCrc(pk, *bot, 300000); }
        if (!haveIcon) if (const PackEntry* e = pk.find("icon.png")) haveIcon = iconFromPng(pk, *e, icon);
        if (!haveIcon) {
            const PackEntry* e = top ? top : bot; fmt::Image img;
            if (pk.read(*e, buf, 300000) && fmt::decodeSplash(buf.data(), buf.size(), top ? 400 : 320, img)) { fmt::iconFromTiled(img.data.data(), img.texW, img.w, img.h, icon); haveIcon = true; }
        }
    } else {
        auto list = badgeEntries(pk, path);
        int count = 0;
        for (const PackEntry* e : list) {
            u8 head[24]; int w, h;
            if (pk.readHead(*e, head, 24) != 24 || !fmt::pngSize(head, 24, w, h) || w % 64 || h % 64 || (w / 64) * (h / 64) > 72) continue;
            count += (w / 64) * (h / 64);
        }
        if (!count) return false;
        r.badgeCount = (u16)(count > 0xffff ? 0xffff : count);
        if (const PackEntry* e = looseSet ? nullptr : pk.find("_seticon.png")) haveIcon = iconFromPng(pk, *e, icon);
        if (!haveIcon && !list.empty()) haveIcon = iconFromPng(pk, *list[0], icon);
    }
    if (!haveIcon) memset(icon, 0xff, fmt::SMDH_ICON_SIZE);   // plain white tile
    return true;
}

int findRec(const std::string& path) {
    for (size_t i = 0; i < g_recs.size(); i++) if (g_recs[i].path == path) return (int)i;
    return -1;
}

struct DirEnt { std::string name; bool dir; u32 size; };

bool listDir(const char* sdPath, std::vector<DirEnt>& out) {
    FS_Archive sd;
    if (R_FAILED(FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")))) return false;
    Handle h;
    bool ok = R_SUCCEEDED(FSUSER_OpenDirectory(&h, sd, fsMakePath(PATH_ASCII, sdPath)));
    if (ok) {
        static FS_DirectoryEntry ents[32];
        for (;;) {
            u32 n = 0;
            if (R_FAILED(FSDIR_Read(h, &n, 32, ents)) || n == 0) break;
            for (u32 i = 0; i < n; i++) {
                u8 name[0x106 * 3 + 1];
                ssize_t len = utf16_to_utf8(name, ents[i].name, sizeof name - 1);
                if (len <= 0) continue;
                name[len] = 0;
                if (name[0] == '.') continue;
                out.push_back({(const char*)name, (ents[i].attributes & FS_ATTRIBUTE_DIRECTORY) != 0, (u32)ents[i].fileSize});
            }
        }
        FSDIR_Close(h);
    }
    FSUSER_CloseArchive(sd);
    return ok;
}

}  // namespace

std::string baseName(const std::string& path) {
    size_t i = path.find_last_of('/');
    return i == std::string::npos ? path : path.substr(i + 1);
}

std::vector<const PackEntry*> badgeEntries(const Pack& pk, const std::string& path) {
    const bool looseSet = path == DIRS[K_BADGE];
    std::vector<const PackEntry*> v;
    for (auto& e : pk.entries()) {
        if (!endsWith(e.name, ".png")) continue;
        if (looseSet && e.name.find('/') != std::string::npos) continue;
        std::string base = baseName(e.name);
        if (!strcasecmp(base.c_str(), "preview.png") || !strcasecmp(base.c_str(), "_seticon.png")) continue;
        v.push_back(&e);
    }
    std::sort(v.begin(), v.end(), [](const PackEntry* a, const PackEntry* b) { return a->name < b->name; });
    return v;
}

void load() {
    if (g_loaded) return;
    g_loaded = true;
    mkdir(APP_DIR "/cache", 0777);
    FILE* f = fopen(INDEX_FILE, "rb");
    if (!f) return;
    u32 head[3];
    if (fread(head, 4, 3, f) == 3 && head[0] == INDEX_MAGIC && head[1] == INDEX_VERSION && head[2] < 20000) {
        g_recs.reserve(head[2]);
        for (u32 i = 0; i < head[2]; i++) {
            Fixed x; Rec r;
            if (fread(&x, sizeof x, 1, f) != 1 || !getStr(f, r.path) || !getStr(f, r.name) || !getStr(f, r.author) || !getStr(f, r.desc)) break;
            r.kind = x.kind; r.flags = x.flags; r.badgeCount = x.badgeCount; r.fileSize = x.fileSize; r.mtime = x.mtime;
            r.bodySize = x.bodySize; r.bodyCrc = x.bodyCrc; r.auxSize = x.auxSize; r.auxCrc = x.auxCrc; r.iconSlot = x.iconSlot;
            g_recs.push_back(std::move(r));
        }
    }
    fclose(f);
}

void save() {
    if (g_icons) fflush(g_icons);
    if (!g_dirty) return;
    FILE* f = fopen(INDEX_FILE, "wb");
    if (!f) return;
    u32 head[3] = {INDEX_MAGIC, INDEX_VERSION, (u32)g_recs.size()};
    fwrite(head, 4, 3, f);
    for (auto& r : g_recs) {
        Fixed x{r.kind, r.flags, r.badgeCount, r.fileSize, r.mtime, r.bodySize, r.bodyCrc, r.auxSize, r.auxCrc, r.iconSlot};
        fwrite(&x, sizeof x, 1, f);
        putStr(f, r.path); putStr(f, r.name); putStr(f, r.author); putStr(f, r.desc);
    }
    fclose(f);
    g_dirty = false;
}

namespace {

// A folder has no size of its own; the sizes of the files in it, added up, stand in for it, so that a
// folder whose contents changed is read again.
u32 folderSize(const std::string& path) {
    Pack pk;
    if (!pk.open(path)) return 0;
    u32 sum = 1;
    for (auto& e : pk.entries()) sum += e.size + 1;
    return sum;
}

bool addKnown(Kind kind, const std::string& path, u32 size, bool isDir, Rec& out) {
    alignas(4) static u8 icon[fmt::SMDH_ICON_SIZE];
    Rec r;
    if (!build(kind, path, size, isDir, r, icon)) return false;
    int i = findRec(path);
    r.iconSlot = i >= 0 && g_recs[i].iconSlot >= 0 ? g_recs[i].iconSlot : freeSlot();
    if (i >= 0 && g_recs[i].mtime) r.mtime = g_recs[i].mtime;     // a file that changed keeps its place in "recently added"
    r.seen = true;
    writeIcon(r.iconSlot, icon);
    if (i >= 0) g_recs[i] = r; else g_recs.push_back(r);
    g_dirty = true;
    out = r;
    return true;
}
}  // namespace

bool add(Kind kind, const std::string& path, Rec& out) {
    load();
    struct stat st;
    if (stat(path.c_str(), &st) != 0) return false;
    bool isDir = S_ISDIR(st.st_mode);
    bool ok = addKnown(kind, path, isDir ? folderSize(path) : (u32)st.st_size, isDir, out);
    if (g_icons) fflush(g_icons);
    return ok;
}

struct Scan {
    Kind kind;
    std::vector<DirEnt> ents;
    size_t at = 0;
    bool loosePng = false;
    int built = 0;
    u64 started = 0;
};

Scan* scanBegin(Kind kind) {
    load();
    Scan* s = new Scan;
    s->kind = kind; s->started = osGetTime();
    mkdir(DIRS[kind], 0777);
    listDir(DIRS[kind] + 5, s->ents);   // + 5: without "sdmc:"
    return s;
}

bool scanStep(Scan* s, int maxNew, std::vector<Rec>& found, int& done, int& total) {
    const Kind kind = s->kind;
    int builtNow = 0;
    total = (int)s->ents.size();
    while (s->at < s->ents.size() && builtNow < maxNew) {
        const DirEnt& e = s->ents[s->at++];
        if (!e.dir && !endsWith(e.name, ".zip")) { if (kind == K_BADGE && endsWith(e.name, ".png")) s->loosePng = true; continue; }
        std::string path = std::string(DIRS[kind]) + "/" + e.name;
        int i = findRec(path);
        u32 size = e.dir ? folderSize(path) : e.size;
        if (i >= 0 && g_recs[i].kind == kind && g_recs[i].fileSize == size) {
            g_recs[i].seen = true;
            if (!(g_recs[i].flags & UNUSABLE)) found.push_back(g_recs[i]);
            continue;
        }
        Rec r;
        if (addKnown(kind, path, size, e.dir, r)) found.push_back(r);
        else {
            // remember files that are not what this folder is for, so they are not opened again at every start
            i = findRec(path);
            if (i >= 0) g_recs.erase(g_recs.begin() + i);
            Rec bad; bad.kind = (u8)kind; bad.flags = UNUSABLE; bad.path = path; bad.fileSize = size; bad.seen = true;
            g_recs.push_back(bad);
            g_dirty = true;
        }
        builtNow++; s->built++;
    }
    done = (int)s->at;
    if (s->at < s->ents.size()) return false;
    if (s->built) LOG("scan %s: %d entries, %d read for the first time, %llu ms", DIRS[kind], total, s->built, (unsigned long long)(osGetTime() - s->started));
    if (s->loosePng) {
        // PNGs lying directly in /Badges form one set; it is cheap to rebuild, so it is not cached
        Rec r;
        if (addKnown(K_BADGE, DIRS[K_BADGE], 0, true, r)) found.push_back(r);
    }
    delete s;
    return true;
}

void forget(const std::string& path) {
    int i = findRec(path);
    if (i < 0) return;
    g_recs.erase(g_recs.begin() + i);
    g_dirty = true;
}

void dropUnseen() {
    size_t before = g_recs.size();
    g_recs.erase(std::remove_if(g_recs.begin(), g_recs.end(), [](const Rec& r) { return !r.seen; }), g_recs.end());
    if (g_recs.size() != before) g_dirty = true;
}

bool readIcon(int slot, u8* icon) {
    if (slot < 0 || !openIcons()) return false;
    if (g_iconsUnflushed) { fflush(g_icons); g_iconsUnflushed = false; }
    if (!g_iconsRead) {
        g_iconsRead = fopen(ICONS_FILE, "rb");
        if (!g_iconsRead) return false;
        setvbuf(g_iconsRead, nullptr, _IOFBF, fmt::SMDH_ICON_SIZE);
    }
    fseek(g_iconsRead, (long)slot * fmt::SMDH_ICON_SIZE, SEEK_SET);
    return fread(icon, 1, fmt::SMDH_ICON_SIZE, g_iconsRead) == fmt::SMDH_ICON_SIZE;
}

}  // namespace lib
