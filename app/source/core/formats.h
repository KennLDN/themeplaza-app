// Readers for the file formats the app meets: SMDH (name, author, icon), the theme body
// (wallpapers and colours), Luma splash images, and PNG. Everything here is plain CPU work on
// byte buffers and is safe to call from a worker thread.
#pragma once
#include <3ds.h>
#include <string>
#include <vector>

namespace fmt {

// ---------- pixels ----------
// GPU textures store pixels in 8x8 tiles, Z-order inside each tile. Index of pixel (x, y) in a texture `texW` wide.
inline u32 tiledIndex(u32 x, u32 y, u32 texW) {
    static const u8 spread[8] = {0x00, 0x01, 0x04, 0x05, 0x10, 0x11, 0x14, 0x15};
    return ((((y >> 3) * (texW >> 3)) + (x >> 3)) << 6) | spread[x & 7] | (spread[y & 7] << 1);
}
inline u16 to565(u8 r, u8 g, u8 b) { return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3); }

// A picture ready to become a texture: already tiled, power-of-two size.
struct Image {
    std::vector<u8> data;
    int texW = 0, texH = 0;      // texture size
    int w = 0, h = 0;            // picture size inside it
    bool rgba = false;           // false: RGB565 (2 bytes per pixel); true: RGBA8 (4 bytes, stored A,B,G,R)
    bool ok() const { return !data.empty(); }
};

// Half-size copy of an RGB565 picture (each pixel the average of four), for showing it at half scale.
void halve(const Image& src, Image& dst);

// ---------- SMDH ----------
constexpr size_t SMDH_SIZE = 0x36C0, SMDH_ICON_OFFSET = 0x24C0, SMDH_ICON_SIZE = 0x1200;
struct Smdh {
    std::string name, desc, author;
    bool hasIcon = false;
    alignas(4) u8 icon[SMDH_ICON_SIZE];     // 48x48 RGB565, tiled (6x6 tiles); aligned because it is read as 16-bit pixels
};
bool parseSmdh(const u8* data, size_t n, Smdh& out);

// Icon as the UI draws it: `size` 48 (grid tile, all corners rounded) or 24 (list row, left corners rounded).
// src: 48x48 RGB565 tiled. Result: RGBA8 texture data, 64x64 or 32x32.
void makeIcon(const u8* icon565, int size, Image& out);
// Shrinks any RGB565 picture (tiled, inside a texW-wide texture) to a 48x48 tiled icon, cropping to a square.
void iconFromTiled(const u8* tiled, int texW, int w, int h, u8* icon565);
// Same from straight RGBA rows.
void iconFromRGBA(const u8* rgba, int w, int h, u8* icon565);

// ---------- LZ11 ----------
// Decompresses a whole LZ11 stream (type byte 0x11). maxOut guards against bad headers.
bool lz11(const u8* in, size_t n, std::vector<u8>& out, size_t maxOut);

// Unpacks only the first `want` bytes (the start of a stream is enough for that). Returns how many were produced.
size_t lz11Head(const u8* in, size_t n, u8* out, size_t want);
// Compresses to the same format. Not the tightest packer, but fast and well within what the HOME Menu accepts.
void lz11Compress(const u8* in, size_t n, std::vector<u8>& out);

// ---------- theme body ----------
struct ThemeLook {
    Image top, bottom;                 // wallpapers, 512x256 RGB565; picture 412x240 and 320x240
    u32 colBar = 0, colTile = 0, colGlyph = 0;   // ABGR, for the fake HOME Menu drawn over the wallpapers
    bool bgmFlag = false;
};
// body: the compressed body_LZ.bin.
bool decodeTheme(const u8* body, size_t n, ThemeLook& out);
// The same from the already unpacked body.
bool parseTheme(const std::vector<u8>& plain, ThemeLook& out);
// Picks overlay colours from a wallpaper's average colour.
void coloursFromImage(const Image& img, ThemeLook& out);

// ---------- Luma splash ----------
// raw: 3 bytes per pixel B,G,R, column by column, each column bottom to top. w = 400 or 320.
bool decodeSplash(const u8* raw, size_t n, int w, Image& out);

// ---------- PNG ----------
struct Png {
    int w = 0, h = 0;
    std::vector<u8> rgba;              // w*h*4, rows top to bottom
};
// Reads only the size from the first bytes of a PNG (needs 24 bytes).
bool pngSize(const u8* data, size_t n, int& w, int& h);
// Decodes PNGs of any colour type and bit depth, interlaced or not. maxPixels guards memory.
bool decodePng(const u8* data, size_t n, Png& out, size_t maxPixels = 1024 * 1024);
// Converts part of a decoded PNG into an RGB565 tiled picture inside a power-of-two texture.
void pngTo565(const Png& png, int sx, int sy, int w, int h, Image& out);
// The same keeping transparency: an RGBA8 tiled picture.
void pngToRGBA(const Png& png, int sx, int sy, int w, int h, Image& out);
// True if every pixel of that part of the PNG is fully transparent.
bool pngEmpty(const Png& png, int sx, int sy, int w, int h);

}  // namespace fmt
