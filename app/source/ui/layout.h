// Geometry shared between drawing and touch hit-testing.
#pragma once
#include <vector>
#include "app.h"

namespace ui {

struct Rect {
    float x, y, w, h;
    bool has(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

struct KeyDef { const char* k; int x, y, w; };
const std::vector<KeyDef>& keyDefs();                                        // on-screen keyboard keys

void footLayout(const std::vector<FootBtn>& spec, std::vector<Rect>& out);   // footer buttons, left to right
void segLayout(Rect out[3]);                                                 // type selector pills
Rect chipRect();                                                             // the "search is on" chip

}  // namespace ui
