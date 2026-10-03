// The preview of a theme: its own wallpaper under a simplified HOME Menu (status bar, icon tiles,
// bottom bar) tinted with the theme's colours.
#include "draw.h"

namespace ui {

using namespace gfx;

static u32 withAlpha(u32 abgr, float a) { return (abgr & 0x00ffffff) | ((u32)(a * 255.0f + 0.5f) << 24); }
static const u32 WHITE = 0x00ffffff, BLACK = 0;

void drawPreviewTop(Item* it, float x, float y, float scale, float alpha) {
    push(x, y, scale, alpha);
    // cards show the half-size picture pixel for pixel; full-screen preview uses the full-size one once it is loaded
    bool full = scale > 0.75f && it->fullTop.ok;
    Tex& top = full ? it->fullTop : it->wallTop;
    float k = full ? 1.0f : 2.0f;
    if (top.ok) {
        // theme wallpapers are 412 wide for the 3D effect; show the middle 400
        float off = top.w * k > 400 ? (top.w * k - 400) / 2.0f : 0;
        image(&top.tex, off / k, 0, 400 / k, 240 / k, 0, 0, 400, 240);
    } else rect(0, 0, 400, 240, it->colBar);
    if (it->kind == K_THEME) {
        rect(0, 0, 400, 22, withAlpha(it->colBar, 0.88f));
        rect(0, 22, 400, 1, withAlpha(BLACK, 0.18f));
        sprColor(SPR_hm_sig, 8, 6, withAlpha(WHITE, 1));
        sliceColor(SPR_hm_pill6, 150, 8, 34, 6, withAlpha(WHITE, 0.9f));
        sliceColor(SPR_hm_pill6, 190, 8, 60, 6, withAlpha(WHITE, 0.9f));
        sprColor(SPR_hm_batt, 368, 6, withAlpha(WHITE, 0.95f));
        rect(370, 8, 13, 7, it->colBar | 0xff000000);
    }
    pop();
}

void drawPreviewBottom(Item* it, float x, float y, float scale, float alpha) {
    push(x, y, scale, alpha);
    bool full = scale > 0.75f && it->fullBot.ok;
    Tex& bot = full ? it->fullBot : it->wallBot;
    float k = full ? 1.0f : 2.0f;
    if (bot.ok) image(&bot.tex, 0, 0, 320 / k, 240 / k, 0, 0, 320, 240);
    else if (it->wallTop.ok) image(&it->wallTop.tex, 20, 0, 160, 120, 0, 0, 320, 240);
    else rect(0, 0, 320, 240, it->colBar);
    if (it->kind == K_THEME) {
        rect(0, 0, 320, 240, withAlpha(BLACK, 0.06f));
        rect(0, 0, 320, 24, withAlpha(it->colBar, 0.9f));
        const int bx[4] = {10, 44, 250, 284};
        for (int b : bx) sprColor(SPR_hm_btn, b, 4, withAlpha(WHITE, 0.9f));
        static const char shapes[8] = {'c', 's', 't', 'c', 's', 't', 's', 'c'};
        for (int row = 0; row < 2; row++)
            for (int i = 0; i < 4; i++) {
                float tx = 28 + i * 68, ty = 52 + row * 70; int k = row * 4 + i;    // 52: the grid sits midway between the two bars
                sprColor(SPR_hm_tile, tx + 2, ty + 3, withAlpha(BLACK, 0.2f));
                sprColor(SPR_hm_tile, tx, ty, it->colTile | 0xff000000);
                float cx = tx + 30, cy = ty + 30; u32 gc = withAlpha(it->colGlyph, 0.55f + (k % 3) * 0.2f);
                if (shapes[k] == 'c') sprColor(SPR_hm_circle, cx - 15, cy - 15, gc);
                else if (shapes[k] == 's') sprColor(SPR_hm_square, cx - 14, cy - 14, gc);
                else sprColor(SPR_hm_tri, cx - 16, cy - 16, gc);
            }
        sprColor(SPR_hm_cursor, 92, 48, withAlpha(WHITE, 1));
        sprColor(SPR_hm_bar, 0, 204, withAlpha(it->colBar, 0.94f));
        sprColor(SPR_hm_pill14, 18, 214, withAlpha(WHITE, 0.9f));
        sprColor(SPR_hm_pill14, 262, 214, withAlpha(WHITE, 0.9f));
    }
    pop();
}

}  // namespace ui
