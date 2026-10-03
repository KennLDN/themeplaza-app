// Bottom (touch) screen: tab bar, type selector, the Collection grid or the Theme Plaza list,
// the button bar, and the overlays (busy dialog, More, keyboard, QR, toast).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "../core/sound.h"
#include "draw.h"

namespace ui {

using namespace gfx;

namespace {

const u32 INK = rgb(0x353d46), INK2 = rgb(0x6c7783), INK3 = rgb(0x9aa5af), WHITE = rgb(0xffffff), BLUE = rgb(0x1c63b0);

bool flash(char key, float dur = 0.11f) {
    const char* all = "abxylrseUDLR"; const char* p = strchr(all, key);
    return p && g.btnFlash[p - all].active(dur);
}

Spr glyphDk(char k) { return k == 'a' ? SPR_g_a_dk : k == 'b' ? SPR_g_b_dk : k == 'x' ? SPR_g_x_dk : SPR_g_y_dk; }

// ---------- header ----------
void head() {
    slice(SPR_head_bg, 0, 0, 320, 28);
    spr(SPR_g_l_wh, 6, 11);
    const float tx[2] = {29, 93}, tw[2] = {62, 80};
    for (int i = 0; i < 2; i++) {
        float a = g.tabAct[i].v(), top = 8 - 3 * a, h = 20 + 3 * a;
        if (a < 0.999f) slice(SPR_tab_off, tx[i], top, tw[i], h, 1);
        if (a > 0.001f) slice(SPR_tab_on, tx[i], top, tw[i], h, a);
        text(F11, tx[i] + tw[i] / 2, top, mixColor(rgb(0xd9e9f9), BLUE, a), SRC_NAMES[i], CENTER, 19 + 3 * a);
    }
    spr(SPR_g_r_wh, 177, 11);
    bool dn = flash('e');
    float oy = dn ? 2 : 0;
    slice(dn ? SPR_hbtn_dn : SPR_hbtn, 238, 5 + oy, 48, 18);
    float cw = 11 + 4 + (textWidth(F10, "Scan")), cx = 238 + ((48 - cw) / 2);
    spr(SPR_ic_qr_blue, cx, 5 + oy + 3);
    text(F10, cx + 15, 4 + oy, BLUE, "Scan", LEFT, 18);     // one pixel above the design's place, on purpose
    dn = flash('s'); oy = dn ? 2 : 0;
    slice(dn ? SPR_hbtn_dn : SPR_hbtn, 290, 5 + oy, 24, 18);
    spr(SPR_ic_dots_blue, 296, 5 + oy + 7);
}

// ---------- type selector and search chip ----------
void seg() {
    if (plaza() && !backend::online()) return;
    Rect r[3]; segLayout(r);
    const Spr icons[3] = {SPR_ic_type0, SPR_ic_type1, SPR_ic_type2};
    for (int i = 0; i < 3; i++) {
        // As in the browser: the fill changes at once (gradients do not blend there), while the text colour
        // and the ring take 0.15 s, the ring fading out on the old segment as it fades in on the new one.
        float on = g.typeOn[i].v(), ring = on * g.focusSel.v();
        bool cur = i == g.type;
        slice(cur ? SPR_tp_on : SPR_tp_off, r[i].x, r[i].y, r[i].w, 18, 1);
        if (ring > 0.01f) slice(cur ? SPR_tp_on_fc : SPR_tp_fc, r[i].x, r[i].y, r[i].w, 18, ring);
        u32 c = mixColor(INK2, WHITE, on);
        sprColor(icons[i], r[i].x + 7, r[i].y + 4, c);
        text(F11, r[i].x + 7 + 11 + 5, r[i].y, c, TYPE_NAMES[i], LEFT, 18);
    }
}

std::string chipText() {
    int nt = (int)g.findTags.size();
    if (!g.findQ.empty()) return g.findQ + (nt ? " +" + std::to_string(nt) : "");
    return std::to_string(nt) + (nt > 1 ? " tags" : " tag");
}

void chip() {
    if (!g.chipShown) return;
    Rect r = chipRect();
    char cut[64]; fit(F10P, chipText().c_str(), 74 - 8 - 4 - 8 - 6, cut, sizeof cut);
    float sc = anim::ease(anim::POP_CHIP, g.chipClock.frac(0.3f));
    push(0, 0, sc, 1, r.x + r.w / 2, r.y + 9);
    slice(SPR_chip_find, r.x, r.y, r.w, 18);
    float tw = text(F10P, r.x + 8, r.y, WHITE, cut, LEFT, 18);
    spr(SPR_ic_x_w, r.x + 8 + tw + 4, r.y + 3);
    pop();
}

// ---------- markers (tick order / installed) ----------
int wantMark(Item* it) {
    if (!it || it->more) return 0;
    int q = tickIndex(it);
    if (q >= 0) return 1 + q;
    return isInst(it) ? 100 : 0;
}

float markScale(Item* it, int want) {
    auto& m = g.marks[it];
    if (m.first != want) { m.first = want; m.second.start(); }
    return anim::ease(anim::POP_CHIP, m.second.frac(0.3f));
}

// ---------- Collection grid ----------
void tilePos(int i, float& x, float& y) { x = 11 + (i % 5) * 61; y = 58 + ((i % 10) / 5) * 59; }

void drawTile(Item* it, float x, float y, float s, bool marksOnly) {
    // s: 0..1 how selected the tile is; the selected tile grows to the cursor's 58px
    push(0, 0, 1 + 0.0741f * s, 1, x + 27, y + 27);
    if (!marksOnly) {
        if (it->more) spr(s > 0.5f ? SPR_tile_more_sel : SPR_tile_more, x, y);
        else {
            spr(s > 0.5f ? SPR_tile_sel : SPR_tile, x, y);
            backend::wantIcon(it);
            if (it->icon48.ok) image(&it->icon48.tex, 0, 0, 48, 48, x + 3, y + 3, 48, 48);
        }
    }
    int want = wantMark(it);
    if ((marksOnly || s > 0.5f) && want) {
        float ms = markScale(it, want);
        Spr sp = want == 100 ? SPR_mk_ok : (Spr)(SPR_mk_q1 + (want - 1));
        float mw = g_spr[sp].bw;
        push(0, 0, ms, 1, x + 54 + 5 - mw / 2, y - 5 + 8.5f);
        spr(sp, x + 54 + 5 - mw, y - 5);
        pop();
    } else if (!want) markScale(it, 0);
    pop();
}

void grid() {
    int sel = getSel(), page = sel / 10, n = (int)g.view.size();
    if (n == 0) return;
    int first = page * 10, last = std::min(n, first + 10);
    float sx, sy; tilePos(sel, sx, sy);
    if (!g.curPlaced) { g.curX.set(sx); g.curY.set(sy); g.curPlaced = true; }
    g.curX.go(sx, 0.11f, anim::EASE_OUT); g.curY.go(sy, 0.11f, anim::EASE_OUT);
    for (int i = first; i < last; i++) g.tileSel[i - first].go(i == sel ? 1 : 0, 0.16f, anim::POP_TILE);

    for (int i = first; i < last; i++) {
        if (i == sel) continue;
        float x, y; tilePos(i, x, y);
        drawTile(g.view[i], x, y, g.tileSel[i - first].v(), false);
    }
    // cursor
    float cx = g.curX.v() - 2, cy = g.curY.v() - 2, idle = g.focusSel.v();
    float p = 0.5f - 0.5f * cosf(g.T * TAU / 2.2f);
    if (idle < 1) { spr(SPR_cur_a, cx, cy, 1 - idle); spr(SPR_cur_b, cx, cy, p * (1 - idle)); }
    if (idle > 0) spr(SPR_cur_idle, cx, cy, idle);
    // the selected tile, above the cursor
    if (sel >= first && sel < last) drawTile(g.view[sel], sx, sy, g.tileSel[sel - first].v(), false);
    for (int i = first; i < last; i++) {
        if (i == sel) continue;
        float x, y; tilePos(i, x, y);
        drawTile(g.view[i], x, y, 0, true);
    }
    // name label: always above the grid, following the selected column
    Item* it = g.view[sel];
    char name[96]; fit(F11, it->name.c_str(), 200, name, sizeof name);
    float bw = (textWidth(F11, name)) + 18;
    float bx = std::max(6.0f, std::min(314 - bw, g.curX.v() + 27 - bw / 2));
    slice(SPR_bub, bx, 32, bw, 18);
    if ((sel % 10) / 5 == 0) spr(SPR_bub_tip, g.curX.v() + 23 - 6, 32);
    text(F11, bx + 9, 32, INK, name, LEFT, 17);
    // page dots
    int pages = (n + 9) / 10;
    if (pages > 1 && !findOn()) {
        if (pages <= 8) {
            float x = 320 - 10 - (pages * 6 + (pages - 1) * 5);
            for (int i = 0; i < pages; i++) spr(i == page ? SPR_pdot_on : SPR_pdot_off, x + i * 11, 11);
        } else {
            // a large Collection has more pages than dots fit beside the selector: show the page number instead
            char t[24]; snprintf(t, sizeof t, "%d / %d", page + 1, pages);
            text(F10P, 310, 5, INK2, t, RIGHT, 18);
        }
    }
}

// ---------- Theme Plaza list ----------
void skeleton() {
    for (int i = 0; i < 4; i++) {
        float y = 30 + i * 27;
        slice(SPR_row, 8, y, 292, 24);
        rect(8, y, 24, 24, rgb(0xdfe6ec));
        slice(SPR_skel, 8 + 33, y + 6, 70, 9);
        slice(SPR_skel, 8 + 292 - 30 - 70, y + 7, 70, 9);
    }
    slice(SPR_scroll_track, 307, 30, 6, 144);
    spr(SPR_scroll_thumb, 307, 30);
}

void list() {
    int n = (int)g.view.size(), sel = getSel();
    if ((int)g.rows.size() != n) g.rows.resize(n);
    int start = std::max(0, std::min(sel - 1, n - 4));
    float y = 30;
    for (int i = 0; i < n; i++) {
        RowAnim& r = g.rows[i];
        float ty;
        if (i < start) ty = 30 - (start - i) * 27;
        else { ty = i == sel ? y + 5 : y; y += i == sel ? 62 : 27; }
        bool vis = i >= start && i < start + 4;
        if (!r.placed) { r.y.set(ty); r.sel.set(i == sel ? 1 : 0); r.vis.set(vis ? 1 : 0); r.placed = true; }
        r.y.go(ty, 0.13f, anim::EASE_OUT);
        r.sel.go(i == sel ? 1 : 0, 0.13f, anim::EASE_OUT);
        r.vis.go(vis ? 1 : 0, 0.13f, anim::EASE);
    }
    float idle = g.focusSel.v();
    // unselected rows first, the selected one on top
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < n; i++) {
            if ((i == sel) != (pass == 1)) continue;
            RowAnim& r = g.rows[i];
            float top = r.y.v(), s = r.sel.v();
            if (top < -30 || top > 185) continue;
            float alpha = r.vis.v();
            if (alpha <= 0.01f) continue;
            Item* it = g.view[i];
            backend::wantIcon(it);
            float left = 8 - 2 * s, w = 292 + 4 * s, h = 24 + 28 * s;
            push(0, 0, 1, alpha);
            if (s < 0.999f) slice(SPR_row, left, top, w, h, 1);
            if (s > 0.01f) {
                slice(SPR_row_sel, left, top, w, h, s * (1 - idle));
                if (idle > 0.01f) slice(SPR_row_sel_idle, left, top, w, h, s * idle);
            }
            float isz = 24 + 24 * s;
            if (s < 0.02f && it->icon24.ok) image(&it->icon24.tex, 0, 0, 24, 24, left, top, 24, 24);
            else if (it->icon48.ok) image(&it->icon48.tex, 0, 0, 48, 48, left + 2 * s, top + 2 * s, isz, isz);
            else rect(left + 2 * s, top + 2 * s, isz, isz, rgb(0xdfe6ec));
            // name and creator
            char cut[96];
            float au1w = textWidth(F10P, it->author.c_str());
            // One name that grows from 12 to 15 px as the row opens, as the browser interpolates the font size:
            // the nearer of the two font sizes, scaled.
            {
                bool big = s > 0.5f;
                if (big) fit(F15, it->name.c_str(), w - 59 - 44, cut, sizeof cut);
                else fit(F12, it->name.c_str(), w - 33 - 30 - au1w - 26, cut, sizeof cut);
                push(left + 33 + 26 * s, top + 6 + 2 * s, (12 + 3 * s) / (big ? 15.0f : 12.0f), 1);
                text(big ? F15 : F12, 0, 0, INK, cut);
                pop();
            }
            // the creator: at the right edge of a closed row, under the name in the open one
            if (s < 0.98f) { push(0, 0, 1, 1 - s); text(F10P, left + w - 30, top + 7, INK3, it->author.c_str(), RIGHT); pop(); }
            if (s > 0.02f) { push(0, 0, 1, s); text(F11P, left + 59, top + 31, INK2, it->author.c_str()); pop(); }
            // markers: tick order, then "on your SD card"
            int q = tickIndex(it);
            float mx = left + w - (5 + 3 * s), my = top + 5 + 3 * s;
            int want = (q >= 0 ? 1 + q : 0) + (it->onSD ? 100 : 0);
            float ms = markScale(it, want);
            if (it->onSD) { mx -= 15; push(0, 0, ms, 1, mx + 7.5f, my + 7.5f); spr(SPR_badge_ok, mx, my); pop(); mx -= 3; }
            if (q >= 0) { Spr sp = (Spr)(SPR_badge_q1 + (q < 24 ? q : 23)); float bw = g_spr[sp].bw; mx -= bw; push(0, 0, ms, 1, mx + bw / 2, my + 7.5f); spr(sp, mx, my); pop(); }
            // download progress along the bottom edge
            if (g.dlItem == it && g.job && g.jobShown > 0.004f) {
                // the bar follows the row's rounded bottom corners
                clip(left, top + h - 4, w * g.jobShown, 4);
                slice(s > 0.5f ? SPR_row_bar : SPR_row_bar6, left, top + h - 4, w, 4);
                unclip();
            }
            pop();
        }
    slice(SPR_scroll_track, 307, 30, 6, 144);
    g.scrollY.go(n > 1 ? (float)sel / (n - 1) * 116 : 0, 0.13f);
    spr(SPR_scroll_thumb, 307, 30 + g.scrollY.v());
}

void emptyState(Spr icon, float top, const char* h, const char* p1, const char* p2) {
    float y = top;
    if (icon != SPR_COUNT) { spr(icon, 160 - g_spr[icon].bw / 2.0f, y); y += g_spr[icon].bh; }
    text(F15, 160, y + 12, INK, h, CENTER);
    if (p1) text(F11P, 160, y + 12 + 15 + 8, INK2, p1, CENTER, 16);
    if (p2) text(F11P, 160, y + 12 + 15 + 8 + 16, INK2, p2, CENTER, 16);
}

void pane() {
    float f = g.paneClock.frac(0.2f);
    float tx = 0, sc = 1, a = 1;
    if (g.paneDir == DIR_F) a = anim::ease(anim::EASE, f);
    else { float e = anim::ease(anim::EASE_OUT, f); tx = (g.paneDir == DIR_R ? 26 : -26) * (1 - e); sc = 0.96f + 0.04f * e; a = e; }
    clip(0, 28, 320, 179);
    push(0, 28);
    push(tx, 0, sc, a, 160, 89.5f);
    if (plaza() && !backend::online()) emptyState(SPR_ic_wifi_g, 30, "Can't reach Theme Plaza", "Check Internet Settings in System Settings,", "then press A to try again.");
    else if (plaza() && !backend::store().ready) skeleton();
    else if (plaza() && backend::store().failed) emptyState(SPR_ic_wifi_g, 30, "Can't load Theme Plaza", "The site did not answer as expected.", "Press A to try again.");
    else if (plaza() && g.view.empty() && findOn()) emptyState(SPR_COUNT, 30 + 32, "Nothing matches", "Press X to change your search.", nullptr);
    else if (plaza() && g.view.empty()) emptyState(SPR_COUNT, 30 + 32, "Nothing here yet", nullptr, nullptr);
    else if (plaza()) list();
    else grid();
    pop();
    pop();
    unclip();
}

// ---------- footer ----------
void foot() {
    slice(SPR_foot_bg, 0, 207, 320, 33);
    auto spec = footSpec();
    std::vector<Rect> rc; footLayout(spec, rc);
    for (size_t i = 0; i < spec.size(); i++) {
        const FootBtn& b = spec[i];
        bool pri = b.key == 'a', dn = flash(b.key), act = b.key == 'x' && findOn() && !b.off;
        float x = rc[i].x, w = rc[i].w, y = 211 + (dn ? 2 : 0);
        push(0, 0, 1, b.off ? 0.45f : 1);
        if (pri) {
            slice(dn ? SPR_btn_pri_dn : SPR_btn_pri, x, y, w, 23);
            Font fnt = b.label.size() > 8 ? F12 : F13;
            float tw = (textWidth(fnt, b.label.c_str())), cw = 13 + 4 + tw, cx = x + 5 + ((w - 11 - cw) / 2);
            spr(SPR_g_a_pri, cx, y + 5);
            textShadow(fnt, cx + 17, y, WHITE, rgb(0x96320a, 0.55f), b.label.c_str(), LEFT, 23);
        } else {
            slice(act ? SPR_btn_act : dn ? SPR_btn_dn : SPR_btn, x, y, w, 23);
            spr(act ? SPR_g_x_wh : glyphDk(b.key), x + 5, y + 5);
            text(F11, x + 5 + 13 + 4, y, act ? WHITE : INK, b.label.c_str(), LEFT, 23);
        }
        pop();
    }
}

// ---------- overlays ----------
void hint(float right, float y, std::initializer_list<std::pair<Spr, const char*>> items) {
    // right-aligned row of "glyph label" pairs
    float total = 0; int n = 0;
    for (auto& it : items) { total += g_spr[it.first].bw + 3 + (textWidth(F10P, it.second)); n++; }
    total += (n - 1) * 8;
    float x = right - total;
    for (auto& it : items) {
        spr(it.first, x, y); x += g_spr[it.first].bw + 3;
        x += (text(F10P, x, y, INK2, it.second, LEFT, 13)) + 8;
    }
}

void busyModal() {
    float d = anim::ease(anim::EASE, g.overClock.frac(0.15f));
    rect(0, 0, 320, 240, rgb(0x14283c, 0.45f * d));
    float sc = anim::ease(anim::POP_MODAL, g.overClock.frac(0.28f));
    push(0, 0, sc, 1, 160, 120);
    spr(SPR_modal, 30, 74);
    int stepN = (int)(g.T / 0.1f) % 8;
    sprEx(SPR_ros_11, 160, 98, 1, stepN * TAU / 8, rgb(0x2f86d6), true);
    char cut[96]; fit(F13, g.jobText.c_str(), 236, cut, sizeof cut);
    text(F13, 160, 118, INK, cut, CENTER);
    spr(SPR_pb_track, 54, 142);
    float w = 212 * g.jobShown;
    if (w >= 8) slice(SPR_pb_fill, 54, 142, w, 8);
    else if (w > 0.5f) { clip(54, 142, w, 8); slice(SPR_pb_fill, 54, 142, 8, 8); unclip(); }
    pop();
}

float sheetOffset() { return 240 * (1 - anim::ease(anim::SHEET, g.overClock.frac(0.2f))); }

void moreSheet() {
    rect(0, 0, 320, 240, rgb(0x14283c, 0.45f * anim::ease(anim::EASE, g.overClock.frac(0.15f))));
    push(0, sheetOffset());
    slice(SPR_sheet, 0, 28, 320, 212);
    text(F13, 14, 39, INK, "More");
    hint(320 - 12, 38, {{SPR_g_b_dk, "Close"}, {SPR_g_a_dk, "Choose"}});
    auto items = menuItems();
    // seven entries fit; with more, the list scrolls to keep the selected one in view
    g.menuScroll.go(menuFirst((int)items.size()) * 25.0f, 0.12f, anim::EASE_OUT);
    float scroll = g.menuScroll.v();
    // From just under the heading. While the list is not scrolled nothing can run into the heading, and the
    // first entry's selection glow is given its full height.
    float clipTop = scroll < 0.5f ? 28 + 24 : 28 + 29;
    clip(0, clipTop, 320, 240 - clipTop);
    // the selected entry goes on last: its ring lies over the edges of the entries above and below
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < (int)items.size(); i++) {
            if ((i == g.menu) != (pass == 1)) continue;
            float y = 28 + 34 + i * 25 - scroll;
            if (y < 28 || y > 240) continue;
            slice(SPR_mi, 10, y, 300, 24);
            if (i == g.menu) slice(SPR_mi_sel, 10, y, 300, 24);
            text(F12, 22, y, items[i].red ? rgb(0xc2382c) : INK, items[i].label, LEFT, 23);
            if (items[i].act == 3) text(F12, 298, y, BLUE, sound::trackName(sound::track()), RIGHT, 23);
        }
    unclip();
    pop();
}

void keyboardSheet() {
    push(0, sheetOffset());
    gradV(0, 0, 320, 240, rgb(0xe9eff3), rgb(0xd9e2e9));
    gradV(0, 0, 320, 26, rgb(0x1f6cc2), rgb(0x1a61b3));
    rect(0, 26, 320, 1, rgb(0x124a8c));
    text(F12, 12, 0, WHITE, plaza() ? "Search Theme Plaza" : "Search your Collection", LEFT, 25);
    auto& keys = keyDefs();
    for (size_t i = 0; i < keys.size(); i++) {
        const KeyDef& k = keys[i];
        bool fn = strlen(k.k) > 1 || k.k[0] == ' ';
        bool dn = g.keyFlash[i].active(0.1f);
        float y = k.y + (dn ? 2 : 0);
        slice(dn ? SPR_key_dn : fn ? SPR_key_fn : SPR_key, k.x, y, k.w, 27);
        if (!strcmp(k.k, "bksp")) { float cx = k.x + (k.w - 33) / 2.0f; spr(SPR_g_x_dk, cx, y + 7); spr(SPR_ic_bksp, cx + 18, y + 9); }
        else if (k.k[0] == ' ') { float tw = (textWidth(F11, "space")), cx = k.x + ((k.w - 18 - tw) / 2); spr(SPR_g_y_dk, cx, y + 7); text(F11, cx + 18, y + 1, INK, "space", LEFT, 25); }
        else text(fn ? F11 : F14, k.x + k.w / 2.0f, y + 1, INK, k.k, CENTER, 25);
    }
    bool dn = flash('b');
    float y = 206 + (dn ? 2 : 0);
    slice(dn ? SPR_btn_dn : SPR_btn, 8, y, 84, 23);
    float tw = (textWidth(F12, "Close")), cx = 8 + ((84 - 18 - tw) / 2);
    spr(SPR_g_b_dk, cx, y + 5); text(F12, cx + 18, y, INK, "Close", LEFT, 23);
    dn = flash('s'); y = 206 + (dn ? 2 : 0);
    slice(dn ? SPR_btn_pri_dn : SPR_btn_pri, 206, y, 106, 23);
    tw = (textWidth(F13, "Search")); cx = 206 + ((106 - 35 - tw) / 2);
    spr(SPR_g_start_pri, cx, y + 5); textShadow(F13, cx + 35, y, WHITE, rgb(0x96320a, 0.55f), "Search", LEFT, 23);
    pop();
}

void qrSheet() {
    rect(0, 0, 320, 240, rgb(0x14283c, 0.45f * anim::ease(anim::EASE, g.overClock.frac(0.15f))));
    push(0, sheetOffset());
    slice(SPR_sheet, 0, 28, 320, 212);
    text(F13, 14, 39, INK, "Scan a QR code");
    hint(320 - 12, 38, {{SPR_g_b_dk, "Back"}});
    spr(SPR_ic_qr_grey44, 138, 74);
    text(F15, 160, 74 + 44 + 14, INK, "Point the outer camera at a code", CENTER);
    text(F11P, 160, 74 + 44 + 14 + 15 + 8, INK2, "Theme Plaza codes download to your SD card.", CENTER, 16);
    pop();
}

void previewBottom() {
    Item* it = cur();
    if (!it) return;
    float a = anim::ease(anim::EASE, g.modeClock.frac(0.22f));
    drawPreviewBottom(it, 0, 0, 1, a);
    float tw = (textWidth(F11, "Back")), w = 6 + 13 + 5 + tw + 10, x = 320 - 8 - w, y = 240 - 8 - 22;
    slice(SPR_pvhint, x, y, w, 22);
    spr(SPR_g_b_dk, x + 6, y + 4.5f);
    text(F11, x + 6 + 18, y, INK, "Back", LEFT, 22);
}

void toastDraw() {
    if (g.toastText.empty()) return;
    // a message too long for one line takes two, and stays up longer
    char lines[2][160];
    int n = wrap(F11, g.toastText.c_str(), 280, 2, lines);
    if (n < 1) return;
    float t = g.toastClock.t / (n > 1 ? 4.2f : 2.4f);
    if (t >= 1) return;
    float a = 1, dy = 0;
    if (t < 0.08f) { a = t / 0.08f; dy = -8 * (1 - a); }
    else if (t > 0.85f) { a = 1 - (t - 0.85f) / 0.15f; dy = -4 * (1 - a); }
    float tw = textWidth(F11, lines[0]);
    if (n > 1) tw = std::max(tw, textWidth(F11, lines[1]));
    float w = tw + 24, x = (160 - w / 2), h = n > 1 ? 36 : 22, y = 181 + 22 - h;
    push(0, dy, 1, a);
    slice(SPR_toast, x, y, w, h);
    for (int i = 0; i < n; i++) text(F11, 160, y + (n > 1 ? 3 + i * 15 : 0), WHITE, lines[i], CENTER, n > 1 ? 15 : 22);
    pop();
}

}  // namespace

// ---------- layout shared with touch handling ----------
void footLayout(const std::vector<FootBtn>& spec, std::vector<Rect>& out) {
    float x = 7;
    out.clear();
    for (auto& b : spec) {
        float w = b.key == 'a' ? 313 - x : 5 + 13 + 4 + (textWidth(F11, b.label.c_str())) + 6;
        out.push_back({x, 211, w, 23});
        x += w + 5;
    }
}

void segLayout(Rect out[3]) {
    float x = 9;
    for (int i = 0; i < 3; i++) {
        float w = 7 + 11 + 5 + (textWidth(F11, TYPE_NAMES[i])) + 7;
        out[i] = {x, 33, w, 18};
        x += w + 4;
    }
}

Rect chipRect() {
    char cut[64]; fit(F10P, chipText().c_str(), 74 - 8 - 4 - 8 - 6, cut, sizeof cut);
    float w = 8 + (textWidth(F10P, cut)) + 4 + 8 + 6;
    return {320 - 8 - w, 33, w, 18};
}

void drawBottom() {
    // the full preview fades in over the screen as it was; once it covers everything, only it is drawn
    bool pv = g.mode == MODE_PREVIEW;
    if (pv && g.modeClock.frac(0.22f) >= 1) { previewBottom(); return; }
    gradV(0, 0, 320, 240, rgb(0xe9eff3), rgb(0xdce6ee));
    sprEx(SPR_ros_58, 284, 72, 1, -g.T * TAU / 120, rgb(0x2f86d6, 0.04f), true);
    sprEx(SPR_ros_34, 12, 90, 1, g.T * TAU / 80, rgb(0x2f86d6, 0.055f), true);
    sprEx(SPR_ros_20, 138, 170, 1, g.T * TAU / 80, rgb(0x2f86d6, 0.055f), true);
    pane();
    seg();
    chip();
    head();
    foot();
    if (pv) { previewBottom(); return; }
    if (g.job && g.busyModal) busyModal();
    else if (g.mode == MODE_MORE) moreSheet();
    else if (g.mode == MODE_SEARCH) keyboardSheet();
    else if (g.mode == MODE_QR) qrSheet();
    toastDraw();
}

}  // namespace ui
