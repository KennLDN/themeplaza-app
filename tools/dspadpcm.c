// DSP-ADPCM encoder (the 3DS's 4-bit sound format), written for this project from the format's
// description: frames of 14 samples in 8 bytes, a header byte (which of 8 predictors, and a scale) and 14
// four-bit steps. tools/music_encode.py picks the predictors and calls this for the frames.
//
// Each sample is rebuilt by the decoder as
//     ((step << scale) * 2048 + 1024 + c1 * previous + c2 * one_before) >> 11        (kept within 16 bits)
// so the encoder tries, for every frame, each predictor with the scales that could fit, steps through the
// frame exactly as the decoder will, and keeps the choice whose result is closest to the original.
#include <stdint.h>
#include <stdlib.h>

static inline int clamp16(int v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }

// pcm: n samples. coef: 8 pairs (c1, c2), as the file stores them. hist: the two samples before the first
// (previous, one before); on return, the two the decoder ends with. out: (n + 13) / 14 * 8 bytes.
// Returns the sum of squared errors.
double dsp_encode(const int16_t* pcm, int n, const int16_t* coef, int16_t* hist, uint8_t* out) {
    int h1 = hist[0], h2 = hist[1];
    double total = 0;
    for (int at = 0; at < n; at += 14) {
        int len = n - at < 14 ? n - at : 14;
        double best = -1; int best_h1 = 0, best_h2 = 0; uint8_t best_frame[8] = {0};
        for (int p = 0; p < 8; p++) {
            int c1 = coef[p * 2], c2 = coef[p * 2 + 1];
            // how large the steps would have to be if nothing were rounded
            int a1 = h1, a2 = h2, need = 0;
            for (int k = 0; k < len; k++) {
                int x = pcm[at + k];
                int r = abs(x - ((1024 + c1 * a1 + c2 * a2) >> 11));
                if (r > need) need = r;
                a2 = a1; a1 = x;
            }
            int s0 = 0;
            while (s0 < 12 && (need >> s0) > 7) s0++;
            for (int s = s0 > 0 ? s0 - 1 : 0; s <= s0 + 1 && s <= 12; s++) {
                int b1 = h1, b2 = h2; double err = 0; uint8_t frame[8] = {0};
                frame[0] = (uint8_t)(p << 4 | s);
                for (int k = 0; k < 14; k++) {
                    int x = k < len ? pcm[at + k] : b1;
                    int pred = 1024 + c1 * b1 + c2 * b2;
                    int target = x - (pred >> 11);
                    int nib = target >= 0 ? (target + (1 << s >> 1)) >> s : -((-target + (1 << s >> 1)) >> s);
                    if (nib > 7) nib = 7; else if (nib < -8) nib = -8;
                    // the neighbouring steps can come out nearer once the result is clipped and rounded
                    int pick = nib, v = 0; double e = -1;
                    for (int d = -1; d <= 1; d++) {
                        int q = nib + d;
                        if (q > 7 || q < -8) continue;
                        int w = clamp16((q * (1 << s) * 2048 + pred) >> 11);
                        double ee = (double)(w - x) * (w - x);
                        if (e < 0 || ee < e) { e = ee; pick = q; v = w; }
                    }
                    if (k < len) err += e;
                    frame[1 + k / 2] |= (uint8_t)((pick & 15) << ((k & 1) ? 0 : 4));
                    b2 = b1; b1 = v;
                    if (best >= 0 && err > best) break;
                }
                if (best < 0 || err < best) {
                    best = err; best_h1 = b1; best_h2 = b2;
                    for (int i = 0; i < 8; i++) best_frame[i] = frame[i];
                }
            }
        }
        for (int i = 0; i < 8; i++) out[at / 14 * 8 + i] = best_frame[i];
        h1 = best_h1; h2 = best_h2; total += best;
    }
    hist[0] = (int16_t)h1; hist[1] = (int16_t)h2;
    return total;
}
