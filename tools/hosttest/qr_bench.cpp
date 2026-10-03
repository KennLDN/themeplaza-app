// Decodes a file of 224x224 8-bit frames (as the app's camera thread hands them to quirc) and prints one
// character per frame: 1 if the expected text was read, 0 if not. Used by qr_bench.py.
#include <cstdio>
#include <cstring>
#include <vector>
#include "../../app/source/core/qrprep.h"
extern "C" {
#include "../../app/source/third_party/quirc/quirc.h"
}

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const int N = 224;
    FILE* f = fopen(argv[1], "rb");
    if (!f) return 2;
    quirc* q = quirc_new();
    quirc_resize(q, N, N);
    std::vector<unsigned char> frame(N * N);
    std::vector<uint16_t> tmp(N * N);
    const char mode = argc < 4 ? '1' : argv[3][0];      // 0: as it comes, 1: sharpened, 2: middle enlarged, then sharpened
    const bool prep = mode != '0';
    static quirc_code code; static quirc_data data;
    while (fread(frame.data(), 1, frame.size(), f) == frame.size()) {
        int w, h;
        unsigned char* img = quirc_begin(q, &w, &h);
        if (mode == '2') qrprep::zoomCentre(frame.data(), img, N); else memcpy(img, frame.data(), frame.size());
        if (prep) qrprep::sharpen(img, tmp.data(), N);
        quirc_end(q);
        bool ok = false;
        for (int i = 0; i < quirc_count(q) && !ok; i++) {
            quirc_extract(q, i, &code);
            quirc_decode_error_t e = quirc_decode(&code, &data);
            if (e == QUIRC_ERROR_DATA_ECC) { quirc_flip(&code); e = quirc_decode(&code, &data); }     // as the app does
            ok = !e && !strcmp((const char*)data.payload, argv[2]);
        }
        putchar(ok ? '1' : '0');
    }
    putchar('\n');
    fclose(f);
    return 0;
}
