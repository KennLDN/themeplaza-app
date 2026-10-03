// Runtime textures: converting decoded pixels into the GPU's tiled layout.
#pragma once
#include <3ds.h>
#include <citro3d.h>

struct Tex {
    C3D_Tex tex{};
    bool ok = false;
    int w = 0, h = 0;      // size of the picture inside the (power-of-two) texture
    void free();
};

namespace texutil {

// rgba: w*h pixels, bytes R,G,B,A, top row first. Creates a GPU_RGBA8 texture.
bool fromRGBA(Tex& out, const u8* rgba, int w, int h);
// rgb565: w*h native-endian pixels, top row first. Creates a GPU_RGB565 texture.
bool fromRGB565(Tex& out, const u16* px, int w, int h);
// A picture already in the GPU's 8x8 tiled order (theme wallpapers, SMDH icons): fmt GPU_RGB565 or
// GPU_RGBA8, data texW*texH pixels. Main thread only.
bool fromTiled(Tex& out, const u8* tiled, int texW, int texH, GPU_TEXCOLOR fmt, int w, int h);

// Multiplies alpha by rounded-corner coverage. corners bit mask: 1 = top-left, 2 = top-right, 4 = bottom-left, 8 = bottom-right.
void roundCorners(u8* rgba, int w, int h, float radius, int corners = 15);

}  // namespace texutil
