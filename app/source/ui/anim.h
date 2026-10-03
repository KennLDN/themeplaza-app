// Timing helpers that reproduce CSS transitions: a value that eases from where it is to a new target.
#pragma once
#include <cmath>

namespace anim {

// CSS cubic-bezier(x1, y1, x2, y2) evaluated at time fraction t (0..1).
inline float bezier(float x1, float y1, float x2, float y2, float t) {
    if (t <= 0) return 0;
    if (t >= 1) return 1;
    auto bx = [&](float u) { float v = 1 - u; return 3 * v * v * u * x1 + 3 * v * u * u * x2 + u * u * u; };
    auto by = [&](float u) { float v = 1 - u; return 3 * v * v * u * y1 + 3 * v * u * u * y2 + u * u * u; };
    float lo = 0, hi = 1, u = t;
    for (int i = 0; i < 14; i++) { float x = bx(u); if (x < t) lo = u; else hi = u; u = (lo + hi) * 0.5f; }
    return by(u);
}

enum Ease { LINEAR, EASE, EASE_OUT, EASE_IN_OUT, POP_TILE, POP_CHIP, POP_MODAL, POP_CARD, SHEET };

inline float ease(Ease e, float t) {
    switch (e) {
        case LINEAR: return t < 0 ? 0 : t > 1 ? 1 : t;
        case EASE: return bezier(0.25f, 0.1f, 0.25f, 1.0f, t);
        case EASE_OUT: return bezier(0.0f, 0.0f, 0.58f, 1.0f, t);
        case EASE_IN_OUT: return bezier(0.42f, 0.0f, 0.58f, 1.0f, t);
        case POP_TILE: return bezier(0.3f, 1.8f, 0.5f, 1.0f, t);    // selected tile growing
        case POP_CHIP: return bezier(0.3f, 1.9f, 0.5f, 1.0f, t);    // chips and markers popping in
        case POP_MODAL: return bezier(0.3f, 1.5f, 0.5f, 1.0f, t);
        case POP_CARD: return bezier(0.2f, 0.9f, 0.3f, 1.25f, t);   // preview cards arriving
        case SHEET: return bezier(0.2f, 0.9f, 0.3f, 1.1f, t);
    }
    return t;
}

struct Tween {
    float from = 0, to = 0, t = 1, dur = 0.1f;
    Ease e = EASE;
    float v() const { return t >= 1 ? to : from + (to - from) * ease(e, t); }
    bool done() const { return t >= 1; }
    void set(float x) { from = to = x; t = 1; }
    void go(float target, float seconds, Ease curve = EASE) {
        if (target == to) return;
        from = v(); to = target; t = 0; dur = seconds; e = curve;
    }
    void step(float dt) { if (t < 1) { t += dt / dur; if (t > 1) t = 1; } }
};

// A one-shot clock: seconds since it was started, or a large value if it never was.
struct Clock {
    float t = 1e9f;
    void start() { t = 0; }
    void step(float dt) { if (t < 1e8f) t += dt; }
    float frac(float dur, float delay = 0) const { float x = (t - delay) / dur; return x < 0 ? 0 : x > 1 ? 1 : x; }
    bool active(float dur) const { return t < dur; }
};

}  // namespace anim
