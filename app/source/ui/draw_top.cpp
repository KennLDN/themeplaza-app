// Top screen: the stage. Preview cards and caption for the selected item, or a message, the search
// form, a full-size preview, or the camera view.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include "draw.h"

namespace ui {

using namespace gfx;

namespace {

const u32 SH45 = rgb(0x083264, 0.45f), SH35 = rgb(0x083264, 0.35f), SH30 = rgb(0x083264, 0.30f);
const u32 WHITE = rgb(0xffffff);

std::string fmtCount(int n) {
    char b[24];
    if (n >= 1000) { snprintf(b, sizeof b, "%.1fk", n / 1000.0); std::string s = b; size_t p = s.find(".0k"); if (p != std::string::npos) s.erase(p, 2); return s; }
    snprintf(b, sizeof b, "%d", n);
    return b;
}

void background() {
    gradV(0, 0, 400, 149, rgb(0x4dabf2), rgb(0x2779cf));
    gradV(0, 149, 400, 91, rgb(0x2779cf), rgb(0x1f6cc2));
    u32 c = rgb(0xffffff, 0.11f);
    sprEx(SPR_ros_14, 220, 12, 1, g.T * TAU / 60, c, true);
    sprEx(SPR_ros_44, 394, 236, 1, -g.T * TAU / 90, c, true);
    sprEx(SPR_ros_18, 2, 158, 1, g.T * TAU / 60, c, true);
}

void status() {
    // the Theme Plaza mark, then the text 6px after it (the design's gap); the mark's svg is 11 * 1275 / 995.5 wide
    sprColor(SPR_tp_mark_s, 16, 8, WHITE);
    float x = 16 + 11.0f * 1275 / 995.5f + 6;
    x += textShadow(F11, x, 8, WHITE, SH35, SRC_NAMES[g.src]) + 6;
    std::string t = statusText();
    char cut[160]; fit(F11, t.c_str(), 355 - 44 - x, cut, sizeof cut);
    textShadow(F11, x, 8, rgb(0xffffff, 0.72f), rgb(0x083264, 0.25f), cut);
    time_t now = time(nullptr); struct tm* tmv = localtime(&now);
    char clock[8]; snprintf(clock, sizeof clock, "%d:%02d", tmv->tm_hour, tmv->tm_min);
    textShadow(F11, 355, 8, WHITE, SH35, clock, RIGHT);
    spr(SPR_ic_batt, 362, 8);
    u8 level = 5; PTMU_GetBatteryLevel(&level);
    rect(364.5f, 10.5f, 11.0f * level / 5.0f, 6, WHITE);
}

// A message with the Theme Plaza mark (mark = true) or another icon above it (empty states, loading, offline).
void promo(Spr icon, bool mark, const char* big, const char* l1, const char* l2) {
    float top = 44;
    // the mark's svg is 52 * 1275 / 1176.5 wide, centred on the screen as an inline block
    if (mark) { spr(SPR_tp_mark_l, 200 - 26.0f * 1275 / 1176.5f, top); top += 52; }
    else { spr(icon, 178, top); top += 36 - 2; }   // the icon is a block of its own: 2px less space under it
    textShadow(F22, 200, top + 12, WHITE, SH45, big, CENTER, 26);
    if (l1) text(F12, 200, top + 12 + 26 + 8, rgb(0xeaf4fd), l1, CENTER, 17);
    if (l2) text(F12, 200, top + 12 + 26 + 8 + 17, rgb(0xeaf4fd), l2, CENTER, 17);
}

struct Chip { Spr icon; std::string text; bool on; };

void caption(Item* it) {
    // chips, right-aligned
    std::vector<Chip> chips;
    if (it->bgm) chips.push_back({SPR_ic_note_w, "BGM", false});
    if (plaza()) {
        if (it->statsReady) {   // these arrive a moment after the row itself
            chips.push_back({SPR_ic_down_w, fmtCount(it->downloads), false});
            chips.push_back({SPR_ic_heart_w, fmtCount(it->likes), false});
        }
        if (it->onSD && !isInst(it)) chips.push_back({SPR_ic_check_g, "On SD", true});
    }
    if (isInst(it)) {
        auto& in = backend::installedThemes();
        std::string label = "Installed";
        if (it->kind == K_THEME && in.size() > 1)
            for (size_t i = 0; i < in.size(); i++) if (in[i] == it || (!it->path.empty() && in[i]->path == it->path)) label = "Shuffle " + std::to_string(i + 1) + "/" + std::to_string(in.size());
        chips.push_back({SPR_ic_check_g, label, true});
    }
    float right = 384;
    for (int i = (int)chips.size() - 1; i >= 0; i--) {
        const Chip& c = chips[i];
        float iw = g_spr[c.icon].bw, ih = g_spr[c.icon].bh;
        float w = 6 + iw + 4 + (textWidth(F10P, c.text.c_str())) + 7, x = right - w;
        float sc = 1;
        if (c.on && g.chipPop) sc = anim::ease(anim::POP_CHIP, g.stageClock.frac(0.45f));
        push(0, 0, sc, 1, x + w / 2, 162 + 8.5f);
        slice(c.on ? SPR_chip_on : SPR_chip, x, 162, w, 17);
        spr(c.icon, x + 6, 162 + floorf((17 - ih) / 2 + 0.5f));   // centred; a half pixel goes down, as the browser snaps it
        text(F10P, x + 6 + iw + 4, 162, c.on ? rgb(0x1f8a53) : WHITE, c.text.c_str(), LEFT, 17);
        pop();
        right = x - 5;
    }
    char cut[160];
    fit(F19, it->name.c_str(), right - 16 - 3, cut, sizeof cut);
    textShadow(F19, 16, 159, WHITE, SH45, cut, LEFT, 20);
    std::string by;
    const char* kind = it->kind == K_BADGE ? "Badge set" : it->kind == K_SPLASH ? "Boot splash" : "Theme";
    if (!it->author.empty()) by = (it->kind == K_THEME ? std::string("by ") : std::string(kind) + " by ") + it->author; else by = kind;
    // a catalogue set's size is only known once its preview has been read or it is on the SD card
    if (it->kind == K_BADGE && it->badgeCount > 0) by += " \xc2\xb7 " + std::to_string(it->badgeCount) + (it->badgeCount == 1 ? " badge" : " badges");
    fit(F11, by.c_str(), 368, cut, sizeof cut);
    textShadow(F11, 16, 183, rgb(0xffe58a), SH35, cut);
    spr(SPR_dots_line, 16, 200);
    static const char* const fallback[3] = {"", "Shown for a moment each time the console starts.", "Badges to decorate your HOME Menu with."};
    const std::string& d = it->desc.empty() ? std::string(fallback[it->kind]) : it->desc;
    char lines[2][160];
    int n = wrap(F11, d.c_str(), 368, 2, lines);
    for (int i = 0; i < n; i++) textShadow(F11, 16, 206 + i * 14, rgb(0xeaf4fd), SH30, lines[i], LEFT, 14);
}

void spinner(float cx, float cy, u32 color) {
    int stepN = (int)(g.T / 0.1f) % 8;
    sprEx(SPR_ros_11, cx, cy, 1, stepN * TAU / 8, color, true);
}

// One preview card: white frame, picture (or loading stripes) inside. w is the picture width (200 or 160).
void card(Item* it, bool bottom, float x, float w, float delay) {
    float e = 1, a = 1, tx = 0, ty = 0, sc = 1;
    float f = g.stageClock.frac(0.24f, delay);
    e = anim::ease(anim::POP_CARD, f); a = e > 1 ? 1 : e;
    if (g.stageDir == DIR_F) ty = 5 * (1 - e);
    else { tx = (g.stageDir == DIR_R ? 26 : -26) * (1 - e); sc = 0.96f + 0.04f * e; }
    push(tx, ty, sc, a, x + w / 2, 30 + 60);
    if (it->previewReady) {
        if (bottom) drawPreviewBottom(it, x, 30, 0.5f); else drawPreviewTop(it, x, 30, 0.5f);
    } else {
        float off = fmodf(g.T * 53.0f, 26.4797f);
        sprPart(SPR_shim, off, 0, w, 120, x, 30);
        spinner(x + w / 2, 30 + 60, rgb(0x8fa6ba));
    }
    slice(SPR_cw, x - 3, 27, w + 6, 126);
    pop();
}

void badgeCard(Item* it) {
    float f = g.stageClock.frac(0.24f);
    float e = anim::ease(anim::POP_CARD, f), a = e > 1 ? 1 : e;
    // the same entrance as the theme cards: up from below on a plain change, in from the side when moving left or right
    if (g.stageDir == DIR_F) push(0, 5 * (1 - e), 1, a);
    else push((g.stageDir == DIR_R ? 26 : -26) * (1 - e), 0, 0.96f + 0.04f * e, a, 200, 90);
    if (!it->previewReady && plaza() && it->badgeArt.empty()) {
        float off = fmodf(g.T * 53.0f, 26.4797f);
        sprPart(SPR_shim, off, 0, 184, 120, 16, 30); sprPart(SPR_shim, off, 0, 184, 120, 200, 30);
        spinner(200, 90, rgb(0x8fa6ba));
    } else {
        spr(SPR_card_bd, 16, 30);
        int n = (int)it->badgeArt.size(); if (n > 6) n = 6;
        float total = n * 46 + (n - 1) * 11, x = 200 - total / 2;
        for (int i = 0; i < n; i++) {
            Tex& t = it->badgeArt[i];
            bool odd = i % 2 == 0;   // CSS :nth-child counts from 1
            if (t.ok) imageRot(&t.tex, 0, 0, t.w, t.h, x + 23, 90 + (odd ? -7 : 9), 46, 46, (odd ? -5 : 6) * TAU / 360);
            x += 57;
        }
        backend::wantIcon(it);
        if (!n && it->icon48.ok) image(&it->icon48.tex, 0, 0, 48, 48, 176, 66, 48, 48);
    }
    slice(SPR_cw, 13, 27, 374, 126);
    pop();
}

void searchForm() {
    auto its = sfItems();
    std::vector<float> xs, ys, ws; sfLayout(xs, ys, ws);
    int nt = plaza() ? NTAGS : 0;
    float w = textShadow(F14, 16, 33, WHITE, SH45, plaza() ? "Search Theme Plaza" : "Search your Collection");
    text(F11, 16 + w + 6, 36, rgb(0xffffff, 0.7f), TYPE_NAMES[g.type]);
    spr(SPR_sfq, 16, 54);
    bool caretOn = fmodf(g.T, 1.0f) < 0.5f;
    if (g.sf.q.empty()) {
        if (caretOn) rect(29, 54 + 7, 2, 16, rgb(0xff7a45));
        text(F15, 32, 54, rgb(0x9aa5af), plaza() ? "Name, creator or tag" : "Name or creator", LEFT, 29);
    } else {
        float tw = text(F15, 28, 54, rgb(0x353d46), g.sf.q.c_str(), LEFT, 29);
        if (caretOn) rect(28 + tw + 1, 54 + 7, 2, 16, rgb(0xff7a45));
    }
    if (nt) text(F10P, 16, 95, rgb(0xcfe4f8), "Tags");
    text(F10P, 16, nt ? 163 : 100, rgb(0xcfe4f8), "Sort order \xc2\xb7 stays as you set it");
    int ord = plaza() ? g.order : g.sort;
    for (int i = 0; i < (int)its.size(); i++) {
        bool on = its[i].tag ? std::find(g.sf.tags.begin(), g.sf.tags.end(), its[i].k) != g.sf.tags.end() : ord == its[i].k;
        bool fc = g.sf.f == i;
        slice(on ? (fc ? SPR_sc_on_fc : SPR_sc_on) : (fc ? SPR_sc_fc : SPR_sc), xs[i], ys[i], ws[i], 19);
        text(F11, xs[i] + 9, ys[i], on ? rgb(0x1c63b0) : WHITE, its[i].label, LEFT, 19);
    }
    float x = 16; u32 hc = rgb(0xeaf4fd);
    // glyphs at 214; the 10px text line is centred on their 13px, which the browser puts at 216
    spr(SPR_g_a_wh, x, 214); x += 13 + 4; x += text(F10P, x, 216, hc, "Choose") + 12;
    spr(SPR_g_b_wh, x, 214); x += 13 + 4; x += text(F10P, x, 216, hc, "Close") + 12;
    spr(SPR_g_start_wh, x, 214); x += 30 + 4; text(F10P, x, 216, hc, "Search");
}

}  // namespace

void drawTop() {
    Item* it = cur();
    if (g.mode == MODE_PREVIEW && it) {
        float a = anim::ease(anim::EASE, g.modeClock.frac(0.22f));
        background();
        if (a < 1) status();     // the status line stays under the picture until it is covered
        drawPreviewTop(it, 0, 0, 1, a);
        return;
    }
    if (g.mode == MODE_QR) {
        // the camera picture fills the screen; everything outside the frame is darkened
        rect(0, 0, 400, 240, rgb(0x1d2329));
        if (g.camTex.ok && g.camSerial) image(&g.camTex.tex, 0, 0, 400, 240, 0, 0, 400, 240);
        u32 dim = rgb(0x000000, 0.35f);
        rect(0, 0, 400, 34, dim); rect(0, 206, 400, 34, dim); rect(0, 34, 114, 172, dim); rect(286, 34, 114, 172, dim);
        spr(SPR_cam_frame, 114, 34);
        float p = fmodf(g.T / 1.6f, 2.0f); if (p > 1) p = 2 - p;
        spr(SPR_cam_line, 134, 54 + 130 * anim::ease(anim::EASE_IN_OUT, p));
        return;
    }
    background();
    status();
    if (g.mode == MODE_SEARCH) {
        float e = anim::ease(anim::EASE_OUT, g.stageClock.frac(0.2f));
        push(0, 5 * (1 - e), 1, e); searchForm(); pop();
        return;
    }
    const char* ty = g.type == 0 ? "themes" : g.type == 1 ? "splashes" : "badges";
    char l1[96];
    if (plaza() && !backend::online()) return promo(SPR_ic_wifi_w, false, "No internet connection", "Theme Plaza needs Wi-Fi.", "Everything on your SD card still works from your Collection.");
    if (plaza() && !backend::store().ready) {
        static const char* const orders[3] = {"newest", "most downloaded", "most liked"};
        if (findOn()) snprintf(l1, sizeof l1, "Searching\xe2\x80\xa6");
        else snprintf(l1, sizeof l1, "Loading the %s %s\xe2\x80\xa6", orders[g.order], ty);
        return promo(SPR_tp_mark_l, true, "Theme Plaza", l1, nullptr);
    }
    if (plaza() && backend::store().failed) {
        const std::string& why = backend::store().error;
        return promo(SPR_ic_wifi_w, false, "Theme Plaza is not answering", why.empty() ? "The site could not be read." : why.c_str(), "Press A to try again.");
    }
    if (plaza() && !it) {
        if (findOn()) return promo(SPR_tp_mark_l, true, "Nothing found", "Press X to change your search.", nullptr);
        snprintf(l1, sizeof l1, "There are no %s here yet.", ty);
        return promo(SPR_tp_mark_l, true, "Theme Plaza", l1, nullptr);
    }
    if (!plaza() && backend::scanning() && !it) return promo(SPR_tp_mark_l, true, "Collection", "Reading your SD card\xe2\x80\xa6", nullptr);
    if (!it) return promo(SPR_tp_mark_l, true, "Nothing found", "Press X to change your search.", nullptr);
    if (it->more) {
        int n = homeCount();
        if (findOn() && !n) return promo(SPR_tp_mark_l, true, "Nothing in your Collection matches", "Tap the orange chip to clear the search, or press X to change it.", nullptr);
        char big[64]; snprintf(big, sizeof big, n ? "Get more %s" : "No %s on your SD card yet", ty);
        snprintf(l1, sizeof l1, "Theme Plaza has %s made by the community.", backend::storeCount((Kind)g.type));
        return promo(SPR_tp_mark_l, true, big, l1, "Press A to open it, or R to switch tabs.");
    }
    if (it->kind == K_BADGE) badgeCard(it);
    else { card(it, false, 16, 200, 0); card(it, true, 224, 160, 0.04f); }
    float e = anim::ease(anim::EASE_OUT, g.stageClock.frac(0.2f));
    push(0, 5 * (1 - e), 1, e); caption(it); pop();
}

}  // namespace ui
