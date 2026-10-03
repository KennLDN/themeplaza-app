// Host test of the two SD-card caches (core/pvcache.cpp, core/rowcache.cpp). Run from an empty working
// directory in which "sdmc:/3ds/Theme Plaza/cache" exists; the caches then use files below it.
//   cache_test write     fills both caches
//   cache_test verify    a fresh process reads everything back and compares
// cache_check.py runs both, then damages the files and runs "verify-damaged", which must not crash and
// must simply report misses.
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include "pvcache.h"
#include "rowcache.h"

namespace logx { void write(const char* fmt, ...) { va_list ap; va_start(ap, fmt); vprintf(fmt, ap); va_end(ap); printf("\n"); } }

static const int PREVIEWS = 40, ROWS = 120;

static fmt::Image picture(int seed, int w, int h) {
    fmt::Image im; im.texW = 256; im.texH = 128; im.w = w; im.h = h; im.rgba = false;
    im.data.resize(256 * 128 * 2);
    for (size_t i = 0; i < im.data.size(); i++) im.data[i] = (u8)(i * 7 + seed * 31 + (i >> 9));
    return im;
}

static fmt::Smdh row(int i) {
    fmt::Smdh s;
    s.name = "Row " + std::to_string(i) + " \xe8\x8a\xad\xe4\xb9\x90"; s.author = "author" + std::to_string(i % 7); s.desc = std::string(i % 5 == 0 ? 600 : 20, 'd');
    s.hasIcon = true;
    for (size_t k = 0; k < sizeof s.icon; k++) s.icon[k] = (u8)(k + i);
    return s;
}

int main(int argc, char** argv) {
    std::string mode = argc > 1 ? argv[1] : "";
    int bad = 0, hits = 0;
    if (mode == "write") {
        for (int i = 0; i < PREVIEWS; i++) {
            pvcache::Colours c; c.bar = 0x1000 + i; c.tile = 0x2000 + i; c.glyph = 0x3000 + i; c.set = i % 3 != 0;
            fmt::Image top = picture(i, 206, 120), bottom = i % 4 == 0 ? fmt::Image() : picture(i + 1000, 160, 120);
            pvcache::put(pvcache::key(i % 2, 1000 + i, 0xABC00000u + i, i, i * 3), top, bottom, c);
        }
        // not cacheable: wrong texture size, badge kind, no contents
        fmt::Image odd = picture(1, 206, 120); odd.texW = 512; odd.data.resize(512 * 128 * 2);
        pvcache::put(0x12345678, odd, odd, {});
        if (pvcache::key(2, 1, 2, 3, 4) != 0 || pvcache::key(0, 5, 0, 5, 0) != 0) { printf("FAILED key for an item that must not be cached\n"); bad++; }
        for (int i = 0; i < ROWS; i++) rowcache::put(5000 + i, row(i));
        for (int i = 0; i < ROWS; i += 2) { rowcache::Stats st; st.downloads = 10 * i; st.likes = i; st.bgm = i % 4 == 0; st.desc = i % 6 == 0 ? "site text " + std::to_string(i) : ""; rowcache::putStats(5000 + i, st); }
        rowcache::put(5000 + 3, row(777));            // a row written again takes the same place
        printf("written\n");
        return bad;
    }
    bool damaged = mode == "verify-damaged";
    for (int i = 0; i < PREVIEWS; i++) {
        fmt::Image top, bottom; pvcache::Colours c;
        bool got = pvcache::get(pvcache::key(i % 2, 1000 + i, 0xABC00000u + i, i, i * 3), top, bottom, c);
        if (!got) { if (!damaged) { printf("FAILED preview %d missing\n", i); bad++; } continue; }
        hits++;
        fmt::Image wt = picture(i, 206, 120), wb = picture(i + 1000, 160, 120);
        bool ok = top.data == wt.data && top.w == 206 && top.h == 120 && (i % 4 == 0 ? !bottom.ok() : (bottom.data == wb.data && bottom.w == 160)) &&
                  c.set == (i % 3 != 0) && (!c.set || (c.bar == 0x1000u + i && c.tile == 0x2000u + i && c.glyph == 0x3000u + i));
        if (!ok) { printf("FAILED preview %d differs\n", i); bad++; }
    }
    { fmt::Image t, b; pvcache::Colours c; if (pvcache::get(0x12345678, t, b, c)) { printf("FAILED an uncacheable preview was kept\n"); bad++; } }
    for (int i = 0; i < ROWS; i++) {
        fmt::Smdh s; bool got = rowcache::get(5000 + i, s);
        if (!got) { if (!damaged) { printf("FAILED row %d missing\n", i); bad++; } continue; }
        hits++;
        fmt::Smdh w = row(i == 3 ? 777 : i);
        if (i % 6 == 0) w.desc = "site text " + std::to_string(i);       // the site's description replaced the SMDH's
        bool ok = s.name == w.name && s.author == w.author && s.hasIcon && !memcmp(s.icon, w.icon, sizeof s.icon) && w.desc.compare(0, s.desc.size(), s.desc) == 0 && s.desc.size() >= (w.desc.size() < 380 ? w.desc.size() : 380);
        rowcache::Stats st; bool hasStats = rowcache::getStats(5000 + i, st);
        if (i % 2 == 0 && i != 3) {
            if (!hasStats || st.downloads != (u32)(10 * i) || st.likes != (u32)i || st.bgm != (i % 4 == 0) || (i % 6 == 0 && st.desc != "site text " + std::to_string(i))) ok = damaged ? ok : false;
        } else if (hasStats) ok = false;
        if (!ok) { printf("FAILED row %d differs\n", i); bad++; }
    }
    { fmt::Smdh s; if (rowcache::get(424242, s)) { printf("FAILED a row that was never kept came back\n"); bad++; } }
    printf("%s: %d found, %d wrong\n", mode.c_str(), hits, bad);
    return bad ? 1 : 0;
}
