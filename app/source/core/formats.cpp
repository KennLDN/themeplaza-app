#include "formats.h"
#include <cmath>
#include <cstring>
#include <zlib.h>
#include "tex.h"

namespace fmt {

namespace {

inline u16 rd16(const u8* p) { return p[0] | (p[1] << 8); }
inline u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
inline u32 be32(const u8* p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

inline void from565(u16 v, u8* rgb) {
    u8 r = v >> 11, g = (v >> 5) & 0x3f, b = v & 0x1f;
    rgb[0] = (r << 3) | (r >> 2); rgb[1] = (g << 2) | (g >> 4); rgb[2] = (b << 3) | (b >> 2);
}

void utf16Field(const u8* p, size_t units, std::string& out) {
    u16 buf[130]; size_t n = 0;
    while (n < units && n < 129) { u16 c = rd16(p + 2 * n); if (!c) break; buf[n++] = c; }
    buf[n] = 0;
    u8 utf8[130 * 3 + 1];
    ssize_t len = utf16_to_utf8(utf8, buf, sizeof utf8 - 1);
    if (len < 0) len = 0;
    utf8[len] = 0;
    out.assign((const char*)utf8);
    while (!out.empty() && (out.back() == ' ' || out.back() == '\n' || out.back() == '\r')) out.pop_back();
}

void hsv(float h, float s, float v, u8* rgb) {
    float c = v * s, x = c * (1 - fabsf(fmodf(h / 60.0f, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
    if (h < 60) { r = c; g = x; } else if (h < 120) { r = x; g = c; } else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; } else if (h < 300) { r = x; b = c; } else { r = c; b = x; }
    rgb[0] = (u8)((r + m) * 255); rgb[1] = (u8)((g + m) * 255); rgb[2] = (u8)((b + m) * 255);
}

void toHsv(const u8* rgb, float& h, float& s, float& v) {
    float r = rgb[0] / 255.0f, g = rgb[1] / 255.0f, b = rgb[2] / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
    v = mx; s = mx > 0 ? d / mx : 0;
    if (d < 1e-5f) h = 0;
    else if (mx == r) h = 60 * fmodf((g - b) / d + 6, 6);
    else if (mx == g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
}

inline u32 abgr(const u8* rgb) { return 0xff000000 | (rgb[2] << 16) | (rgb[1] << 8) | rgb[0]; }

void solid565(Image& img, int w, int h, u8 r, u8 g, u8 b) {
    img.texW = 512; img.texH = 256; img.w = w; img.h = h; img.rgba = false;
    img.data.resize(512 * 256 * 2);
    u16 v = to565(r, g, b); u16* p = (u16*)img.data.data();
    for (size_t i = 0; i < 512 * 256; i++) p[i] = v;
}

// Copies a wallpaper out of the theme body. Wide (1024) textures are cut to their left 512 columns:
// tiles are stored row by row, so that is the first 64 of every 128 tiles.
bool wallpaper(const std::vector<u8>& d, u32 off, bool wide, int w, int h, Image& img) {
    size_t need = wide ? 0x80000 : 0x40000;
    if (off == 0 || off > d.size() || need > d.size() - off) return false;
    img.texW = 512; img.texH = 256; img.w = w; img.h = h; img.rgba = false;
    img.data.resize(0x40000);
    if (!wide) memcpy(img.data.data(), &d[off], 0x40000);
    else for (int row = 0; row < 32; row++) memcpy(&img.data[row * 8192], &d[off + row * 16384], 8192);
    return true;
}

}  // namespace

void halve(const Image& src, Image& dst) {
    if (!src.ok() || src.rgba) { dst = Image(); return; }
    int w = src.w / 2, h = src.h / 2, tw = 8, th = 8;
    while (tw < w) tw <<= 1;
    while (th < h) th <<= 1;
    dst.texW = tw; dst.texH = th; dst.w = w; dst.h = h; dst.rgba = false;
    dst.data.assign((size_t)tw * th * 2, 0);
    const u16* s = (const u16*)src.data.data(); u16* d = (u16*)dst.data.data();
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            u32 r = 0, g = 0, b = 0;
            for (int k = 0; k < 4; k++) {
                u16 v = s[tiledIndex(2 * x + (k & 1), 2 * y + (k >> 1), src.texW)];
                r += v >> 11; g += (v >> 5) & 0x3f; b += v & 0x1f;
            }
            d[tiledIndex(x, y, tw)] = (u16)((((r + 2) >> 2) << 11) | (((g + 2) >> 2) << 5) | ((b + 2) >> 2));
        }
}

// ---------- SMDH ----------
bool parseSmdh(const u8* d, size_t n, Smdh& out) {
    if (n < SMDH_SIZE || memcmp(d, "SMDH", 4) != 0) return false;
    // titles: 16 languages of 0x200 bytes from 0x8. Prefer English, then whichever is filled in.
    static const int order[] = {1, 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    for (int lang : order) {
        const u8* t = d + 8 + lang * 0x200;
        utf16Field(t, 64, out.name);
        if (out.name.empty()) continue;
        utf16Field(t + 0x80, 128, out.desc);
        utf16Field(t + 0x180, 64, out.author);
        break;
    }
    memcpy(out.icon, d + SMDH_ICON_OFFSET, SMDH_ICON_SIZE);
    out.hasIcon = false;
    for (size_t i = 0; i < SMDH_ICON_SIZE; i++) if (out.icon[i]) { out.hasIcon = true; break; }
    return true;
}

void makeIcon(const u8* icon565, int size, Image& out) {
    static const int R48 = 8, R24 = 6;
    u8 px[48 * 48 * 4];
    const u16* src = (const u16*)icon565;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++) {
            u8* p = px + 4 * (y * 48 + x);
            from565(src[tiledIndex(x, y, 48)], p);
            p[3] = 255;
        }
    int tex = size == 48 ? 64 : 32;
    if (size != 48) {
        for (int y = 0; y < 24; y++)
            for (int x = 0; x < 24; x++)
                for (int c = 0; c < 4; c++)
                    px[4 * (y * 24 + x) + c] = (px[4 * (2 * y * 48 + 2 * x) + c] + px[4 * (2 * y * 48 + 2 * x + 1) + c] +
                                                px[4 * ((2 * y + 1) * 48 + 2 * x) + c] + px[4 * ((2 * y + 1) * 48 + 2 * x + 1) + c] + 2) / 4;
        texutil::roundCorners(px, 24, 24, R24, 1 | 4);
    } else texutil::roundCorners(px, 48, 48, R48);
    out.texW = out.texH = tex; out.w = out.h = size; out.rgba = true;
    out.data.assign((size_t)tex * tex * 4, 0);
    u32* dst = (u32*)out.data.data();
    for (int y = 0; y < size; y++)
        for (int x = 0; x < size; x++) {
            const u8* p = px + 4 * (y * size + x);
            dst[tiledIndex(x, y, tex)] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
}

void iconFromTiled(const u8* tiled, int texW, int w, int h, u8* icon565) {
    const u16* src = (const u16*)tiled; u16* dst = (u16*)icon565;
    int side = w < h ? w : h, ox = (w - side) / 2, oy = (h - side) / 2;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++) {
            // average a 3x3 sample grid inside the source block
            int r = 0, g = 0, b = 0;
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    int px = ox + ((x * 3 + sx) * side) / 144, py = oy + ((y * 3 + sy) * side) / 144;
                    u8 c[3]; from565(src[tiledIndex(px, py, texW)], c);
                    r += c[0]; g += c[1]; b += c[2];
                }
            dst[tiledIndex(x, y, 48)] = to565(r / 9, g / 9, b / 9);
        }
}

void iconFromRGBA(const u8* rgba, int w, int h, u8* icon565) {
    u16* dst = (u16*)icon565;
    int side = w < h ? w : h, ox = (w - side) / 2, oy = (h - side) / 2;
    for (int y = 0; y < 48; y++)
        for (int x = 0; x < 48; x++) {
            int r = 0, g = 0, b = 0;
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    const u8* p = rgba + 4 * ((oy + ((y * 3 + sy) * side) / 144) * w + ox + ((x * 3 + sx) * side) / 144);
                    // transparent pixels sit on the white tile
                    r += (p[0] * p[3] + 255 * (255 - p[3])) / 255; g += (p[1] * p[3] + 255 * (255 - p[3])) / 255; b += (p[2] * p[3] + 255 * (255 - p[3])) / 255;
                }
            dst[tiledIndex(x, y, 48)] = to565(r / 9, g / 9, b / 9);
        }
}

// ---------- LZ11 ----------
bool lz11(const u8* in, size_t n, std::vector<u8>& out, size_t maxOut) {
    if (n < 4 || in[0] != 0x11) return false;
    size_t size = in[1] | (in[2] << 8) | (in[3] << 16), p = 4;
    if (size == 0) { if (n < 8) return false; size = rd32(in + 4); p = 8; }
    if (size > maxOut) return false;
    out.resize(size);
    u8* dst = out.data();
    size_t o = 0;
    while (o < size) {
        if (p >= n) return false;
        u8 flags = in[p++];
        for (int bit = 0; bit < 8 && o < size; bit++, flags <<= 1) {
            if (!(flags & 0x80)) {
                if (p >= n) return false;
                dst[o++] = in[p++];
                continue;
            }
            if (p + 1 >= n) return false;
            u32 b0 = in[p], len, disp;
            switch (b0 >> 4) {
                case 0:
                    if (p + 2 >= n) return false;
                    len = (((b0 & 0xf) << 4) | (in[p + 1] >> 4)) + 0x11;
                    disp = (((in[p + 1] & 0xf) << 8) | in[p + 2]) + 1; p += 3; break;
                case 1:
                    if (p + 3 >= n) return false;
                    len = (((b0 & 0xf) << 12) | (in[p + 1] << 4) | (in[p + 2] >> 4)) + 0x111;
                    disp = (((in[p + 2] & 0xf) << 8) | in[p + 3]) + 1; p += 4; break;
                default:
                    len = (b0 >> 4) + 1;
                    disp = (((b0 & 0xf) << 8) | in[p + 1]) + 1; p += 2; break;
            }
            if (disp > o) return false;
            if (len > size - o) len = size - o;
            const u8* s = dst + o - disp;
            for (u32 i = 0; i < len; i++) dst[o + i] = s[i];   // may overlap: byte by byte
            o += len;
        }
    }
    return true;
}

size_t lz11Head(const u8* in, size_t n, u8* out, size_t want) {
    if (n < 4 || in[0] != 0x11) return 0;
    size_t p = 4, o = 0;
    if ((in[1] | (in[2] << 8) | (in[3] << 16)) == 0) p = 8;
    while (o < want && p < n) {
        u8 flags = in[p++];
        for (int bit = 0; bit < 8 && o < want; bit++, flags <<= 1) {
            if (!(flags & 0x80)) { if (p >= n) return o; out[o++] = in[p++]; continue; }
            if (p + 3 >= n) return o;
            u32 b0 = in[p], len, disp;
            switch (b0 >> 4) {
                case 0: len = (((b0 & 0xf) << 4) | (in[p + 1] >> 4)) + 0x11; disp = (((in[p + 1] & 0xf) << 8) | in[p + 2]) + 1; p += 3; break;
                case 1: len = (((b0 & 0xf) << 12) | (in[p + 1] << 4) | (in[p + 2] >> 4)) + 0x111; disp = (((in[p + 2] & 0xf) << 8) | in[p + 3]) + 1; p += 4; break;
                default: len = (b0 >> 4) + 1; disp = (((b0 & 0xf) << 8) | in[p + 1]) + 1; p += 2; break;
            }
            if (disp > o) return o;
            for (u32 i = 0; i < len && o < want; i++, o++) out[o] = out[o - disp];
        }
    }
    return o;
}

void lz11Compress(const u8* in, size_t n, std::vector<u8>& out) {
    out.clear();
    out.reserve(n / 2 + 16);
    out.push_back(0x11); out.push_back(n & 0xff); out.push_back((n >> 8) & 0xff); out.push_back((n >> 16) & 0xff);
    // matches are found through a hash of the next three bytes; each position remembers the previous one with the same hash
    constexpr int WINDOW = 4096, MAX_LEN = 0x110, HASH_BITS = 13, CHAIN = 24;
    std::vector<s32> head(1 << HASH_BITS, -1), prev(WINDOW, -1);
    auto hash = [&](size_t i) { return ((in[i] << 8) ^ (in[i + 1] << 4) ^ in[i + 2]) & ((1 << HASH_BITS) - 1); };
    auto insert = [&](size_t i) { if (i + 2 < n) { int h = hash(i); prev[i % WINDOW] = head[h]; head[h] = (s32)i; } };
    size_t pos = 0;
    while (pos < n) {
        size_t flagAt = out.size();
        out.push_back(0);
        u8 flags = 0;
        for (int bit = 0; bit < 8 && pos < n; bit++) {
            int bestLen = 0, bestDist = 0;
            if (pos + 2 < n) {
                int maxLen = n - pos < (size_t)MAX_LEN ? (int)(n - pos) : MAX_LEN;
                s32 cand = head[hash(pos)];
                for (int steps = 0; cand >= 0 && pos - cand <= WINDOW && steps < CHAIN; steps++) {
                    int len = 0;
                    while (len < maxLen && in[cand + len] == in[pos + len]) len++;
                    if (len > bestLen) { bestLen = len; bestDist = (int)(pos - cand); if (len == maxLen) break; }
                    s32 next = prev[cand % WINDOW];
                    if (next >= cand) break;        // the slot has been reused by a newer position
                    cand = next;
                }
            }
            if (bestLen >= 3) {
                flags |= 0x80 >> bit;
                int disp = bestDist - 1;
                if (bestLen <= 0x10) { out.push_back((u8)(((bestLen - 1) << 4) | (disp >> 8))); out.push_back((u8)disp); }
                else { int l = bestLen - 0x11; out.push_back((u8)(l >> 4)); out.push_back((u8)(((l & 0xf) << 4) | (disp >> 8))); out.push_back((u8)disp); }
                for (int k = 0; k < bestLen; k++) insert(pos + k);
                pos += bestLen;
            } else {
                out.push_back(in[pos]);
                insert(pos);
                pos++;
            }
        }
        out[flagAt] = flags;
    }
    while (out.size() % 4) out.push_back(0);
}

// ---------- theme body ----------
void coloursFromImage(const Image& img, ThemeLook& out) {
    u32 r = 0, g = 0, b = 0, cnt = 0;
    if (img.ok() && !img.rgba) {
        const u16* px = (const u16*)img.data.data();
        for (int y = 4; y < img.h; y += 8)
            for (int x = 4; x < img.w; x += 8) {
                u8 c[3]; from565(px[tiledIndex(x, y, img.texW)], c);
                r += c[0]; g += c[1]; b += c[2]; cnt++;
            }
    }
    u8 avg[3] = {128, 128, 128};
    if (cnt) { avg[0] = r / cnt; avg[1] = g / cnt; avg[2] = b / cnt; }
    float h, s, v; toHsv(avg, h, s, v);
    u8 c[3];
    // bar: dark enough for the white marks drawn on it; a grey wallpaper keeps a grey bar
    float bs = s < 0.08f ? s : fminf(0.7f, fmaxf(0.35f, s * 1.3f));
    hsv(h, bs, fminf(0.55f, fmaxf(0.3f, v * 0.75f)), c); out.colBar = abgr(c);
    hsv(h, s < 0.08f ? 0.0f : 0.08f, 1.0f, c); out.colTile = abgr(c);
    hsv(h, s < 0.08f ? 0.0f : 0.6f, 0.6f, c); out.colGlyph = abgr(c);
}

bool decodeTheme(const u8* body, size_t n, ThemeLook& out) {
    std::vector<u8> d;
    return lz11(body, n, d, 0x150000) && parseTheme(d, out);
}

bool parseTheme(const std::vector<u8>& d, ThemeLook& out) {
    if (d.size() < 0xC4 || rd32(&d[0]) != 1) return false;
    out.bgmFlag = d[5] != 0;
    auto block = [&](u32 flagOff, u32 offOff, u32 size) -> const u8* {
        if (flagOff && rd32(&d[flagOff]) == 0) return nullptr;
        u32 off = rd32(&d[offOff]);
        if (off == 0 || off > d.size() || size > d.size() - off) return nullptr;
        return &d[off];
    };
    u32 topDraw = rd32(&d[0x0C]), topFrame = rd32(&d[0x10]);
    bool ok = false;
    if (topDraw == 3) ok = wallpaper(d, rd32(&d[0x18]), topFrame != 1, 412, 240, out.top);
    if (!ok) {
        const u8* c = (topDraw == 1 || topDraw == 2) ? block(0, 0x14, 3) : nullptr;
        if (c) solid565(out.top, 412, 240, c[0], c[1], c[2]); else solid565(out.top, 412, 240, 0xd8, 0xdc, 0xe0);
    }
    u32 botDraw = rd32(&d[0x20]), botFrame = rd32(&d[0x24]);
    ok = false;
    if (botDraw == 3) ok = wallpaper(d, rd32(&d[0x28]), botFrame != 1, 320, 240, out.bottom);
    if (!ok) {
        const u8* c = botDraw == 1 ? block(0x88, 0x8C, 9) : nullptr;
        if (!c && botDraw == 1) c = block(0x80, 0x84, 0xD);
        if (c) solid565(out.bottom, 320, 240, c[3], c[4], c[5]); else solid565(out.bottom, 320, 240, 0xd8, 0xdc, 0xe0);
    }
    coloursFromImage(out.top, out);
    // the theme's own cursor colour, when it has one and it is not close to white, for the icon shapes
    if (const u8* cur = block(0x2C, 0x30, 0xC)) {
        const u8* m = cur + 3;
        if (m[0] + m[1] + m[2] < 3 * 225) out.colGlyph = abgr(m);
    }
    return true;
}

// ---------- Luma splash ----------
bool decodeSplash(const u8* raw, size_t n, int w, Image& out) {
    if (n != (size_t)w * 240 * 3) return false;
    out.texW = 512; out.texH = 256; out.w = w; out.h = 240; out.rgba = false;
    out.data.assign(512 * 256 * 2, 0);
    u16* dst = (u16*)out.data.data();
    for (int x = 0; x < w; x++) {
        const u8* col = raw + (size_t)x * 720;
        for (int y = 0; y < 240; y++) {
            const u8* p = col + 3 * (239 - y);
            dst[tiledIndex(x, y, 512)] = to565(p[2], p[1], p[0]);
        }
    }
    return true;
}

// ---------- PNG ----------
bool pngSize(const u8* d, size_t n, int& w, int& h) {
    static const u8 sig[8] = {0x89, 'P', 'N', 'G', 13, 10, 26, 10};
    if (n < 24 || memcmp(d, sig, 8) != 0 || memcmp(d + 12, "IHDR", 4) != 0) return false;
    w = (int)be32(d + 16); h = (int)be32(d + 20);
    return w > 0 && h > 0;
}

bool decodePng(const u8* d, size_t n, Png& out, size_t maxPixels) {
    int w, h;
    if (!pngSize(d, n, w, h) || n < 33 || w > 4096 || h > 4096 || (u64)w * h > maxPixels) return false;
    int depth = d[24], ctype = d[25], interlace = d[28];
    if (interlace > 1 || d[26] || d[27]) return false;
    int chans = ctype == 0 ? 1 : ctype == 2 ? 3 : ctype == 3 ? 1 : ctype == 4 ? 2 : ctype == 6 ? 4 : 0;
    if (!chans || (depth != 1 && depth != 2 && depth != 4 && depth != 8 && depth != 16)) return false;
    if (ctype == 3 && depth == 16) return false;
    if ((ctype == 2 || ctype == 4 || ctype == 6) && depth < 8) return false;
    size_t bpp = (chans * depth + 7) / 8;
    if (bpp < 1) bpp = 1;
    auto bytesFor = [&](int pixels) { return ((size_t)pixels * chans * depth + 7) / 8; };

    // The picture arrives as one pass of full rows, or (interlaced, "Adam7") as seven passes that each hold
    // every dx-th pixel of every dy-th row, starting at (x0, y0).
    struct Pass { int x0, y0, dx, dy; };
    static const Pass adam7[7] = {{0, 0, 8, 8}, {4, 0, 8, 8}, {0, 4, 4, 8}, {2, 0, 4, 4}, {0, 2, 2, 4}, {1, 0, 2, 2}, {0, 1, 1, 2}};
    static const Pass whole = {0, 0, 1, 1};
    const Pass* passes = interlace ? adam7 : &whole;
    const int passCount = interlace ? 7 : 1;
    int pass = -1, row = 0, pw = 0, ph = 0;       // current pass, row in it, its size in pixels
    size_t rowBytes = 0;
    bool finished = false;

    u8 pal[256][4];
    for (auto& p : pal) { p[0] = p[1] = p[2] = 0; p[3] = 255; }
    int keyGrey = -1; int keyRGB[3] = {-1, -1, -1};
    out.w = w; out.h = h;
    out.rgba.assign((size_t)w * h * 4, 0);
    std::vector<u8> line(bytesFor(w) + 1), prev(bytesFor(w) + 1, 0);

    z_stream z{};
    if (inflateInit(&z) != Z_OK) return false;
    bool ok = true, ended = false;

    // moves on to the next pass that has pixels; sets up the scanline buffer for it
    auto nextPass = [&]() {
        for (pass++; pass < passCount; pass++) {
            const Pass& ps = passes[pass];
            pw = (w - ps.x0 + ps.dx - 1) / ps.dx; ph = (h - ps.y0 + ps.dy - 1) / ps.dy;
            if (w <= ps.x0 || h <= ps.y0 || pw <= 0 || ph <= 0) continue;
            row = 0; rowBytes = bytesFor(pw);
            memset(prev.data(), 0, prev.size());
            z.next_out = line.data(); z.avail_out = rowBytes + 1;
            return;
        }
        finished = true;
    };
    nextPass();

    auto emitRow = [&]() {
        const Pass& ps = passes[pass];
        u8* cur = line.data() + 1; const u8* up = prev.data() + 1;
        switch (line[0]) {
            case 1: for (size_t i = bpp; i < rowBytes; i++) cur[i] += cur[i - bpp]; break;
            case 2: for (size_t i = 0; i < rowBytes; i++) cur[i] += up[i]; break;
            case 3: for (size_t i = 0; i < rowBytes; i++) cur[i] += ((i >= bpp ? cur[i - bpp] : 0) + up[i]) >> 1; break;
            case 4:
                for (size_t i = 0; i < rowBytes; i++) {
                    int a = i >= bpp ? cur[i - bpp] : 0, b = up[i], c = i >= bpp ? up[i - bpp] : 0;
                    int p = a + b - c, pa = abs(p - a), pb = abs(p - b), pc = abs(p - c);
                    cur[i] += (pa <= pb && pa <= pc) ? a : (pb <= pc ? b : c);
                }
                break;
            default: break;
        }
        int y = ps.y0 + row * ps.dy;
        int step = depth == 16 ? 2 : 1;
        for (int x = 0; x < pw; x++) {
            u8* dst = &out.rgba[((size_t)y * w + ps.x0 + x * ps.dx) * 4];
            if (ctype == 6) { const u8* s = cur + x * 4 * step; dst[0] = s[0]; dst[1] = s[step]; dst[2] = s[2 * step]; dst[3] = s[3 * step]; }
            else if (ctype == 2) {
                const u8* s = cur + x * 3 * step; dst[0] = s[0]; dst[1] = s[step]; dst[2] = s[2 * step];
                dst[3] = (s[0] == keyRGB[0] && s[step] == keyRGB[1] && s[2 * step] == keyRGB[2]) ? 0 : 255;
            } else if (ctype == 4) { const u8* s = cur + x * 2 * step; dst[0] = dst[1] = dst[2] = s[0]; dst[3] = s[step]; }
            else {
                int v;
                if (depth >= 8) v = cur[x * step];
                else { int shift = 8 - depth - ((x * depth) & 7); v = (cur[(x * depth) >> 3] >> shift) & ((1 << depth) - 1); }
                if (ctype == 3) memcpy(dst, pal[v & 255], 4);
                else {
                    int g = depth >= 8 ? v : v * 255 / ((1 << depth) - 1);
                    dst[0] = dst[1] = dst[2] = (u8)g; dst[3] = v == keyGrey ? 0 : 255;
                }
            }
        }
        memcpy(prev.data(), line.data(), rowBytes + 1);
        if (++row >= ph) nextPass();
        else { z.next_out = line.data(); z.avail_out = rowBytes + 1; }
    };

    size_t p = 8;
    while (ok && !ended && p + 12 <= n) {
        u32 len = be32(d + p); const u8* type = d + p + 4; const u8* body = d + p + 8;
        if (len > n - p - 12) { ok = false; break; }      // (p + 12 <= n here; written this way so a huge length cannot wrap)
        if (!memcmp(type, "PLTE", 4)) { for (u32 i = 0; i < len / 3 && i < 256; i++) memcpy(pal[i], body + 3 * i, 3); }
        else if (!memcmp(type, "tRNS", 4)) {
            if (ctype == 3) for (u32 i = 0; i < len && i < 256; i++) pal[i][3] = body[i];
            else if (ctype == 0 && len >= 2) keyGrey = depth == 16 ? body[0] : ((body[0] << 8) | body[1]);
            else if (ctype == 2 && len >= 6) for (int i = 0; i < 3; i++) keyRGB[i] = depth == 16 ? body[2 * i] : body[2 * i + 1];
        } else if (!memcmp(type, "IDAT", 4)) {
            z.next_in = (Bytef*)body; z.avail_in = len;
            while (z.avail_in && !finished) {
                int rc = inflate(&z, Z_NO_FLUSH);
                if (rc != Z_OK && rc != Z_STREAM_END) { ok = false; break; }
                if (z.avail_out == 0) emitRow();
                if (rc == Z_STREAM_END) break;
            }
        } else if (!memcmp(type, "IEND", 4)) ended = true;
        p += 12 + len;
    }
    inflateEnd(&z);
    return ok && finished;
}

void pngTo565(const Png& png, int sx, int sy, int w, int h, Image& out) {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    int tw = 8, th = 8;
    while (tw < w) tw <<= 1;
    while (th < h) th <<= 1;
    out.texW = tw; out.texH = th; out.w = w; out.h = h; out.rgba = false;
    out.data.assign((size_t)tw * th * 2, 0);
    u16* dst = (u16*)out.data.data();
    for (int y = 0; y < h; y++) {
        if (sy + y >= png.h) break;
        const u8* row = &png.rgba[((size_t)(sy + y) * png.w + sx) * 4];
        for (int x = 0; x < w && sx + x < png.w; x++) dst[tiledIndex(x, y, tw)] = to565(row[4 * x], row[4 * x + 1], row[4 * x + 2]);
    }
}

void pngToRGBA(const Png& png, int sx, int sy, int w, int h, Image& out) {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    int tw = 8, th = 8;
    while (tw < w) tw <<= 1;
    while (th < h) th <<= 1;
    out.texW = tw; out.texH = th; out.w = w; out.h = h; out.rgba = true;
    out.data.assign((size_t)tw * th * 4, 0);
    u32* dst = (u32*)out.data.data();
    for (int y = 0; y < h && sy + y < png.h; y++) {
        const u8* row = &png.rgba[((size_t)(sy + y) * png.w + sx) * 4];
        for (int x = 0; x < w && sx + x < png.w; x++) {
            const u8* p = row + 4 * x;
            dst[tiledIndex(x, y, tw)] = ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
        }
    }
}

bool pngEmpty(const Png& png, int sx, int sy, int w, int h) {
    if (sx < 0) sx = 0;
    if (sy < 0) sy = 0;
    for (int y = sy; y < sy + h && y < png.h; y++)
        for (int x = sx; x < sx + w && x < png.w; x++)
            if (png.rgba[((size_t)y * png.w + x) * 4 + 3]) return false;
    return true;
}

}  // namespace fmt
