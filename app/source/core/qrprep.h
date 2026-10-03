// Getting a camera picture ready for the QR reader. The 3DS cameras have a fixed focus, so a code held
// close is soft; quirc needs crisp module edges. An unsharp mask (the picture plus three times its
// difference from a 7x7 box blur of itself) restores enough of the edges: measured with
// tools/hosttest/qr_bench.py, a 130 px code under a blur of radius 2 goes from never read to four frames
// in five, and nothing is lost on a sharp picture.
//
// Plain C++ with no console dependencies, so the PC benchmark runs the very same code.
#pragma once
#include <cstdint>

namespace qrprep {

constexpr int RADIUS = 3, AMOUNT = 3;
constexpr int MAX_SIDE = 512;     // the largest picture side handled (the app's is 224)

// img: n x n brightness values, sharpened in place. tmp: n x n 16-bit values of working space.
inline void sharpen(uint8_t* img, uint16_t* tmp, int n) {
    const int k = 2 * RADIUS + 1;
    if (n < 1 || n > MAX_SIDE) return;
    // box sums along each row into tmp (at the edges the outermost pixel repeats)
    for (int y = 0; y < n; y++) {
        const uint8_t* row = img + y * n;
        uint16_t* out = tmp + y * n;
        int sum = row[0] * (RADIUS + 1);
        for (int x = 1; x <= RADIUS; x++) sum += row[x < n ? x : n - 1];
        for (int x = 0; x < n; x++) {
            out[x] = (uint16_t)sum;
            int add = x + RADIUS + 1, drop = x - RADIUS;
            sum += row[add < n ? add : n - 1] - row[drop > 0 ? drop : 0];
        }
    }
    // Box sums of those down the columns give the blurred value. They are kept as one running sum per column
    // and the picture is walked row by row, so memory is read in order (walking down each column instead
    // costs a cache miss per pixel on the console). The result goes straight back into img; tmp is only read.
    int colSum[MAX_SIDE];
    for (int x = 0; x < n; x++) {
        int sum = tmp[x] * (RADIUS + 1);
        for (int y = 1; y <= RADIUS; y++) sum += tmp[(y < n ? y : n - 1) * n + x];
        colSum[x] = sum;
    }
    const int recip = (65536 + k * k / 2) / (k * k);        // sum / 49 as a multiplication and a shift
    for (int y = 0; y < n; y++) {
        uint8_t* row = img + y * n;
        int add = y + RADIUS + 1, drop = y - RADIUS;
        const uint16_t* addRow = tmp + (add < n ? add : n - 1) * n;
        const uint16_t* dropRow = tmp + (drop > 0 ? drop : 0) * n;
        for (int x = 0; x < n; x++) {
            int v = row[x];
            v += AMOUNT * (v - ((colSum[x] * recip) >> 16));
            row[x] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
            colSum[x] += addRow[x] - dropRow[x];
        }
    }
}

// A closer look at the middle: the central half of an n x n picture (n even), enlarged to n x n with
// values between neighbours averaged. A code that is small on the picture (under about 60 px wide) has
// too few pixels per module for quirc; enlarged, a 50 px code is read in 6 frames of 10 instead of 1.
// The app does this on every other frame. src and dst must not overlap.
inline void zoomCentre(const uint8_t* src, uint8_t* dst, int n) {
    const int h = n / 2, o = n / 4;                      // the middle h x h, starting at (o, o)
    for (int y = 0; y < n; y++) {
        int sy = o + y / 2, sy2 = (y & 1) && sy + 1 < n ? sy + 1 : sy;
        const uint8_t* a = src + sy * n + o;
        const uint8_t* b = src + sy2 * n + o;
        uint8_t* out = dst + y * n;
        for (int x = 0; x < h; x++) {
            int x2 = x + 1 < h ? x + 1 : x;
            int left = (a[x] + b[x] + 1) >> 1, right = (a[x2] + b[x2] + 1) >> 1;
            out[2 * x] = (uint8_t)left;
            out[2 * x + 1] = (uint8_t)((left + right + 1) >> 1);
        }
    }
}

}  // namespace qrprep
