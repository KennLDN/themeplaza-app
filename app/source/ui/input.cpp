#include "input.h"

namespace input {

namespace {
State g;
u32 g_prevHeld = 0;
bool g_prevTouch = false;
int g_repeatFrames = 0;
u32 g_scriptKeys = 0; int g_scriptKeyFrames = 0;
int g_scriptTouchFrames = 0, g_stx = 0, g_sty = 0;
constexpr u32 DIRS = UP | DOWN | LEFT | RIGHT;
constexpr int REPEAT_DELAY = 18, REPEAT_RATE = 5;  // frames
}

void update() {
    hidScanInput();
    u32 held = hidKeysHeld();
    // the circle pad acts as the D-pad
    if (held & KEY_CPAD_UP) held |= UP;
    if (held & KEY_CPAD_DOWN) held |= DOWN;
    if (held & KEY_CPAD_LEFT) held |= LEFT;
    if (held & KEY_CPAD_RIGHT) held |= RIGHT;
    held &= A | B | X | Y | L | R | START | SELECT | DIRS;

    bool touch = (hidKeysHeld() & KEY_TOUCH) != 0;
    touchPosition tp;
    if (touch) { hidTouchRead(&tp); g.tx = tp.px; g.ty = tp.py; }

    if (g_scriptKeyFrames > 0) { held |= g_scriptKeys; g_scriptKeyFrames--; }
    if (g_scriptTouchFrames > 0) { touch = true; g.tx = g_stx; g.ty = g_sty; g_scriptTouchFrames--; }

    g.down = held & ~g_prevHeld;
    g.held = held;
    g.repeat = g.down;
    if ((held & DIRS) && (held & DIRS) == (g_prevHeld & DIRS)) {
        g_repeatFrames++;
        if (g_repeatFrames >= REPEAT_DELAY && (g_repeatFrames - REPEAT_DELAY) % REPEAT_RATE == 0) g.repeat |= held & DIRS;
    } else {
        g_repeatFrames = 0;
    }
    g.touchDown = touch && !g_prevTouch;
    g.touchUp = !touch && g_prevTouch;
    g.touchHeld = touch;
    g_prevHeld = held;
    g_prevTouch = touch;
}

const State& get() { return g; }

void scriptHold(u32 keys, int frames) { g_scriptKeys = keys; g_scriptKeyFrames = frames; }
void scriptTouch(int x, int y, int frames) { g_stx = x; g_sty = y; g_scriptTouchFrames = frames; }
bool scriptBusy() { return g_scriptKeyFrames > 0 || g_scriptTouchFrames > 0; }

}  // namespace input
