// QR scanning with the outer camera. A thread captures frames, keeps the newest one ready for the
// screen, and looks for a code in the middle of the picture with quirc.
#pragma once
#include <3ds.h>
#include <string>

namespace qr {

constexpr int W = 400, H = 240;          // camera picture
constexpr int TEX_W = 512, TEX_H = 256;  // the texture it is shown in (RGB565, tiled)

bool start();                 // false if the camera could not be started
void stop(bool wait = false); // asks the camera thread to end; with wait, returns only once it has
void update();                // once per frame: frees a stopped camera thread
bool failed();                // the camera thread ended because the camera could not be set up
bool active();                // false once the camera has stopped by itself (console closed, HOME pressed)
// Copies the newest camera picture, already in texture layout, into dst (TEX_W*TEX_H*2 bytes).
// Returns false when there is none newer than `serial`.
bool picture(u8* dst, u32& serial);
bool take(std::string& text); // the text of a code that was read; true once per code

}  // namespace qr
