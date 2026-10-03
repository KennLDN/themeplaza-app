// Thin drawing layer over citro3d: one UI atlas, 9-slice boxes, pixel-exact text, a small
// transform stack (translate / scale / alpha) and rectangular clipping.
#pragma once
#include <3ds.h>
#include <citro3d.h>
#include "sprites_gen.h"

namespace gfx {

enum Screen { TOP = 0, BOT = 1 };
// The P variants are the same size without the slight stroke (used for small secondary text).
enum Font { F10, F10P, F11, F11P, F12, F13, F14, F15, F19, F22, FONT_COUNT };
enum Align { LEFT = 0, CENTER = 1, RIGHT = 2 };

// 0xRRGGBB plus alpha 0..1 -> citro2d colour
constexpr u32 rgb(u32 hex, float a = 1.0f) {
    return ((u32)(a * 255.0f + 0.5f) << 24) | ((hex & 0xff) << 16) | (hex & 0xff00) | ((hex >> 16) & 0xff);
}

void init();
void loadAssets();          // sprites and fonts; init() alone is enough for plain rectangles and gradients
void fini();
void frameBegin();
void screenBegin(Screen s);
void frameEnd();
int vertexCount();          // vertices submitted so far this frame
float fillArea();           // pixels covered by the quads submitted so far this frame (dev builds; else 0)
Screen screen();

// Transform stack. A pushed transform moves by (tx,ty), scales about (ox,oy) and multiplies alpha.
void push(float tx, float ty, float scale = 1.0f, float alpha = 1.0f, float ox = 0.0f, float oy = 0.0f);
void pop();

void rect(float x, float y, float w, float h, u32 color);
void gradV(float x, float y, float w, float h, u32 top, u32 bottom);

// Sprites are placed by their element box: (x,y) is where the box's top-left goes; shadows extend outside it.
void spr(Spr s, float x, float y, float alpha = 1.0f);
void sprColor(Spr s, float x, float y, u32 color);                       // white artwork drawn in a flat colour
void sprEx(Spr s, float cx, float cy, float scale, float angle, u32 color, bool flat);  // centred, rotated
void sprPart(Spr s, float sx, float sy, float sw, float sh, float x, float y, float alpha = 1.0f);  // a sub-rectangle of a sprite
void slice(Spr s, float x, float y, float w, float h, float alpha = 1.0f);  // 9-slice stretched to a w x h box
void sliceColor(Spr s, float x, float y, float w, float h, u32 color);

// Arbitrary texture region (previews, icons). uv in texture pixels of a texW x texH texture whose row 0 is the top.
void image(C3D_Tex* tex, float sx, float sy, float sw, float sh, float x, float y, float w, float h, float alpha = 1.0f);
// The same, centred on (cx,cy) and rotated by angle (radians).
void imageRot(C3D_Tex* tex, float sx, float sy, float sw, float sh, float cx, float cy, float w, float h, float angle, float alpha = 1.0f);

// Text. y is the top of a CSS line box of height lineH (0 = the font size), so positions can be copied
// straight from the design. Returns the drawn width.
float text(Font f, float x, float y, u32 color, const char* str, Align align = LEFT, float lineH = 0.0f);
float textShadow(Font f, float x, float y, u32 color, u32 shadow, const char* str, Align align = LEFT, float lineH = 0.0f);
float textWidth(Font f, const char* str);
// Cuts a string so it fits maxW, appending an ellipsis. Returns the number of bytes kept (before the ellipsis).
size_t fit(Font f, const char* str, float maxW, char* out, size_t outSize);
// Word-wraps into at most maxLines lines of width maxW; the last line gets an ellipsis if text is left over.
int wrap(Font f, const char* str, float maxW, int maxLines, char lines[][160]);

void clip(float x, float y, float w, float h);
void unclip();
void cover(u32 color);      // one colour over the whole screen being drawn, whatever clip or transform was left in force

}  // namespace gfx
