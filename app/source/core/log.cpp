#include "log.h"
#include <3ds.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include "appdir.h"

namespace logx {

namespace {
FILE* g_file = nullptr;
LightLock g_lock;
}

void init() {
    LightLock_Init(&g_lock);
    mkdir("sdmc:/3ds", 0777);     // a card set up for installed titles only may not have it
    mkdir(APP_DIR, 0777);
    g_file = fopen(APP_DIR "/log.txt", "w");
}

void write(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf - 2, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof buf - 2) n = sizeof buf - 2;
    svcOutputDebugString(buf, n);
    LightLock_Lock(&g_lock);
    if (g_file) { buf[n] = '\n'; fwrite(buf, 1, n + 1, g_file); fflush(g_file); }
    LightLock_Unlock(&g_lock);
}

}  // namespace logx
