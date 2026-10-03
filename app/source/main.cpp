#include <3ds.h>
#include "core/backend.h"
#include "core/log.h"
#include "core/sound.h"
#include "dev/testctl.h"
#include "ui/app.h"
#include "ui/gfx.h"
#include "ui/input.h"

// The .3dsx build gets libctru's default 32 KB main stack unless told otherwise (the CIA takes its size from the RSF).
u32 __stacksize__ = 128 * 1024;

// aptMainLoop, with one difference. The HOME button can be switched off (ui::update does that while files
// are being written and after an install), but the POWER button also opens the HOME Menu, and libctru lets
// that through regardless. While a job runs it is held back until the job is done. After an install the
// app leaves the proper way instead, which restarts the console; otherwise the HOME Menu would wake with
// its old settings in memory.
static bool keepRunning() {
    aptHandleSleep();
    if (aptShouldJumpToHome()) {
        if (ui::g.job) {}
        else if (backend::needsReboot()) return false;
        else aptJumpToHomeMenu();
    }
    return !aptShouldClose();
}

int main() {
    osSetSpeedupEnable(true);      // New 3DS: full clock speed (the CIA also asks for it in its header)
    gfxInitDefault();
    romfsInit();
    cfguInit();
    ptmuInit();
    logx::init();
    LOG("theme plaza start");
    u64 s0 = svcGetSystemTick();
    gfx::init();
    // Nothing is drawn until the whole interface can be: the screens stay black from the end of the HOME Menu's
    // start-up logo to the first frame. A second of bare backgrounds in between looks worse than black.
    u64 sf = svcGetSystemTick();
    gfx::loadAssets();
    u64 s1 = svcGetSystemTick();
    backend::init();
    u64 s2 = svcGetSystemTick();
    sound::init();
    LOG("sound: %s", sound::available() ? "running" : "not available");
    dev::init();
    ui::init();
    u64 s3 = svcGetSystemTick();
    LOG("start: graphics %.0f ms, sprites and fonts %.0f ms, data and network %.0f ms, sound and the rest %.0f ms",
        (double)(sf - s0) * 1000.0 / SYSCLOCK_ARM11, (double)(s1 - sf) * 1000.0 / SYSCLOCK_ARM11, (double)(s2 - s1) * 1000.0 / SYSCLOCK_ARM11, (double)(s3 - s2) * 1000.0 / SYSCLOCK_ARM11);

    u64 last = svcGetSystemTick();
    while (keepRunning() && !ui::g.quit && !dev::quitRequested()) {
        // Waits until the GPU has finished the previous frame. It comes first because the update below
        // replaces and frees textures, which that frame may still be reading.
        gfx::frameBegin();
        u64 now = svcGetSystemTick();
        float dt = (float)((double)(now - last) / SYSCLOCK_ARM11);
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        if (dt < 0.001f) dt = 0.001f;

        dev::update();
        dt = dev::frameDt(dt);
        // the timing leaves out the test channel above: its look at the SD card takes long in the emulator
        u64 t0 = svcGetSystemTick();
        input::update();
        ui::update(dt);
        sound::update(dt);
        u64 t1 = svcGetSystemTick();

        u64 t2 = svcGetSystemTick();
        // The app fades in from black over its first third of a second (the start-up logo fades out to black
        // before it, and the screens stay black while the app loads).
        static float shown = 0;
        const float FADE_IN = 0.33f;
        float veil = shown >= FADE_IN ? 0.0f : (1 - shown / FADE_IN) * (1 - shown / FADE_IN);
        gfx::screenBegin(gfx::TOP);
        ui::drawTop();
        if (veil > 0) gfx::cover(gfx::rgb(0x000000, veil));
        gfx::screenBegin(gfx::BOT);
        ui::drawBottom();
        if (veil > 0) gfx::cover(gfx::rgb(0x000000, veil));
        if (shown < FADE_IN) shown += 1.0f / 60;      // by frames, not by the clock: the first frames after loading are long
        u64 t3 = svcGetSystemTick();
        gfx::frameEnd();
        dev::afterFrame();
        dev::frameTimes((float)(t1 - t0), (float)(t3 - t2), dt, gfx::vertexCount(), gfx::fillArea());
    }

    // The HOME Menu reads themes and badges only when it starts, and would write its old settings back
    // over ours if it simply resumed. A restart makes it load what was installed.
    u64 closing = svcGetSystemTick();
    backend::fini();                 // finishes the writes still queued
    bool reboot = backend::needsReboot();
    LOG("exit: workers stopped after %.0f ms%s", (double)(svcGetSystemTick() - closing) * 1000.0 / SYSCLOCK_ARM11, reboot ? ", restarting the console" : "");
    sound::fini();
    gfx::fini();
    ptmuExit();
    cfguExit();
    romfsExit();
    gfxExit();
    if (reboot) {
        // Ask for the restart and stay here until it happens. Returning would hand control back to the
        // HOME Menu first, which could save its old settings over the new ones.
        APT_HardwareResetAsync();
        for (int i = 0; i < 150; i++) svcSleepThread(100 * 1000 * 1000LL);   // it takes a second or two; give up after 15
    }
    return 0;
}
