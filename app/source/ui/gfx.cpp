// A small batched 2D renderer directly on citro3d. It does not use citro2d: citro2d's tinting and
// circles depend on procedural textures, which the Azahar emulator does not draw, and one renderer
// with three plain texture-combiner modes is easier to keep fast on old hardware.
#include "gfx.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <tex3ds.h>
#include <memory>
#include <vector>
#include "../core/formats.h"
#include "../core/log.h"
#include "../core/worker.h"
#include "font_gen.h"
#include "lineheight_gen.h"
#include "kern_gen.h"
#include "render2d_shbin.h"

namespace gfx {

namespace {

constexpr int FONT_PX[FONT_COUNT] = {10, 10, 11, 11, 12, 13, 14, 15, 19, 22};
// Baseline offset from the top of a line box whose height equals the font size, as measured in Chromium
// for M PLUS 1p (tools/measure_font.mjs). Only the system-font fallback needs it; the UI font's glyphs
// carry their own offsets.
constexpr int CSS_BASELINE[FONT_COUNT] = {9, 9, 9, 9, 10, 11, 12, 13, 16, 19};
// Row of g_lineHeights for each font.
constexpr int LH_ROW[FONT_COUNT] = {0, 0, 1, 1, 2, 3, 4, 5, 6, 7};

enum Mode { M_NONE, M_SOLID, M_IMAGE, M_MASK };
struct Vtx { float x, y, z, u, v; u32 color; };
constexpr int MAX_VTX = 6 * 8000;

C3D_RenderTarget* g_target[2];
C3D_Mtx g_proj[2];
Screen g_screen = TOP;
DVLB_s* g_dvlb;
shaderProgram_s g_prog;
int g_uProj;
Vtx* g_vbo;
int g_vcount = 0, g_vstart = 0;
C3D_Tex* g_curTex = nullptr;
Mode g_curMode = M_NONE;

C3D_Tex g_atlas;
float g_du, g_dv;   // atlas pixel -> uv (v flipped: row 0 is at v = 1)

C3D_Tex g_fontTex;      // the UI font atlas (alpha only)
float g_fdu, g_fdv;
CFNT_s* g_sys = nullptr; C3D_Tex* g_sysSheets = nullptr;   // system font, for glyphs the UI font lacks

struct Xf { float tx, ty, s, a; };
Xf g_stack[16];
int g_sp = 0;
inline Xf& X() { return g_stack[g_sp]; }

struct WidthCache { u32 key; float w; };
WidthCache g_wc[1024];

int g_clipDepth = 0;
bool g_clipEmpty = false;   // the current clip rectangle has no area: nothing is drawn
float g_fill = 0;      // pixels covered by this frame's quads (dev builds), for the overdraw figure in the perf log

u32 hashStr(Font f, const char* s) {
    u32 h = 2166136261u ^ (u32)f;
    while (*s) { h ^= (u8)*s++; h *= 16777619u; }
    return h ? h : 1;
}

inline u32 mulAlpha(u32 color, float a) {
    if (a >= 1.0f) return color;
    u32 al = (u32)((color >> 24) * (a < 0 ? 0 : a) + 0.5f);
    return (color & 0x00ffffff) | (al << 24);
}

void flush() {
    if (g_vcount > g_vstart) C3D_DrawArrays(GPU_TRIANGLES, g_vstart, g_vcount - g_vstart);
    g_vstart = g_vcount;
}

void setState(Mode m, C3D_Tex* tex) {
    if (m == g_curMode && (m == M_SOLID || tex == g_curTex)) return;
    flush();
    if (m != g_curMode) {
        C3D_TexEnv* env = C3D_GetTexEnv(0);
        C3D_TexEnvInit(env);
        if (m == M_SOLID) {
            C3D_TexEnvSrc(env, C3D_Both, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_REPLACE);
        } else if (m == M_IMAGE) {
            C3D_TexEnvSrc(env, C3D_Both, GPU_TEXTURE0, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_Both, GPU_MODULATE);
        } else {  // M_MASK: the texture supplies only the shape; the colour comes from the vertex
            C3D_TexEnvSrc(env, C3D_RGB, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_RGB, GPU_REPLACE);
            C3D_TexEnvSrc(env, C3D_Alpha, GPU_TEXTURE0, GPU_PRIMARY_COLOR, (GPU_TEVSRC)0);
            C3D_TexEnvFunc(env, C3D_Alpha, GPU_MODULATE);
        }
        g_curMode = m;
    }
    if (m != M_SOLID && tex != g_curTex) { C3D_TexBind(0, tex); g_curTex = tex; }
}

inline void vtx(float x, float y, float u, float v, u32 c) {
    Vtx& o = g_vbo[g_vcount++];
    o.x = x; o.y = y; o.z = 0.5f; o.u = u; o.v = v; o.color = c;
}

// Axis-aligned quad in screen space (already transformed).
void quadRaw(float x0, float y0, float x1, float y1, float u0, float v0, float u1, float v1, u32 ct, u32 cb) {
    if (g_vcount + 6 > MAX_VTX || g_clipEmpty) return;
    // nothing to draw: off the screen, or fully transparent
    const float sw = g_screen == TOP ? 400.0f : 320.0f;
    if (x1 <= 0 || y1 <= 0 || x0 >= sw || y0 >= 240.0f || !((ct | cb) >> 24)) return;
#ifdef THEME_PLAZA_DEV
    g_fill += ((x1 < sw ? x1 : sw) - (x0 > 0 ? x0 : 0)) * ((y1 < 240.0f ? y1 : 240.0f) - (y0 > 0 ? y0 : 0));
#endif
    vtx(x0, y0, u0, v0, ct); vtx(x0, y1, u0, v1, cb); vtx(x1, y0, u1, v0, ct);
    vtx(x1, y0, u1, v0, ct); vtx(x0, y1, u0, v1, cb); vtx(x1, y1, u1, v1, cb);
}

// Quad centred on (cx,cy), rotated.
void quadRot(float cx, float cy, float w, float h, float angle, float u0, float v0, float u1, float v1, u32 c) {
    if (g_vcount + 6 > MAX_VTX || g_clipEmpty || !(c >> 24)) return;
#ifdef THEME_PLAZA_DEV
    g_fill += w * h;
#endif
    float s = sinf(angle), co = cosf(angle), hw = w / 2, hh = h / 2;
    float px[4] = {-hw, hw, -hw, hw}, py[4] = {-hh, -hh, hh, hh}, X_[4], Y_[4];
    for (int i = 0; i < 4; i++) { X_[i] = cx + px[i] * co - py[i] * s; Y_[i] = cy + px[i] * s + py[i] * co; }
    vtx(X_[0], Y_[0], u0, v0, c); vtx(X_[2], Y_[2], u0, v1, c); vtx(X_[1], Y_[1], u1, v0, c);
    vtx(X_[1], Y_[1], u1, v0, c); vtx(X_[2], Y_[2], u0, v1, c); vtx(X_[3], Y_[3], u1, v1, c);
}

// Atlas pixels (sx,sy,sw,sh) into the local rectangle (x,y,w,h).
void atlasQuad(Mode m, float sx, float sy, float sw, float sh, float x, float y, float w, float h, u32 color) {
    if (w <= 0 || h <= 0 || sw <= 0 || sh <= 0) return;
    setState(m, &g_atlas);
    const Xf& t = X();
    quadRaw(t.tx + t.s * x, t.ty + t.s * y, t.tx + t.s * (x + w), t.ty + t.s * (y + h),
            sx * g_du, 1.0f - sy * g_dv, (sx + sw) * g_du, 1.0f - (sy + sh) * g_dv, color, color);
}

// Layout positions can be fractional (text widths are); a browser paints boxes on whole pixels, so do the same.
inline float snap(float v) { return floorf(v + 0.5f); }

void sliceImpl(Mode m, Spr s, float x, float y, float w, float h, u32 color) {
    const SprInfo& i = g_spr[s];
    { float x1 = snap(x + w), y1 = snap(y + h); x = snap(x); y = snap(y); w = x1 - x; h = y1 - y; }
    // margins outside the box (shadow / glow), then the fixed corners inside it
    float ml = i.ox, mt = i.oy, mr = i.w - i.ox - i.bw, mb = i.h - i.oy - i.bh;
    float cl = ml + i.sl, cr = mr + i.sr, ct = mt + i.st, cb = mb + i.sb;
    float midSW = i.w - cl - cr, midSH = i.h - ct - cb;
    float dx0 = x - ml, dy0 = y - mt;
    float midW = w - i.sl - i.sr, midH = h - i.st - i.sb;
    bool hs = i.sl || i.sr, vs = i.st || i.sb;
    if (!hs) { cl = 0; cr = 0; midSW = i.w; midW = w + ml + mr; }
    if (!vs) { ct = 0; cb = 0; midSH = i.h; midH = h + mt + mb; }
    float xs[4] = {dx0, dx0 + cl, dx0 + cl + midW, dx0 + cl + midW + cr};
    float ys[4] = {dy0, dy0 + ct, dy0 + ct + midH, dy0 + ct + midH + cb};
    float sxs[4] = {(float)i.x, i.x + cl, i.x + cl + midSW, (float)i.x + i.w};
    float sys[4] = {(float)i.y, i.y + ct, i.y + ct + midSH, (float)i.y + i.h};
    // An unsliced axis that is being stretched is a uniform strip: sample only its middle, or linear
    // filtering would fade the two ends into the atlas gutter.
    if (!hs && i.w >= 3 && fabsf(midW - i.w) > 0.01f) { sxs[1] = i.x + 1.0f; sxs[2] = i.x + i.w - 1.0f; }
    if (!vs && i.h >= 3 && fabsf(midH - i.h) > 0.01f) { sys[1] = i.y + 1.0f; sys[2] = i.y + i.h - 1.0f; }
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 3; c++)
            atlasQuad(m, sxs[c], sys[r], sxs[c + 1] - sxs[c], sys[r + 1] - sys[r], xs[c], ys[r], xs[c + 1] - xs[c], ys[r + 1] - ys[r], color);
}

C3D_Tex* makeSheets(CFNT_s* cfnt) {
    TGLP_s* gi = cfnt->finf.tglp;
    C3D_Tex* sheets = (C3D_Tex*)calloc(gi->nSheets, sizeof(C3D_Tex));
    for (int i = 0; i < gi->nSheets; i++) {
        C3D_Tex* tex = &sheets[i];
        tex->data = &gi->sheetData[gi->sheetSize * i];
        tex->fmt = (GPU_TEXCOLOR)gi->sheetFmt;
        tex->size = gi->sheetSize;
        tex->width = gi->sheetWidth;
        tex->height = gi->sheetHeight;
        tex->param = GPU_TEXTURE_MAG_FILTER(GPU_LINEAR) | GPU_TEXTURE_MIN_FILTER(GPU_LINEAR) | GPU_TEXTURE_WRAP_S(GPU_CLAMP_TO_EDGE) | GPU_TEXTURE_WRAP_T(GPU_CLAMP_TO_EDGE);
        tex->border = 0;
        tex->lodParam = 0;
    }
    return sheets;
}

const GlyphInfo* findGlyph(Font f, u32 code) {
    const FontInfo& fi = g_fonts[f];
    int lo = 0, hi = fi.count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        u32 c = fi.glyphs[mid].code;
        if (c == code) return &fi.glyphs[mid];
        if (c < code) lo = mid + 1; else hi = mid - 1;
    }
    return nullptr;
}

// Kerning in pixels between two consecutive characters (the table covers printable ASCII).
inline float kern(Font f, u32 a, u32 b) {
    if (a < 32 || a > 126 || b < 32 || b > 126) return 0;
    return g_kern[(a - 32) * 95 + (b - 32)] * (FONT_PX[f] / 1000.0f);
}

// The same with the letter before those two taken into account. The browser joins "ff" into one glyph, and what
// follows that glyph sits differently than after a single f (measured in Chromium at 1000px: "ffi" and "ffl"
// are 973 wide, "ff" 818, i and l 320 each; a third f is not pulled in at all).
inline float kern(Font f, u32 before, u32 a, u32 b) {
    if (before == 'f' && a == 'f') {
        if (b == 'i' || b == 'l') return (973 - 818 - 320) * (FONT_PX[f] / 1000.0f);
        if (b == 'f') return 0;
    }
    return kern(f, a, b);
}

// the system font's cell is 30px with roughly a 21px em
inline float sysScale(Font f) { return FONT_PX[f] / 21.0f; }

// ---------- the system fonts of the other regions ----------
// A console's own system font covers Latin, Japanese and symbols (or Chinese, or Korean, by region), and
// Theme Plaza has names in all of them. Every console carries the system fonts of all four regions as data
// archives, so one this console does not use is fetched when a name first needs it: read and unpacked on the
// disk worker (a few MB), one at a time. Until it is there the system font's own placeholder is drawn.
enum { EX_NONE, EX_LOADING, EX_READY, EX_OFF };    // EX_OFF: the console's own font, or one that could not be loaded
struct Extra { u64 title; const char* file; CFNT_s* font; C3D_Tex* sheets; u8 state; };
Extra g_extra[4] = {
    {0x0004009B00014002ULL, "sysfont:/cbf_std.bcfnt.lz", nullptr, nullptr, EX_NONE},
    {0x0004009B00014102ULL, "sysfont:/cbf_zh-Hans-CN.bcfnt.lz", nullptr, nullptr, EX_NONE},
    {0x0004009B00014202ULL, "sysfont:/cbf_ko-Hang-KR.bcfnt.lz", nullptr, nullptr, EX_NONE},
    {0x0004009B00014302ULL, "sysfont:/cbf_zh-Hant-TW.bcfnt.lz", nullptr, nullptr, EX_NONE},
};
bool g_extraBusy = false;
void clearTextMemos();

void loadExtra(int i) {
    g_extra[i].state = EX_LOADING; g_extraBusy = true;
    worker::disk.post([i] {
        auto plain = std::make_shared<std::vector<u8>>();
        Result rc = romfsMountFromTitle(g_extra[i].title, MEDIATYPE_NAND, "sysfont");
        if (R_SUCCEEDED(rc)) {
            std::vector<u8> packed;
            if (FILE* f = fopen(g_extra[i].file, "rb")) {
                u8 chunk[16 * 1024]; size_t n;
                while ((n = fread(chunk, 1, sizeof chunk, f)) > 0 && packed.size() < 8 * 1024 * 1024) packed.insert(packed.end(), chunk, chunk + n);
                fclose(f);
            }
            romfsUnmount("sysfont");
            if (!fmt::lz11(packed.data(), packed.size(), *plain, 16 * 1024 * 1024) || plain->size() < 0x100 || memcmp(plain->data(), "CFNT", 4) != 0) plain->clear();
        }
        LOG("system font %d: %s (%08lX, %lu bytes)", i, plain->empty() ? "could not be read" : "read", (unsigned long)rc, (unsigned long)plain->size());
        worker::toMain([i, plain] {
            // The glyph pictures are textures, so the font has to sit in linear memory, which only the main
            // thread hands out. It is left alone if that would crowd out the previews.
            u8* mem = !plain->empty() && linearSpaceFree() > plain->size() + 8 * 1024 * 1024 ? (u8*)linearAlloc(plain->size()) : nullptr;
            if (!mem) {
                g_extra[i].state = EX_OFF; g_extraBusy = false;
                if (!plain->empty()) LOG("system font %d: not enough memory for it", i);
                clearTextMemos();   // so that the next font in line is asked for
                return;
            }
            worker::disk.post([i, plain, mem] {
                memcpy(mem, plain->data(), plain->size());
                size_t n = plain->size();
                worker::toMain([i, mem, n] {
                    Extra& e = g_extra[i];
                    e.font = (CFNT_s*)mem;
                    fontFixPointers(e.font);
                    e.sheets = makeSheets(e.font);
                    GSPGPU_FlushDataCache(mem, n);
                    e.state = EX_READY; g_extraBusy = false;
                    clearTextMemos();   // widths and cut-off points worked out with the placeholder
                });
            });
        });
    });
}

// The system-font glyph for a character the UI font lacks: from the console's own font, else from another
// region's (asking for that font if it is not there yet).
struct SysGlyph { CFNT_s* font; C3D_Tex* sheets; int index; };
// Finding a character in a system font walks its tables (thousands of entries for kanji), and a line of
// Japanese or Chinese asks for each of its characters twice per frame, so the answers are remembered.
struct GlyphMemo { u32 code; SysGlyph g; };
GlyphMemo g_glyphMemo[512];
bool sysGlyphUncached(u32 code, SysGlyph& g);
bool sysGlyph(u32 code, SysGlyph& g) {
    if (!g_sys) return false;
    GlyphMemo& m = g_glyphMemo[(code * 2654435761u) >> 23];
    if (m.code != code || !m.g.font) { sysGlyphUncached(code, m.g); m.code = code; }
    g = m.g;
    return true;
}
bool sysGlyphUncached(u32 code, SysGlyph& g) {
    g = {g_sys, g_sysSheets, fontGlyphIndexFromCodePoint(g_sys, code)};
    if (g.index != g_sys->finf.alterCharIndex) return true;
    const bool hangul = (code >= 0x1100 && code < 0x1200) || (code >= 0x3130 && code < 0x3190) || (code >= 0xAC00 && code < 0xD7B0);
    const bool cjk = (code >= 0x2E80 && code < 0xA000) || (code >= 0xF900 && code < 0xFB00) || (code >= 0xFF00 && code < 0xFFF0);
    if (!hangul && !cjk) return true;
    for (Extra& e : g_extra) {
        if (e.state != EX_READY) continue;
        int idx = fontGlyphIndexFromCodePoint(e.font, code);
        if (idx != e.font->finf.alterCharIndex) { g = {e.font, e.sheets, idx}; return true; }
    }
    if (!g_extraBusy) {
        static const int forHangul[] = {2}, forCjk[] = {1, 3, 0};
        if (hangul) { for (int i : forHangul) if (g_extra[i].state == EX_NONE) { loadExtra(i); break; } }
        else { for (int i : forCjk) if (g_extra[i].state == EX_NONE) { loadExtra(i); break; } }
    }
    return true;
}

float measure(Font f, const char* str) {
    float w = 0;
    u32 prev = 0, before = 0;
    const u8* p = (const u8*)str;
    while (*p) {
        u32 code; ssize_t n = decode_utf8(&code, p);
        if (n <= 0) break;
        p += n;
        w += kern(f, before, prev, code); before = prev; prev = code;
        if (const GlyphInfo* gi = findGlyph(f, code)) w += gi->adv;
        else if (SysGlyph sg; sysGlyph(code, sg)) w += fontGetCharWidthInfo(sg.font, sg.index)->charWidth * sysScale(f);
    }
    // The browser's layout works in 1/64 px and rounds a run of text up to the next step. Doing the same keeps
    // centred and right-aligned things on a consistent pixel when a sum lands near a half.
    return ceilf(w * 64.0f - 0.01f) / 64.0f;
}

float drawText(Font f, float x, float y, u32 color, const char* str, Align align, float lineH) {
    if (!str || !*str) return 0;
    const Xf& xf = X();
    float w = textWidth(f, str);
    int px = FONT_PX[f];
    float lh = lineH > 0 ? lineH : (float)px;
    // Where the browser puts the text inside a taller line box (it rounds differently per font size).
    int extra = (int)(lh - px + 0.5f);
    float top = y + (extra >= 0 && extra <= LH_EXTRA_MAX ? (float)g_lineHeights[LH_ROW[f]].shift[extra] : floorf((lh - px) / 2.0f));
    float pen = align == CENTER ? x - w / 2.0f : align == RIGHT ? x - w : x;
    u32 c = mulAlpha(color, xf.a);
    u32 prev = 0, before = 0;
    const u8* p = (const u8*)str;
    while (*p) {
        u32 code; ssize_t n = decode_utf8(&code, p);
        if (n <= 0) break;
        p += n;
        pen += kern(f, before, prev, code); before = prev; prev = code;
        if (const GlyphInfo* gi = findGlyph(f, code)) {
            if (gi->w) {
                setState(M_MASK, &g_fontTex);
                float gx = floorf(pen + 0.5f) + gi->ox, gy = top + gi->oy;
                quadRaw(xf.tx + xf.s * gx, xf.ty + xf.s * gy, xf.tx + xf.s * (gx + gi->w), xf.ty + xf.s * (gy + gi->h),
                        gi->x * g_fdu, 1.0f - gi->y * g_fdv, (gi->x + gi->w) * g_fdu, 1.0f - (gi->y + gi->h) * g_fdv, c, c);
            }
            pen += gi->adv;
        } else if (SysGlyph sg; sysGlyph(code, sg)) {
            float sc = sysScale(f);
            fontGlyphPos_s gp;
            fontCalcGlyphPos(&gp, sg.font, sg.index, GLYPH_POS_CALC_VTXCOORD | GLYPH_POS_AT_BASELINE, sc, sc);
            if (gp.width > 0) {
                setState(M_MASK, &sg.sheets[gp.sheetIndex]);
                float base = top + CSS_BASELINE[f];
                quadRaw(xf.tx + xf.s * (pen + gp.vtxcoord.left), xf.ty + xf.s * (base + gp.vtxcoord.top),
                        xf.tx + xf.s * (pen + gp.vtxcoord.right), xf.ty + xf.s * (base + gp.vtxcoord.bottom),
                        gp.texcoord.left, gp.texcoord.top, gp.texcoord.right, gp.texcoord.bottom, c, c);
            }
            pen += gp.xAdvance;
        }
    }
    return w;
}

}  // namespace

void init() {
    C3D_Init(C3D_DEFAULT_CMDBUF_SIZE * 2);
    const u32 flags = GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(0) | GX_TRANSFER_RAW_COPY(0) |
                      GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGB8) |
                      GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO);
    g_target[TOP] = C3D_RenderTargetCreate(240, 400, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    C3D_RenderTargetSetOutput(g_target[TOP], GFX_TOP, GFX_LEFT, flags);
    g_target[BOT] = C3D_RenderTargetCreate(240, 320, GPU_RB_RGBA8, GPU_RB_DEPTH16);
    C3D_RenderTargetSetOutput(g_target[BOT], GFX_BOTTOM, GFX_LEFT, flags);

    g_dvlb = DVLB_ParseFile((u32*)render2d_shbin, render2d_shbin_size);
    shaderProgramInit(&g_prog);
    shaderProgramSetVsh(&g_prog, &g_dvlb->DVLE[0]);
    C3D_BindProgram(&g_prog);
    g_uProj = shaderInstanceGetUniformLocation(g_prog.vertexShader, "projection");

    C3D_AttrInfo* attr = C3D_GetAttrInfo();
    AttrInfo_Init(attr);
    AttrInfo_AddLoader(attr, 0, GPU_FLOAT, 3);          // position
    AttrInfo_AddLoader(attr, 1, GPU_FLOAT, 2);          // texcoord
    AttrInfo_AddLoader(attr, 2, GPU_UNSIGNED_BYTE, 4);  // colour
    g_vbo = (Vtx*)linearAlloc(sizeof(Vtx) * MAX_VTX);
    C3D_BufInfo* buf = C3D_GetBufInfo();
    BufInfo_Init(buf);
    BufInfo_Add(buf, g_vbo, sizeof(Vtx), 3, 0x210);

    Mtx_OrthoTilt(&g_proj[TOP], 0.0f, 400.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
    Mtx_OrthoTilt(&g_proj[BOT], 0.0f, 320.0f, 240.0f, 0.0f, 1.0f, -1.0f, true);
    C3D_DepthTest(false, GPU_ALWAYS, GPU_WRITE_COLOR);
    C3D_CullFace(GPU_CULL_NONE);
    C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_ONE, GPU_ONE_MINUS_SRC_ALPHA);
    g_stack[0] = {0, 0, 1, 1};
}

// The sprite sheet, the font and the system fonts. Plain rectangles and gradients can be drawn before this.
void loadAssets() {
    FILE* f = fopen("romfs:/gfx/ui.t3x", "rb");
    if (!f) svcBreak(USERBREAK_PANIC);
    Tex3DS_Texture t3x = Tex3DS_TextureImportStdio(f, &g_atlas, nullptr, false);
    fclose(f);
    if (!t3x) svcBreak(USERBREAK_PANIC);
    Tex3DS_TextureFree(t3x);
    C3D_TexSetFilter(&g_atlas, GPU_LINEAR, GPU_LINEAR);
    C3D_TexSetWrap(&g_atlas, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
    g_du = 1.0f / g_atlas.width; g_dv = 1.0f / g_atlas.height;

    f = fopen("romfs:/gfx/font.t3x", "rb");
    if (!f) svcBreak(USERBREAK_PANIC);
    t3x = Tex3DS_TextureImportStdio(f, &g_fontTex, nullptr, false);
    fclose(f);
    if (!t3x) svcBreak(USERBREAK_PANIC);
    Tex3DS_TextureFree(t3x);
    C3D_TexSetFilter(&g_fontTex, GPU_LINEAR, GPU_LINEAR);
    g_fdu = 1.0f / g_fontTex.width; g_fdv = 1.0f / g_fontTex.height;
    if (R_SUCCEEDED(fontEnsureMapped())) { g_sys = fontGetSystemFont(); g_sysSheets = makeSheets(g_sys); }
    // which of the four system fonts is this console's own (regions 0 to 3 share the standard one)
    u8 region = 0;
    if (R_FAILED(CFGU_SecureInfoGetRegion(&region))) region = 0;
    g_extra[region == 4 ? 1 : region == 5 ? 2 : region == 6 ? 3 : 0].state = EX_OFF;
    g_stack[0] = {0, 0, 1, 1};
}

void fini() {
    shaderProgramFree(&g_prog);
    DVLB_Free(g_dvlb);
    C3D_TexDelete(&g_atlas);
    C3D_Fini();
}

void frameBegin() {
    C3D_FrameBegin(C3D_FRAME_SYNCDRAW);
    g_vcount = g_vstart = 0; g_fill = 0;
    g_curMode = M_NONE; g_curTex = nullptr;
    g_sp = 0;
    g_stack[0] = {0, 0, 1, 1};
}

void screenBegin(Screen s) {
    flush();
    g_clipDepth = 0; g_clipEmpty = false;
    C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
    g_screen = s;
    C3D_RenderTargetClear(g_target[s], C3D_CLEAR_ALL, 0x000000ff, 0);
    C3D_FrameDrawOn(g_target[s]);
    C3D_FVUnifMtx4x4(GPU_VERTEX_SHADER, g_uProj, &g_proj[s]);
}

void frameEnd() {
    flush();
    C3D_FrameEnd(0);
}

Screen screen() { return g_screen; }
int vertexCount() { return g_vcount; }
float fillArea() { return g_fill; }

void push(float tx, float ty, float scale, float alpha, float ox, float oy) {
    const Xf p = X();
    if (g_sp < 15) g_sp++;
    Xf& n = X();
    n.s = p.s * scale;
    n.tx = p.tx + p.s * (tx + ox * (1.0f - scale));
    n.ty = p.ty + p.s * (ty + oy * (1.0f - scale));
    n.a = p.a * alpha;
}
void pop() { if (g_sp > 0) g_sp--; }

void rect(float x, float y, float w, float h, u32 color) { gradV(x, y, w, h, color, color); }

void gradV(float x, float y, float w, float h, u32 top, u32 bottom) {
    const Xf& t = X();
    setState(M_SOLID, nullptr);
    quadRaw(t.tx + t.s * x, t.ty + t.s * y, t.tx + t.s * (x + w), t.ty + t.s * (y + h), 0, 0, 0, 0, mulAlpha(top, t.a), mulAlpha(bottom, t.a));
}

void spr(Spr s, float x, float y, float alpha) {
    const SprInfo& i = g_spr[s];
    atlasQuad(M_IMAGE, i.x, i.y, i.w, i.h, snap(x) - i.ox, snap(y) - i.oy, i.w, i.h, mulAlpha(0xffffffff, alpha * X().a));
}

void sprColor(Spr s, float x, float y, u32 color) {
    const SprInfo& i = g_spr[s];
    atlasQuad(M_MASK, i.x, i.y, i.w, i.h, snap(x) - i.ox, snap(y) - i.oy, i.w, i.h, mulAlpha(color, X().a));
}

void sprEx(Spr s, float cx, float cy, float scale, float angle, u32 color, bool flat) {
    const SprInfo& i = g_spr[s];
    const Xf& t = X();
    setState(flat ? M_MASK : M_IMAGE, &g_atlas);
    u32 c = flat ? mulAlpha(color, t.a) : mulAlpha(0xffffffff, (color >> 24) / 255.0f * t.a);
    quadRot(t.tx + t.s * cx, t.ty + t.s * cy, i.w * scale * t.s, i.h * scale * t.s, angle,
            i.x * g_du, 1.0f - i.y * g_dv, (i.x + i.w) * g_du, 1.0f - (i.y + i.h) * g_dv, c);
}

void sprPart(Spr s, float sx, float sy, float sw, float sh, float x, float y, float alpha) {
    const SprInfo& i = g_spr[s];
    atlasQuad(M_IMAGE, i.x + i.ox + sx, i.y + i.oy + sy, sw, sh, snap(x), snap(y), sw, sh, mulAlpha(0xffffffff, alpha * X().a));
}

void slice(Spr s, float x, float y, float w, float h, float alpha) {
    sliceImpl(M_IMAGE, s, x, y, w, h, mulAlpha(0xffffffff, alpha * X().a));
}

void sliceColor(Spr s, float x, float y, float w, float h, u32 color) {
    sliceImpl(M_MASK, s, x, y, w, h, mulAlpha(color, X().a));
}

void image(C3D_Tex* tex, float sx, float sy, float sw, float sh, float x, float y, float w, float h, float alpha) {
    if (!tex) return;
    const Xf& t = X();
    setState(M_IMAGE, tex);
    u32 c = mulAlpha(0xffffffff, alpha * t.a);
    quadRaw(t.tx + t.s * x, t.ty + t.s * y, t.tx + t.s * (x + w), t.ty + t.s * (y + h),
            sx / tex->width, 1.0f - sy / tex->height, (sx + sw) / tex->width, 1.0f - (sy + sh) / tex->height, c, c);
}

void imageRot(C3D_Tex* tex, float sx, float sy, float sw, float sh, float cx, float cy, float w, float h, float angle, float alpha) {
    if (!tex) return;
    const Xf& t = X();
    setState(M_IMAGE, tex);
    quadRot(t.tx + t.s * cx, t.ty + t.s * cy, t.s * w, t.s * h, angle,
            sx / tex->width, 1.0f - sy / tex->height, (sx + sw) / tex->width, 1.0f - (sy + sh) / tex->height, mulAlpha(0xffffffff, alpha * t.a));
}

float text(Font f, float x, float y, u32 color, const char* str, Align align, float lineH) {
    return drawText(f, x, y, color, str, align, lineH);
}

float textShadow(Font f, float x, float y, u32 color, u32 shadow, const char* str, Align align, float lineH) {
    drawText(f, x, y + 1, shadow, str, align, lineH);
    return drawText(f, x, y, color, str, align, lineH);
}

float textWidth(Font f, const char* str) {
    if (!str || !*str) return 0;
    u32 k = hashStr(f, str);
    WidthCache& c = g_wc[k & 1023];
    if (c.key == k) return c.w;
    c.key = k; c.w = measure(f, str);
    return c.w;
}

// fit() and wrap() are asked the same question every frame while a screen is up, and answering it means
// measuring the text many times over. Both remember their recent answers.
namespace {
struct FitMemo { u32 key = 0; char text[160]; };
struct WrapMemo { u32 key = 0; int n = 0; char lines[3][160]; };
FitMemo g_fitMemo[64];
WrapMemo g_wrapMemo[16];
u32 memoKey(Font f, const char* s, float maxW, u32 salt) {
    u32 h = hashStr(f, s) ^ (salt * 0x9e3779b9u) ^ ((u32)(maxW * 4.0f) * 0x85ebca6bu);
    return h ? h : 1;
}
size_t fitUncached(Font f, const char* str, float maxW, char* out, size_t outSize);
int wrapUncached(Font f, const char* str, float maxW, int maxLines, char lines[][160]);
void clearTextMemos() {
    for (auto& m : g_glyphMemo) m.g.font = nullptr;
    for (auto& c : g_wc) c.key = 0;
    for (auto& m : g_fitMemo) m.key = 0;
    for (auto& m : g_wrapMemo) m.key = 0;
}
}  // namespace

size_t fit(Font f, const char* str, float maxW, char* out, size_t outSize) {
    u32 key = memoKey(f, str, maxW, 1);
    FitMemo& m = g_fitMemo[key & 63];
    if (m.key != key) { fitUncached(f, str, maxW, m.text, sizeof m.text); m.key = key; }
    size_t n = strlen(m.text);
    if (n >= outSize) return fitUncached(f, str, maxW, out, outSize);   // the caller's buffer is smaller than the answer
    memcpy(out, m.text, n + 1);
    return n;
}

int wrap(Font f, const char* str, float maxW, int maxLines, char lines[][160]) {
    if (maxLines > 3) return wrapUncached(f, str, maxW, maxLines, lines);
    u32 key = memoKey(f, str, maxW, 2 + maxLines);
    WrapMemo& m = g_wrapMemo[key & 15];
    if (m.key != key) { m.n = wrapUncached(f, str, maxW, maxLines, m.lines); m.key = key; }
    for (int i = 0; i < m.n; i++) memcpy(lines[i], m.lines[i], 160);
    return m.n;
}

namespace {
size_t fitUncached(Font f, const char* str, float maxW, char* out, size_t outSize) {
    size_t n = strlen(str);
    if (n >= outSize) n = outSize - 1;
    memcpy(out, str, n); out[n] = 0;
    if (measure(f, out) <= maxW) return n;
    static const char ell[] = "\xe2\x80\xa6";
    while (n > 0) {
        n--;
        while (n > 0 && ((u8)out[n] & 0xc0) == 0x80) n--;  // do not cut inside a UTF-8 sequence
        while (n > 0 && out[n - 1] == ' ') n--;
        if (n + 4 >= outSize) continue;
        memcpy(out + n, ell, 4);
        if (measure(f, out) <= maxW) return n;
    }
    out[0] = 0;
    return 0;
}

// Chinese, Japanese and Korean text has no spaces: a line may end after any of its characters, as in the
// browser, but closing punctuation is not carried to the start of the next line.
bool isCjk(u32 c) { return (c >= 0x2E80 && c < 0xA000) || (c >= 0xAC00 && c < 0xD7B0) || (c >= 0xF900 && c < 0xFB00) || (c >= 0xFF00 && c < 0xFFF0); }
bool stayWithPrevious(u32 c) {
    if (c < 0x80) return c && strchr(",.:;!?)]}%", (int)c) != nullptr;   // plain punctuation written straight after such a character
    static const u16 marks[] = {0x3001, 0x3002, 0x3005, 0x3009, 0x300B, 0x300D, 0x300F, 0x3011, 0x30FC, 0xFF01, 0xFF09, 0xFF0C, 0xFF0E, 0xFF1A, 0xFF1B, 0xFF1F};
    for (u16 m : marks) if (c == m) return true;
    return false;
}

int wrapUncached(Font f, const char* str, float maxW, int maxLines, char lines[][160]) {
    int n = 0;
    const char* p = str;
    while (*p && n < maxLines) {
        while (*p == ' ') p++;
        if (!*p) break;
        char line[160]; size_t len = 0; const char* lineEnd = p; const char* q = p;
        // add whole words while they fit
        while (*q) {
            const char* w = q;
            while (*w == ' ') w++;
            const char* e = w;
            u32 c = 0; ssize_t k = decode_utf8(&c, (const u8*)e);
            if (*e && k > 0 && isCjk(c)) {
                // one character is a unit of its own, together with any closing punctuation after it
                e += k;
                while (*e && (k = decode_utf8(&c, (const u8*)e)) > 0 && stayWithPrevious(c)) e += k;
            } else {
                while (*e && *e != ' ' && *e != '\n') {
                    k = decode_utf8(&c, (const u8*)e);
                    if (k <= 0) { e++; continue; }
                    if (isCjk(c) && !stayWithPrevious(c)) break;
                    e += k;
                }
            }
            size_t cand = (size_t)(e - p);
            if (cand >= sizeof line) break;
            memcpy(line, p, cand); line[cand] = 0;
            if (measure(f, line) > maxW && len > 0) break;
            len = cand; lineEnd = e; q = e;
            if (*e == '\n') break;
        }
        if (len == 0) { len = strlen(p); if (len >= sizeof line) len = sizeof line - 1; lineEnd = p + len; }
        memcpy(line, p, len); line[len] = 0;
        const char* rest = lineEnd; if (*rest == '\n') rest++;
        while (*rest == ' ') rest++;
        if (n == maxLines - 1 && *rest) fitUncached(f, p, maxW, lines[n], 160);   // text remains: cut with an ellipsis
        else fitUncached(f, line, maxW, lines[n], 160);
        n++;
        p = rest;
    }
    return n;
}
}  // namespace

// Clip rectangles nest: a clip inside another is cut down to the part they share, and unclip() goes back
// to the outer one.
namespace {
struct ClipRect { int x0, y0, x1, y1; };
ClipRect g_clips[8];
void applyClip() {
    flush();
    g_clipEmpty = false;
    if (g_clipDepth == 0) { C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0); return; }
    const ClipRect& c = g_clips[g_clipDepth - 1];
    // The GPU's scissor registers hold the last pixel inside, so a rectangle without area cannot be
    // expressed (at the screen edge it would wrap round to "everything"). Such a clip drops the quads instead.
    if (c.x1 <= c.x0 || c.y1 <= c.y0) { g_clipEmpty = true; return; }
    int sw = g_screen == TOP ? 400 : 320;
    // the framebuffer is rotated: fb_x = 240 - y, fb_y = screenW - x
    C3D_SetScissor(GPU_SCISSOR_NORMAL, 240 - c.y1, sw - c.x1, 240 - c.y0, sw - c.x0);
}
}  // namespace

void clip(float x, float y, float w, float h) {
    const Xf& t = X();
    float X0 = t.tx + t.s * x, Y0 = t.ty + t.s * y, X1 = X0 + t.s * w, Y1 = Y0 + t.s * h;
    int sw = g_screen == TOP ? 400 : 320;
    auto cl = [](float v, int hi) { int i = (int)floorf(v + 0.5f); return i < 0 ? 0 : i > hi ? hi : i; };
    ClipRect c = {cl(X0, sw), cl(Y0, 240), cl(X1, sw), cl(Y1, 240)};
    if (g_clipDepth > 0) {
        const ClipRect& o = g_clips[g_clipDepth - 1];
        if (c.x0 < o.x0) c.x0 = o.x0;
        if (c.y0 < o.y0) c.y0 = o.y0;
        if (c.x1 > o.x1) c.x1 = o.x1;
        if (c.y1 > o.y1) c.y1 = o.y1;
    }
    if (c.x1 < c.x0) c.x1 = c.x0;
    if (c.y1 < c.y0) c.y1 = c.y0;
    if (g_clipDepth < 8) g_clips[g_clipDepth++] = c;
    applyClip();
}

void unclip() {
    if (g_clipDepth > 0) g_clipDepth--;
    applyClip();
}

void cover(u32 color) {
    while (g_clipDepth > 0) unclip();
    g_sp = 0; g_stack[0] = {0, 0, 1, 1};
    rect(0, 0, g_screen == TOP ? 400.0f : 320.0f, 240, color);
}

}  // namespace gfx
