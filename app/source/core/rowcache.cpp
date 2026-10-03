#include "rowcache.h"
#include <cstdio>
#include <cstring>
#include <ctime>
#include <sys/stat.h>
#include <unordered_map>
#include "log.h"
#include "appdir.h"

namespace rowcache {

namespace {

const char* const BIN = APP_DIR "/cache/plaza_rows.bin";
const char* const IDX = APP_DIR "/cache/plaza_rows.idx";
constexpr u32 MAX_AGE = 14 * 24 * 3600;            // seconds
constexpr u32 STATS_AGE = 24 * 3600;               // download counts and likes are asked for again after a day
constexpr u32 MAX_RECORDS = 3000;                  // about 16 MB; past that the cache starts again

struct Rec {
    u32 id, time;
    char name[200], author[200], desc[392];
    u8 icon[fmt::SMDH_ICON_SIZE];
    u32 statsTime, downloads, likes;               // statsTime 0: no figures kept
    u8 bgm;
    u8 pad[11];
};
constexpr size_t RECORD = sizeof(Rec);
static_assert(RECORD == 5432, "row record size");
struct Entry { u32 id, time; };                    // one per record in the index file
struct Where { u32 record, time; };

bool g_loaded = false;
std::unordered_map<u32, Where> g_where;
u32 g_count = 0;
FILE* g_bin = nullptr;
Rec g_rec;

void openBin(const char* mode) {
    g_bin = fopen(BIN, mode);
    if (g_bin) setvbuf(g_bin, nullptr, _IONBF, 0);
}

void load() {
    if (g_loaded) return;
    g_loaded = true;
    mkdir(APP_DIR "/cache", 0777);
    if (FILE* f = fopen(IDX, "rb")) {
        Entry e[128]; size_t n;
        while ((n = fread(e, sizeof(Entry), 128, f)) > 0)
            for (size_t i = 0; i < n; i++) g_where[e[i].id] = {g_count++, e[i].time};
        fclose(f);
    }
    openBin("r+b");
    if (!g_bin) { openBin("w+b"); g_where.clear(); g_count = 0; remove(IDX); }
}

void startAgain() {
    if (g_bin) fclose(g_bin);
    openBin("w+b");
    remove(IDX);
    g_where.clear(); g_count = 0;
}

void copyText(char* dst, size_t size, const std::string& s) {
    size_t n = s.size() < size - 1 ? s.size() : size - 1;
    while (n > 0 && n < s.size() && ((u8)s[n] & 0xC0) == 0x80) n--;    // do not cut a UTF-8 character in half
    memcpy(dst, s.data(), n); memset(dst + n, 0, size - n);
}

}  // namespace

bool get(long plazaId, fmt::Smdh& out) {
    load();
    if (plazaId <= 0 || !g_bin) return false;
    auto f = g_where.find((u32)plazaId);
    if (f == g_where.end()) return false;
    u32 now = (u32)time(nullptr);
    if (now - f->second.time > MAX_AGE) return false;           // kept, but due to be fetched again (put() then writes over it)
    if (fseek(g_bin, (long)(f->second.record * RECORD), SEEK_SET) != 0 || fread(&g_rec, 1, RECORD, g_bin) != RECORD || g_rec.id != (u32)plazaId) { g_where.erase(f); return false; }
    g_rec.name[sizeof g_rec.name - 1] = g_rec.author[sizeof g_rec.author - 1] = g_rec.desc[sizeof g_rec.desc - 1] = 0;
    out.name = g_rec.name; out.author = g_rec.author; out.desc = g_rec.desc;
    memcpy(out.icon, g_rec.icon, sizeof out.icon); out.hasIcon = true;
    return true;
}

void put(long plazaId, const fmt::Smdh& row) {
    load();
    if (plazaId <= 0 || !g_bin || !row.hasIcon) return;
    auto f = g_where.find((u32)plazaId);
    if (f == g_where.end() && g_count >= MAX_RECORDS) { startAgain(); if (!g_bin) return; f = g_where.end(); }
    u32 record = f == g_where.end() ? g_count : f->second.record;
    memset(&g_rec, 0, sizeof g_rec);
    g_rec.id = (u32)plazaId; g_rec.time = (u32)time(nullptr);
    copyText(g_rec.name, sizeof g_rec.name, row.name); copyText(g_rec.author, sizeof g_rec.author, row.author); copyText(g_rec.desc, sizeof g_rec.desc, row.desc);
    memcpy(g_rec.icon, row.icon, sizeof g_rec.icon);
    if (fseek(g_bin, (long)(record * RECORD), SEEK_SET) != 0 || fwrite(&g_rec, 1, RECORD, g_bin) != RECORD) {
        LOG("row cache: could not write (SD card full?), not used any more in this run");
        fclose(g_bin); g_bin = nullptr; return;
    }
    fflush(g_bin);
    // the record first, then its line in the index: a record without one is simply written over later
    Entry e = {g_rec.id, g_rec.time};
    // at its own place, not "at the end": a cut-off entry left by an earlier run must not shift the ones after it
    FILE* idx = fopen(IDX, g_count ? "r+b" : "wb");
    if (!idx) return;
    bool ok = fseek(idx, (long)(record * sizeof(Entry)), SEEK_SET) == 0 && fwrite(&e, sizeof e, 1, idx) == 1;
    if (fclose(idx) != 0) ok = false;
    if (!ok) return;
    g_where[g_rec.id] = {record, g_rec.time};
    if (record == g_count) g_count++;
}

bool getStats(long plazaId, Stats& out) {
    load();
    if (plazaId <= 0 || !g_bin) return false;
    auto f = g_where.find((u32)plazaId);
    if (f == g_where.end()) return false;
    if (fseek(g_bin, (long)(f->second.record * RECORD), SEEK_SET) != 0 || fread(&g_rec, 1, RECORD, g_bin) != RECORD || g_rec.id != (u32)plazaId) return false;
    if (!g_rec.statsTime || (u32)time(nullptr) - g_rec.statsTime > STATS_AGE) return false;
    g_rec.desc[sizeof g_rec.desc - 1] = 0;
    out.downloads = g_rec.downloads; out.likes = g_rec.likes; out.bgm = g_rec.bgm != 0; out.desc = g_rec.desc;
    return true;
}

void putStats(long plazaId, const Stats& st) {
    load();
    if (plazaId <= 0 || !g_bin) return;
    auto f = g_where.find((u32)plazaId);
    if (f == g_where.end()) return;                // figures are only kept for rows that are kept
    long at = (long)(f->second.record * RECORD);
    if (fseek(g_bin, at, SEEK_SET) != 0 || fread(&g_rec, 1, RECORD, g_bin) != RECORD || g_rec.id != (u32)plazaId) return;
    g_rec.statsTime = (u32)time(nullptr); g_rec.downloads = st.downloads; g_rec.likes = st.likes; g_rec.bgm = st.bgm ? 1 : 0;
    if (!st.desc.empty()) copyText(g_rec.desc, sizeof g_rec.desc, st.desc);
    if (fseek(g_bin, at, SEEK_SET) == 0 && fwrite(&g_rec, 1, RECORD, g_bin) == RECORD) fflush(g_bin);
}

}  // namespace rowcache
