// Development test channel. The test runner on the PC writes commands to a file on the (emulated)
// SD card; the app plays them as button presses and touches and writes screenshots back.
// Compiled only when THEME_PLAZA_DEV is defined.
#include "testctl.h"
#ifdef THEME_PLAZA_DEV
#include <3ds.h>
#include <citro3d.h>
#include <cstdio>
#include <malloc.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <vector>
#include <dirent.h>
#include "../core/backend.h"
#include "../core/worker.h"
#include "../core/log.h"
#define FUZZ_LOG(...) LOG(__VA_ARGS__)
#include "fuzzcore.h"
#include "../ui/input.h"
#include "../core/appdir.h"

namespace dev {

volatile bool failInstall = false;     // makes a single-theme install stop after the theme body is written (core/install.cpp)

namespace {
const char* CMD = APP_DIR "/ctl/cmd.txt";
const char* ACK = APP_DIR "/ctl/ack.txt";
std::vector<std::string> g_queue;
size_t g_pos = 0;
long g_seq = -1;
int g_wait = 0, g_poll = 0;
std::string g_shot;
bool g_quit = false, g_monkey = false;
bool g_frozen = false; int g_stepLeft = 0;   // animation time stopped; frames still to play at exactly 1/60 s

u32 keyMask(const char* n) {
    struct { const char* n; u32 k; } tab[] = {{"a", KEY_A}, {"b", KEY_B}, {"x", KEY_X}, {"y", KEY_Y}, {"l", KEY_L}, {"r", KEY_R},
        {"start", KEY_START}, {"select", KEY_SELECT}, {"up", KEY_DUP}, {"down", KEY_DDOWN}, {"left", KEY_DLEFT}, {"right", KEY_DRIGHT}};
    for (auto& t : tab) if (!strcmp(t.n, n)) return t.k;
    return 0;
}

void poll() {
    FILE* f = fopen(CMD, "r");
    if (!f) return;
    char line[256];
    if (!fgets(line, sizeof line, f)) { fclose(f); return; }
    long seq = strtol(line, nullptr, 10);
    if (seq != g_seq) {
        g_seq = seq; g_queue.clear(); g_pos = 0;
        while (fgets(line, sizeof line, f)) {
            size_t n = strlen(line);
            while (n && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = 0;
            if (n) g_queue.push_back(line);
        }
        LOG("ctl: seq %ld, %u commands", seq, (unsigned)g_queue.size());
    }
    fclose(f);
}

void ack() {
    FILE* f = fopen(ACK, "w");
    if (f) { fprintf(f, "%ld\n", g_seq); fclose(f); }
}

// Writes both screens as one 400x480 24-bit BMP (bottom screen centred under the top one).
void saveShot(const std::string& name) {
    u16 w, h;
    u8* top = gfxGetFramebuffer(GFX_TOP, GFX_LEFT, &w, &h);
    u8* bot = gfxGetFramebuffer(GFX_BOTTOM, GFX_LEFT, &w, &h);
    const int W = 400, H = 480;
    std::vector<u8> img(W * H * 3, 0);
    for (int y = 0; y < 240; y++) {
        u8* rowTop = &img[(size_t)(H - 1 - y) * W * 3];
        for (int x = 0; x < 400; x++) memcpy(rowTop + x * 3, top + (x * 240 + (239 - y)) * 3, 3);
        u8* rowBot = &img[(size_t)(H - 1 - (240 + y)) * W * 3];
        for (int x = 0; x < 320; x++) memcpy(rowBot + (40 + x) * 3, bot + (x * 240 + (239 - y)) * 3, 3);
    }
    std::string path = APP_DIR "/ctl/" + name + ".bmp";
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return;
    u32 size = 54 + img.size();
    u8 hd[54] = {'B', 'M'};
    auto put32 = [&](int o, u32 v) { hd[o] = v; hd[o + 1] = v >> 8; hd[o + 2] = v >> 16; hd[o + 3] = v >> 24; };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H); hd[26] = 1; hd[28] = 24; put32(34, img.size());
    fwrite(hd, 1, 54, f);
    fwrite(img.data(), 1, img.size(), f);
    fclose(f);
    LOG("ctl: shot %s", name.c_str());
}
}  // namespace

void init() {
    mkdir(APP_DIR "/ctl", 0777);
    // commands left in the file by an earlier run are not for this one
    if (FILE* f = fopen(CMD, "r")) {
        char line[64];
        if (fgets(line, sizeof line, f)) g_seq = strtol(line, nullptr, 10);
        fclose(f);
    }
}

void update() {
    if (g_wait > 0) { g_wait--; return; }
    if (input::scriptBusy()) return;
    if (g_pos >= g_queue.size()) {
        if (g_pos && g_pos == g_queue.size()) { ack(); g_pos++; }
        if (++g_poll >= 15) { g_poll = 0; poll(); }
        return;
    }
    char cmd[32] = "", a1[64] = "", a2[32] = "", a3[32] = "";
    sscanf(g_queue[g_pos++].c_str(), "%31s %63s %31s %31s", cmd, a1, a2, a3);
    if (!strcmp(cmd, "press")) { input::scriptHold(keyMask(a1), 3); g_wait = 5; }
    else if (!strcmp(cmd, "hold")) { input::scriptHold(keyMask(a1), atoi(a2)); }
    else if (!strcmp(cmd, "touch")) { input::scriptTouch(atoi(a1), atoi(a2), a3[0] ? atoi(a3) : 3); g_wait = 5; }
    else if (!strcmp(cmd, "wait")) { g_wait = atoi(a1); }
    else if (!strcmp(cmd, "shot")) { g_shot = a1; g_wait = 1; }
    else if (!strcmp(cmd, "exit")) { g_quit = true; ack(); }   // acknowledged now: there is no next frame to do it in
    else if (!strcmp(cmd, "offline")) { backend::setOffline(atoi(a1) != 0); }
    else if (!strcmp(cmd, "monkey")) { g_monkey = atoi(a1) != 0; }
    else if (!strcmp(cmd, "fuzz")) {
        // damaged copies of every file in sdmc:/3ds/Theme Plaza/fuzz, read by the app's own readers on this console
        int iterations = atoi(a1) > 0 ? atoi(a1) : 20;
        worker::disk.post([iterations] {
            std::vector<std::string> files;
            if (DIR* d = opendir(APP_DIR "/fuzz")) {
                while (dirent* e = readdir(d)) if (e->d_name[0] != '.' && strcmp(e->d_name, "work") != 0) files.push_back(std::string(APP_DIR "/fuzz/") + e->d_name);
                closedir(d);
            }
            LOG("fuzz: %u files, %d damaged copies of each", (unsigned)files.size(), iterations);
            long runs = fuzzcore::run(iterations, APP_DIR "/fuzz/work", files);
            LOG("fuzz: done, %ld damaged files read", runs);
        });
    }
    else if (!strcmp(cmd, "queues")) {
        int o, q, ms;
        worker::disk.state(o, q, ms); LOG("queues: disk worker has %d operations and %d other tasks waiting, current task running for %d ms", o, q, ms);
        worker::net.state(o, q, ms); LOG("queues: network worker has %d operations and %d other tasks waiting, current task running for %d ms", o, q, ms);
    }
    else if (!strcmp(cmd, "failinstall")) { failInstall = atoi(a1) != 0; }
    else if (!strcmp(cmd, "freeze")) { g_frozen = atoi(a1) != 0; g_stepLeft = 0; }
    else if (!strcmp(cmd, "step")) { g_stepLeft = atoi(a1); g_wait = g_stepLeft; }
    else LOG("ctl: unknown command \"%s\" (an older build?)", cmd);
}

void afterFrame() {
    if (g_shot.empty()) return;
    gspWaitForVBlank();
    saveShot(g_shot);
    g_shot.clear();
}

bool quitRequested() { return g_quit; }
bool monkey() { return g_monkey; }
float frameDt(float dt) {
    if (!g_frozen) return dt;
    if (g_stepLeft > 0) { g_stepLeft--; return 1.0f / 60.0f; }
    return 0;
}

void frameTimes(float updateTicks, float drawTicks, float dt, int vertices, float fillArea) {
    static float up = 0, dr = 0, upMax = 0, drMax = 0, time = 0, fill = 0, fillMax = 0; static int n = 0, verts = 0;
    const float ms = 1000.0f / SYSCLOCK_ARM11;
    up += updateTicks * ms; dr += drawTicks * ms; time += dt; verts += vertices; n++;
    // how many times over both screens (400x240 + 320x240 pixels) were painted
    fillArea /= 400.0f * 240 + 320.0f * 240;
    fill += fillArea; if (fillArea > fillMax) fillMax = fillArea;
    if (updateTicks * ms > upMax) upMax = updateTicks * ms;
    if (drawTicks * ms > drMax) drMax = drawTicks * ms;
    // share of the GPU command buffer the last frame used; going over it stops the program
    static float cmdMax = 0;
    float cmd = C3D_GetCmdBufUsage();
    if (cmd > cmdMax) cmdMax = cmd;
    if (n < 180) return;
    LOG("perf: update %.2f ms (max %.2f), draw %.2f ms (max %.2f), %.1f fps, %d vertices, fill x%.2f (max x%.2f), command buffer max %.0f%%, linear free %lu KB, heap free %lu KB",
        up / n, upMax, dr / n, drMax, n / time, verts / n, fill / n, fillMax, cmdMax * 100.0f, (unsigned long)(linearSpaceFree() / 1024), (unsigned long)((envGetHeapSize() - mallinfo().uordblks) / 1024));
    up = dr = upMax = drMax = time = fill = fillMax = cmdMax = 0; n = verts = 0;
}

}  // namespace dev
#else
namespace dev {
void init() {}
void update() {}
void afterFrame() {}
bool quitRequested() { return false; }
bool monkey() { return false; }
float frameDt(float dt) { return dt; }
void frameTimes(float, float, float, int, float) {}
}
#endif
