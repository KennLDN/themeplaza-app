// Buttons and touch for one frame. Real input from HID is merged with scripted input from the
// development test channel, so both go through the same code paths.
#pragma once
#include <3ds.h>

namespace input {

enum Btn : u32 {
    A = KEY_A, B = KEY_B, X = KEY_X, Y = KEY_Y, L = KEY_L, R = KEY_R, START = KEY_START, SELECT = KEY_SELECT,
    UP = KEY_DUP, DOWN = KEY_DDOWN, LEFT = KEY_DLEFT, RIGHT = KEY_DRIGHT,
};

struct State {
    u32 down = 0;     // pressed this frame
    u32 held = 0;
    u32 repeat = 0;   // pressed this frame, or auto-repeating while held (directions only)
    bool touchDown = false, touchHeld = false, touchUp = false;
    int tx = 0, ty = 0;       // last touch position on the bottom screen
};

void update();
const State& get();

// scripted input (development test channel)
void scriptHold(u32 keys, int frames);
void scriptTouch(int x, int y, int frames);
bool scriptBusy();

}  // namespace input
