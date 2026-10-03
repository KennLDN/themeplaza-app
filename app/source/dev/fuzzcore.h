// The damaged-file test shared by the PC run (tools/hosttest/fuzz.cpp, under the address sanitizer, 64-bit)
// and the run on the console itself (the test channel's "fuzz N", 32-bit ARM, where a size that wraps around
// behaves differently). The includer defines FUZZ_LOG(format, ...).
#pragma once
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <strings.h>
#include <sys/stat.h>
#include <vector>
#include "../core/bcstm.h"
#include "../core/formats.h"
#include "../core/pack.h"

namespace fuzzcore {

inline u32 g_seed = 1;
inline u32 rnd() { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }

inline std::vector<u8> load(const char* path) {
    std::vector<u8> d;
    if (FILE* f = fopen(path, "rb")) { u8 buf[65536]; size_t n; while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n); fclose(f); }
    return d;
}
inline void save(const std::string& path, const std::vector<u8>& d) { FILE* f = fopen(path.c_str(), "wb"); fwrite(d.data(), 1, d.size(), f); fclose(f); }

// A damaged copy: a few bytes changed (more often near the start, where the headers are), sometimes cut short.
inline std::vector<u8> mutate(const std::vector<u8>& src) {
    std::vector<u8> d = src;
    if (d.empty()) return d;
    int edits = 1 + rnd() % 6;
    for (int i = 0; i < edits; i++) {
        size_t span = (rnd() % 3) ? (d.size() < 512 ? d.size() : 512) : d.size();
        size_t at = rnd() % span;
        switch (rnd() % 4) {
            case 0: d[at] = (u8)rnd(); break;
            case 1: d[at] = 0xff; break;
            case 2: d[at] = 0; break;
            default: if (at + 4 <= d.size()) { u32 v = (rnd() % 2) ? 0xffffffffu - rnd() % 64 : rnd() % 64; memcpy(&d[at], &v, 4); } break;
        }
    }
    if (rnd() % 10 == 0) d.resize(rnd() % d.size());
    return d;
}

inline bool ends(const std::string& s, const char* e) { size_t n = strlen(e); return s.size() >= n && !strcasecmp(s.c_str() + s.size() - n, e); }

// Reads `iterations` damaged copies of each file (.png .smdh .bcstm .zip, or a body_LZ.bin). work: a folder
// for the copies that have to be files. Returns how many were read, or -1 if a round trip gave wrong data.
inline long run(int iterations, const std::string& work, const std::vector<std::string>& files, u32 seed = 1) {
    g_seed = seed;
    mkdir(work.c_str(), 0777); mkdir((work + "/theme").c_str(), 0777);
    long runs = 0;
    for (const std::string& path : files) {
        std::vector<u8> orig = load(path.c_str());
        if (orig.empty()) { FUZZ_LOG("skip %s", path.c_str()); continue; }
        // for a theme body, also damage the unpacked data: that is where its header and offsets are
        std::vector<u8> plain;
        bool body = path.find("body_LZ") != std::string::npos || path.find("body_lz") != std::string::npos;
        if (body) fmt::lz11(orig.data(), orig.size(), plain, 0x150000);
        for (int it = 0; it < iterations; it++) {
            std::vector<u8> d = mutate(orig);
            runs++;
            if (ends(path, ".png")) {
                fmt::Png png;
                if (fmt::decodePng(d.data(), d.size(), png)) {
                    fmt::Image img; fmt::pngTo565(png, (int)(rnd() % 64) - 16, 0, 400, 240, img);
                    fmt::pngToRGBA(png, 0, 0, 64, 64, img);
                    alignas(4) u8 icon[fmt::SMDH_ICON_SIZE]; if (png.w > 0 && png.h > 0) fmt::iconFromRGBA(png.rgba.data(), png.w, png.h, icon);
                }
            } else if (ends(path, ".smdh")) {
                fmt::Smdh s;
                if (fmt::parseSmdh(d.data(), d.size(), s)) { fmt::Image i; fmt::makeIcon(s.icon, 48, i); fmt::makeIcon(s.icon, 24, i); }
            } else if (body) {
                fmt::ThemeLook look;
                fmt::decodeTheme(d.data(), d.size(), look);
                u8 head[16]; fmt::lz11Head(d.data(), d.size() < 64 ? d.size() : 64, head, 6 + rnd() % 10);
                if (!plain.empty() && it % 16 == 0) {   // packing damaged data and unpacking it must give the same bytes back
                    std::vector<u8> p = mutate(plain), packed, again;
                    fmt::lz11Compress(p.data(), p.size(), packed);
                    if (!fmt::lz11(packed.data(), packed.size(), again, 0x400000) || again != p) { FUZZ_LOG("lz11 round trip FAILED"); return -1; }
                }
                if (!plain.empty()) { fmt::ThemeLook l2; std::vector<u8> p = mutate(plain); if (fmt::parseTheme(p, l2)) { fmt::Image h; fmt::halve(l2.top, h); } }
            } else if (ends(path, ".bcstm")) {
                save(work + "/theme/bgm.bcstm", d);
                Bcstm b;
                if (b.open(work + "/theme")) {
                    std::vector<s16> out(4096 * 2);
                    for (int k = 0; k < 40 && b.read(out.data(), 4096); k++) {}
                    b.rewind();
                    b.read(out.data(), 4096);
                }
            } else if (ends(path, ".zip")) {
                save(work + "/t.zip", d);
                Pack pk;
                if (pk.open(work + "/t.zip")) {
                    std::vector<u8> buf; int n = 0;
                    for (auto& e : pk.entries()) { if (n++ > 12) break; pk.read(e, buf, 4 * 1024 * 1024); u8 head[24]; pk.readHead(e, head, 24); }
                    pk.find("body_LZ.bin");
                    EntryStream es;
                    if (es.open(work + "/t.zip", "info.smdh")) { u8 tmp[4096]; while (es.read(tmp, sizeof tmp) == sizeof tmp) {} }
                }
            }
        }
        FUZZ_LOG("ok %s", path.c_str());
    }
    return runs;
}

}  // namespace fuzzcore
