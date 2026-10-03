#include "pvcache.h"
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unordered_map>
#include "log.h"
#include "appdir.h"

namespace pvcache {

namespace {

const char* const BIN = APP_DIR "/cache/previews.bin";
const char* const IDX = APP_DIR "/cache/previews.idx";
constexpr int TEX_W = 256, TEX_H = 128;                      // the size every cached picture's texture has
constexpr size_t PIC = (size_t)TEX_W * TEX_H * 2, HEAD = 64, RECORD = HEAD + 2 * PIC;
constexpr u32 MAGIC = 0x31565041;                            // "APV1"
constexpr u32 MAX_RECORDS = 1500;                            // about 200 MB; past that the cache starts again

struct Head {
    u32 magic, key, flags;                 // flags: 1 top, 2 bottom, 4 colours
    u16 topW, topH, botW, botH;
    u32 bar, tile, glyph;
    u8 pad[HEAD - 32];
};
static_assert(sizeof(Head) == HEAD, "record header size");

bool g_loaded = false;
std::unordered_map<u32, u32> g_where;      // key -> record number
u32 g_count = 0;
FILE* g_bin = nullptr;
u8 g_rec[RECORD];                          // one record, for reading and for writing

void load() {
    if (g_loaded) return;
    g_loaded = true;
    mkdir(APP_DIR "/cache", 0777);
    if (FILE* f = fopen(IDX, "rb")) {
        u32 keys[256]; size_t n;
        while ((n = fread(keys, 4, 256, f)) > 0)
            for (size_t i = 0; i < n; i++) g_where[keys[i]] = g_count++;
        fclose(f);
    }
    g_bin = fopen(BIN, "r+b");
    if (!g_bin) { g_bin = fopen(BIN, "w+b"); g_where.clear(); g_count = 0; remove(IDX); }
    if (g_bin) setvbuf(g_bin, nullptr, _IONBF, 0);           // whole records are read and written in one go
}

void startAgain() {
    if (g_bin) fclose(g_bin);
    g_bin = fopen(BIN, "w+b");
    if (g_bin) setvbuf(g_bin, nullptr, _IONBF, 0);
    remove(IDX);
    g_where.clear(); g_count = 0;
}

bool cacheable(const fmt::Image& im) { return !im.ok() || (!im.rgba && im.texW == TEX_W && im.texH == TEX_H && im.data.size() == PIC); }

}  // namespace

u32 key(int kind, u32 bodySize, u32 bodyCrc, u32 auxSize, u32 auxCrc) {
    if (kind > 1 || (!bodyCrc && !auxCrc)) return 0;         // themes and splashes whose contents are known
    u32 k = bodyCrc ^ (bodySize * 2654435761u) ^ ((auxCrc << 13) | (auxCrc >> 19)) ^ (auxSize * 40503u) ^ (u32)kind;
    return k ? k : 1;
}

u32 plazaKey(int kind, long plazaId) {
    if (kind > 1 || plazaId <= 0) return 0;
    u32 k = ((u32)plazaId * 2654435761u) ^ (0xA5A50000u + (u32)kind);
    return k ? k : 1;
}

bool get(u32 k, fmt::Image& top, fmt::Image& bottom, Colours& col) {
    load();
    if (!k || !g_bin) return false;
    auto f = g_where.find(k);
    if (f == g_where.end()) return false;
    u8* rec = g_rec;
    if (fseek(g_bin, (long)(f->second * RECORD), SEEK_SET) != 0 || fread(rec, 1, RECORD, g_bin) != RECORD) { g_where.erase(f); return false; }
    Head h; memcpy(&h, rec, sizeof h);
    if (h.magic != MAGIC || h.key != k || !(h.flags & 3)) { g_where.erase(f); return false; }
    auto take = [](fmt::Image& im, const u8* data, int w, int ht) {
        im.texW = TEX_W; im.texH = TEX_H; im.w = w; im.h = ht; im.rgba = false;
        im.data.assign(data, data + PIC);
    };
    if ((h.flags & 1) && h.topW <= TEX_W && h.topH <= TEX_H) take(top, rec + HEAD, h.topW, h.topH);
    if ((h.flags & 2) && h.botW <= TEX_W && h.botH <= TEX_H) take(bottom, rec + HEAD + PIC, h.botW, h.botH);
    if (h.flags & 4) { col.bar = h.bar; col.tile = h.tile; col.glyph = h.glyph; col.set = true; }
    if (!top.ok() && !bottom.ok()) { g_where.erase(k); return false; }
    return true;
}

void put(u32 k, const fmt::Image& top, const fmt::Image& bottom, const Colours& col) {
    load();
    if (!k || !g_bin || (!top.ok() && !bottom.ok()) || !cacheable(top) || !cacheable(bottom)) return;
    if (g_where.count(k)) return;
    if (g_count >= MAX_RECORDS) startAgain();
    if (!g_bin) return;
    u8* rec = g_rec;
    Head h{};
    h.magic = MAGIC; h.key = k;
    h.flags = (top.ok() ? 1 : 0) | (bottom.ok() ? 2 : 0) | (col.set ? 4 : 0);
    h.topW = (u16)top.w; h.topH = (u16)top.h; h.botW = (u16)bottom.w; h.botH = (u16)bottom.h;
    h.bar = col.bar; h.tile = col.tile; h.glyph = col.glyph;
    memcpy(rec, &h, sizeof h);
    if (top.ok()) memcpy(rec + HEAD, top.data.data(), PIC); else memset(rec + HEAD, 0, PIC);
    if (bottom.ok()) memcpy(rec + HEAD + PIC, bottom.data.data(), PIC); else memset(rec + HEAD + PIC, 0, PIC);
    // the record first, then its key: a record without a key is simply written over by the next one
    if (fseek(g_bin, (long)(g_count * RECORD), SEEK_SET) != 0 || fwrite(rec, 1, RECORD, g_bin) != RECORD) {
        // most likely a full card: stop using the cache for this run rather than try (and log) on every preview
        LOG("preview cache: could not write (SD card full?), not used any more in this run");
        fclose(g_bin); g_bin = nullptr; return;
    }
    fflush(g_bin);
    // at its own place, not "at the end": a cut-off entry left by an earlier run must not shift the ones after it
    FILE* f = fopen(IDX, g_count ? "r+b" : "wb");
    if (!f) return;
    bool ok = fseek(f, (long)(g_count * 4), SEEK_SET) == 0 && fwrite(&k, 4, 1, f) == 1;
    if (fclose(f) != 0) ok = false;
    if (ok) g_where[k] = g_count++;
}

}  // namespace pvcache
