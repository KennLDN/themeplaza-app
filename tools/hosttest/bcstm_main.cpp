// Decodes a theme's bgm.bcstm with the app's decoder and writes the samples as raw 16-bit data,
// for comparing against another decoder.   bcstm_dump THEME_ZIP_OR_FOLDER OUT.raw
#include <cstdio>
#include <vector>
#include "../../app/source/core/bcstm.h"
int main(int argc, char** argv) {
    Bcstm b;
    if (argc < 3 || !b.open(argv[1])) { printf("cannot open\n"); return 1; }
    FILE* f = fopen(argv[2], "wb");
    std::vector<s16> buf(4096 * 2); size_t n, total = 0;
    while ((n = b.read(buf.data(), 4096)) > 0) { fwrite(buf.data(), 2 * b.channels, n, f); total += n; }
    fclose(f);
    printf("%d Hz, %d channels, %zu frames\n", b.rate, b.channels, total);
    return 0;
}
