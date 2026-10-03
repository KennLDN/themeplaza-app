// Runs the app's format readers on the PC against real files, and writes what they produce as
// raw RGBA dumps that tools/hosttest/run.py turns into PNGs for inspection.
//   hosttest OUTDIR FILE...     each FILE is a theme / splash / badge zip or folder
#include <cstdio>
#include <cstring>
#include <string>
#include "../../app/source/core/formats.h"
#include "../../app/source/core/pack.h"

static std::string g_out;

static void dump(const std::string& name, const fmt::Image& img) {
    if (!img.ok()) { printf("    %s: (none)\n", name.c_str()); return; }
    std::vector<u8> rgba((size_t)img.w * img.h * 4);
    for (int y = 0; y < img.h; y++)
        for (int x = 0; x < img.w; x++) {
            u8* o = &rgba[4 * ((size_t)y * img.w + x)];
            u32 i = fmt::tiledIndex(x, y, img.texW);
            if (img.rgba) { u32 v; memcpy(&v, &img.data[4 * i], 4); o[0] = v >> 24; o[1] = v >> 16; o[2] = v >> 8; o[3] = v; }
            else { u16 v; memcpy(&v, &img.data[2 * i], 2); o[0] = (v >> 11) << 3; o[1] = ((v >> 5) & 63) << 2; o[2] = (v & 31) << 3; o[3] = 255; }
        }
    char path[512]; snprintf(path, sizeof path, "%s/%s.%dx%d.rgba", g_out.c_str(), name.c_str(), img.w, img.h);
    FILE* f = fopen(path, "wb"); fwrite(rgba.data(), 1, rgba.size(), f); fclose(f);
    printf("    %s: %dx%d\n", name.c_str(), img.w, img.h);
}

static void swatch(const std::string& name, u32 a, u32 b, u32 c) {
    fmt::Image img; img.texW = 64; img.texH = 16; img.w = 48; img.h = 16; img.rgba = true; img.data.resize(64 * 16 * 4);
    u32 cols[3] = {a, b, c};
    for (int y = 0; y < 16; y++) for (int x = 0; x < 48; x++) {
        u32 v = cols[x / 16], out = ((v & 0xff) << 24) | (((v >> 8) & 0xff) << 16) | (((v >> 16) & 0xff) << 8) | 0xff;
        memcpy(&img.data[4 * fmt::tiledIndex(x, y, 64)], &out, 4);
    }
    dump(name, img);
}

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    g_out = argv[1];
    for (int a = 2; a < argc; a++) {
        std::string path = argv[a], tag = "f" + std::to_string(a - 2);
        Pack pk;
        if (!pk.open(path)) { printf("%s: cannot open\n", path.c_str()); continue; }
        printf("%s (%s, %zu entries)\n", path.c_str(), pk.isZip() ? "zip" : "folder", pk.entries().size());
        std::vector<u8> buf;
        if (auto* e = pk.find("info.smdh")) {
            fmt::Smdh s;
            if (pk.read(*e, buf, 0x4000) && fmt::parseSmdh(buf.data(), buf.size(), s)) {
                printf("    smdh: name='%s' author='%s' desc='%s' icon=%d\n", s.name.c_str(), s.author.c_str(), s.desc.c_str(), s.hasIcon);
                fmt::Image i48, i24; fmt::makeIcon(s.icon, 48, i48); fmt::makeIcon(s.icon, 24, i24);
                dump(tag + "_icon48", i48); dump(tag + "_icon24", i24);
            } else printf("    smdh: failed\n");
        }
        if (auto* e = pk.find("body_LZ.bin")) {
            fmt::ThemeLook look;
            bool ok = pk.read(*e, buf, 0x150000) && fmt::decodeTheme(buf.data(), buf.size(), look);
            {
                std::vector<u8> plain, packed, again;
                bool rt = fmt::lz11(buf.data(), buf.size(), plain, 0x150000);
                if (rt) { fmt::lz11Compress(plain.data(), plain.size(), packed); rt = fmt::lz11(packed.data(), packed.size(), again, 0x150000) && again == plain; }
                printf("    lz11 round trip: %s (%zu unpacked, original %zu packed, ours %zu)\n", rt ? "ok" : "FAILED", plain.size(), buf.size(), packed.size());
            }
            { u8 head[8]; size_t got = fmt::lz11Head(buf.data(), buf.size() < 64 ? buf.size() : 64, head, 6); printf("    flag from the first 64 bytes: %s\n", got == 6 ? (head[5] ? "on" : "off") : "unreadable"); }
            printf("    body: %s size=%u crc=%08x calc=%08x bgmFlag=%d bgmFile=%d\n", ok ? "ok" : "FAILED", e->size, e->crc, crc32Of(buf.data(), buf.size()), look.bgmFlag, pk.find("bgm.bcstm") != nullptr);
            if (ok) {
                dump(tag + "_top", look.top); dump(tag + "_bottom", look.bottom); swatch(tag + "_colours", look.colBar, look.colTile, look.colGlyph);
                alignas(4) u8 icon[fmt::SMDH_ICON_SIZE]; fmt::iconFromTiled(look.top.data.data(), look.top.texW, look.top.w, look.top.h, icon);
                fmt::Image gen; fmt::makeIcon(icon, 48, gen); dump(tag + "_icon_from_wall", gen);
            }
        }
        if (auto* e = pk.find("splash.bin")) { fmt::Image img; bool ok = pk.read(*e, buf, 300000) && fmt::decodeSplash(buf.data(), buf.size(), 400, img); printf("    splash top: %s\n", ok ? "ok" : "FAILED"); dump(tag + "_splash_top", img); }
        if (auto* e = pk.find("splashbottom.bin")) { fmt::Image img; bool ok = pk.read(*e, buf, 300000) && fmt::decodeSplash(buf.data(), buf.size(), 320, img); printf("    splash bottom: %s\n", ok ? "ok" : "FAILED"); dump(tag + "_splash_bottom", img); }
        int pngs = 0, bad = 0;
        for (auto& e : pk.entries()) {
            size_t L = e.name.size();
            if (L < 4 || strcasecmp(e.name.c_str() + L - 4, ".png") != 0) continue;
            fmt::Png png;
            if (!pk.read(e, buf, 4 * 1024 * 1024) || !fmt::decodePng(buf.data(), buf.size(), png)) { bad++; printf("    png FAILED: %s\n", e.name.c_str()); continue; }
            if (pngs < 3) {
                fmt::Image img; img.texW = 8; while (img.texW < png.w) img.texW <<= 1; img.texH = 8; while (img.texH < png.h) img.texH <<= 1;
                img.w = png.w; img.h = png.h; img.rgba = true; img.data.resize((size_t)img.texW * img.texH * 4);
                for (int y = 0; y < png.h; y++) for (int x = 0; x < png.w; x++) { const u8* p = &png.rgba[4 * ((size_t)y * png.w + x)]; u32 v = (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; memcpy(&img.data[4 * fmt::tiledIndex(x, y, img.texW)], &v, 4); }
                dump(tag + "_png" + std::to_string(pngs), img);
            }
            pngs++;
        }
        if (pngs || bad) printf("    pngs: %d decoded, %d failed\n", pngs, bad);
    }
    return 0;
}
