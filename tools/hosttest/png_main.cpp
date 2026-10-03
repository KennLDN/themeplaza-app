// Host test of the app's PNG reader: decodes each file given and writes the pixels as raw RGBA next to it
// (FILE.rgba, preceded by nothing; the size is printed). png_check.py compares them with another decoder.
#include <cstdio>
#include <string>
#include <vector>
#include "formats.h"

int main(int argc, char** argv) {
    int bad = 0;
    for (int a = 1; a < argc; a++) {
        std::vector<u8> buf;
        FILE* f = fopen(argv[a], "rb");
        if (!f) { printf("%s: cannot open\n", argv[a]); bad++; continue; }
        fseek(f, 0, SEEK_END); buf.resize(ftell(f)); fseek(f, 0, SEEK_SET);
        if (fread(buf.data(), 1, buf.size(), f) != buf.size()) buf.clear();
        fclose(f);
        fmt::Png png;
        if (!fmt::decodePng(buf.data(), buf.size(), png)) { printf("%s: refused\n", argv[a]); bad++; continue; }
        std::string out = std::string(argv[a]) + ".rgba";
        f = fopen(out.c_str(), "wb");
        fwrite(png.rgba.data(), 1, png.rgba.size(), f);
        fclose(f);
        printf("%s: %dx%d\n", argv[a], png.w, png.h);
    }
    return bad ? 1 : 0;
}
