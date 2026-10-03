// Small logger: lines go to sdmc:/3ds/Theme Plaza/log.txt and to the debugger/emulator log.
#pragma once

namespace logx {
void init();
void write(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
}

#define LOG(...) logx::write(__VA_ARGS__)

// Development aid: logs a line when the enclosing block took longer than 2 ms on the main thread.
#ifdef THEME_PLAZA_DEV
#include <3ds.h>
struct SlowBlock {
    const char* name; u64 start;
    explicit SlowBlock(const char* n) : name(n), start(svcGetSystemTick()) {}
    ~SlowBlock() { float ms = (float)(svcGetSystemTick() - start) * 1000.0f / SYSCLOCK_ARM11; if (ms > 2.0f) LOG("slow: %s took %.1f ms", name, ms); }
};
#define SLOW(name) SlowBlock slowBlock_(name)
#else
#define SLOW(name)
#endif
