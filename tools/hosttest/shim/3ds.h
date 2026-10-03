// Just enough of libctru's types for the format readers to compile and run on the PC (tools/hosttest).
#pragma once
#include <cstddef>
#include <cstdint>
#include <sys/types.h>
typedef uint8_t u8; typedef uint16_t u16; typedef uint32_t u32; typedef uint64_t u64;
typedef int8_t s8; typedef int16_t s16; typedef int32_t s32; typedef int64_t s64;

inline ssize_t utf16_to_utf8(u8* out, const u16* in, size_t len) {
    size_t n = 0;
    for (; *in; in++) {
        u32 c = *in;
        if (c >= 0xD800 && c < 0xDC00 && in[1] >= 0xDC00 && in[1] < 0xE000) { c = 0x10000 + ((c - 0xD800) << 10) + (in[1] - 0xDC00); in++; }
        u8 buf[4]; int k;
        if (c < 0x80) { buf[0] = c; k = 1; }
        else if (c < 0x800) { buf[0] = 0xC0 | (c >> 6); buf[1] = 0x80 | (c & 0x3F); k = 2; }
        else if (c < 0x10000) { buf[0] = 0xE0 | (c >> 12); buf[1] = 0x80 | ((c >> 6) & 0x3F); buf[2] = 0x80 | (c & 0x3F); k = 3; }
        else { buf[0] = 0xF0 | (c >> 18); buf[1] = 0x80 | ((c >> 12) & 0x3F); buf[2] = 0x80 | ((c >> 6) & 0x3F); buf[3] = 0x80 | (c & 0x3F); k = 4; }
        if (n + k > len) break;
        for (int i = 0; i < k; i++) out[n++] = buf[i];
    }
    return (ssize_t)n;
}
