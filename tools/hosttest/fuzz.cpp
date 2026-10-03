// Feeds damaged files to the app's readers under the address sanitizer, to find out-of-bounds reads
// and writes before a real file does. Built and run by tools/hosttest/fuzz.py inside the build container.
//   fuzz ITERATIONS WORKDIR FILE...     FILE: .png .smdh .bcstm .zip, or a body_LZ.bin
// The test itself is in app/source/dev/fuzzcore.h, which the app's development build also runs on the console.
#include <cstdio>
#define FUZZ_LOG(...) do { printf(__VA_ARGS__); printf("\n"); fflush(stdout); } while (0)
#include "../../app/source/dev/fuzzcore.h"

int main(int argc, char** argv) {
    if (argc < 4) return 1;
    std::vector<std::string> files(argv + 3, argv + argc);
    long runs = fuzzcore::run(atoi(argv[1]), argv[2], files);
    if (runs < 0) return 1;
    printf("%ld damaged files read without a memory error\n", runs);
    return 0;
}
