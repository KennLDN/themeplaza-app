#include "tex.h"
#include <cmath>
#include <cstring>

void Tex::free() {
    if (ok) { C3D_TexDelete(&tex); ok = false; }
}

namespace texutil {

namespace {
int pot(int v) { int p = 8; while (p < v) p <<= 1; return p; }
inline u32 morton(u32 x, u32 y) {
    static const u8 spread[8] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
    return spread[x] | (spread[y] << 1);
}
}  // namespace

bool fromRGBA(Tex& out, const u8* rgba, int w, int h) {
    out.free();
    int tw = pot(w), th = pot(h);
    if (tw > 1024 || th > 1024) return false;
    if (!C3D_TexInit(&out.tex, tw, th, GPU_RGBA8)) return false;
    C3D_TexSetFilter(&out.tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&out.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    memset(out.tex.data, 0, out.tex.size);
    u32* dst = (u32*)out.tex.data;
    const u32 tilesPerRow = tw >> 3;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const u8* p = rgba + 4 * (y * w + x);
            u32 idx = ((((u32)y >> 3) * tilesPerRow + ((u32)x >> 3)) << 6) | morton(x & 7, y & 7);
            dst[idx] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
    C3D_TexFlush(&out.tex);
    out.ok = true; out.w = w; out.h = h;
    return true;
}

bool fromRGB565(Tex& out, const u16* px, int w, int h) {
    out.free();
    int tw = pot(w), th = pot(h);
    if (tw > 1024 || th > 1024) return false;
    if (!C3D_TexInit(&out.tex, tw, th, GPU_RGB565)) return false;
    C3D_TexSetFilter(&out.tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&out.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    memset(out.tex.data, 0, out.tex.size);
    u16* dst = (u16*)out.tex.data;
    const u32 tilesPerRow = tw >> 3;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            dst[((((u32)y >> 3) * tilesPerRow + ((u32)x >> 3)) << 6) | morton(x & 7, y & 7)] = px[y * w + x];
    C3D_TexFlush(&out.tex);
    out.ok = true; out.w = w; out.h = h;
    return true;
}

bool fromTiled(Tex& out, const u8* tiled, int texW, int texH, GPU_TEXCOLOR fmt, int w, int h) {
    out.free();
    if (!C3D_TexInit(&out.tex, texW, texH, fmt)) return false;
    C3D_TexSetFilter(&out.tex, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&out.tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    memcpy(out.tex.data, tiled, (size_t)texW * texH * (fmt == GPU_RGBA8 ? 4 : 2));
    C3D_TexFlush(&out.tex);
    out.ok = true; out.w = w; out.h = h;
    return true;
}

void roundCorners(u8* rgba, int w, int h, float r, int corners) {
    int n = (int)ceilf(r);
    for (int y = 0; y < n; y++)
        for (int x = 0; x < n; x++) {
            // coverage of the pixel centre against a circle of radius r centred at (r, r), 4x4 supersampled
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float dx = r - (x + (sx + 0.5f) / 4), dy = r - (y + (sy + 0.5f) / 4);
                    if (dx * dx + dy * dy <= r * r) hit++;
                }
            if (hit == 16) continue;
            auto apply = [&](int px, int py) { u8* a = rgba + 4 * (py * w + px) + 3; *a = (u8)(*a * hit / 16); };
            if (corners & 1) apply(x, y);
            if (corners & 2) apply(w - 1 - x, y);
            if (corners & 4) apply(x, h - 1 - y);
            if (corners & 8) apply(w - 1 - x, h - 1 - y);
        }
}

}  // namespace texutil
