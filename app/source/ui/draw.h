// Internal drawing helpers shared by the top and bottom screen code.
#pragma once
#include "app.h"
#include "layout.h"

namespace ui {

void drawPreviewTop(Item* it, float x, float y, float scale, float alpha = 1.0f);      // 400x240 at scale 1
void drawPreviewBottom(Item* it, float x, float y, float scale, float alpha = 1.0f);   // 320x240 at scale 1

inline u32 mixColor(u32 a, u32 b, float t) {
    auto ch = [&](int sh) { float x = (float)((a >> sh) & 0xff), y = (float)((b >> sh) & 0xff); return (u32)(x + (y - x) * t + 0.5f) << sh; };
    return ch(0) | ch(8) | ch(16) | ch(24);
}

constexpr float TAU = 6.2831853f;

}  // namespace ui
