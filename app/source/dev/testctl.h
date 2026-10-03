#pragma once

namespace dev {
void init();
void update();        // call once per frame, before input::update()
void afterFrame();    // call after the frame is presented; writes a pending screenshot
bool quitRequested();
// The time step to use for this frame. Normally the measured one; with "freeze 1" it is 0, and "step N"
// then plays N frames of exactly 1/60 s, so animations can be photographed at known moments.
float frameDt(float dt);
bool monkey();        // random-input test running: the app must not exit by itself
// Per-frame cost for the log: CPU ticks spent in update and in building the draw list, the frame's
// duration in seconds, and the number of vertices drawn. An average is logged every few seconds.
void frameTimes(float updateTicks, float drawTicks, float dt, int vertices, float fillArea);
}
