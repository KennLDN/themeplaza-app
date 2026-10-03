// UI sound clip table for romfs:/audio/sfx.bin. Do not edit.
#pragma once

constexpr int AUDIO_RATE = 32728;

// UI sounds: position and length, in samples, inside romfs:/audio/sfx.bin (16-bit mono).
enum SfxId { SFX_MOVE, SFX_PAGE, SFX_TAB, SFX_OK, SFX_BACK, SFX_ERR, SFX_DONE, SFX_OPEN, SFX_CLOSE, SFX_KEY, SFX_DEL, SFX_TICK1, SFX_TICK2, SFX_TICK3, SFX_TICK4, SFX_TICK5, SFX_TICK6, SFX_TICK7, SFX_TICK8, SFX_TICK9, SFX_TICK10, SFX_UNTICK1, SFX_UNTICK2, SFX_UNTICK3, SFX_UNTICK4, SFX_UNTICK5, SFX_UNTICK6, SFX_UNTICK7, SFX_UNTICK8, SFX_UNTICK9, SFX_UNTICK10, SFX_PROG0, SFX_PROG1, SFX_PROG2, SFX_PROG3, SFX_PROG4, SFX_PROG5, SFX_PROG6, SFX_PROG7, SFX_PROG8, SFX_PROG9, SFX_PROG10, SFX_PROG11, SFX_COUNT };
struct SfxClip { int offset, samples; };
inline constexpr SfxClip g_sfx[SFX_COUNT] = {
    {0, 2946},  // MOVE
    {2946, 4582},  // PAGE
    {7528, 6873},  // TAB
    {14401, 9164},  // OK
    {23565, 6873},  // BACK
    {30438, 9819},  // ERR
    {40257, 23565},  // DONE
    {63822, 5564},  // OPEN
    {69386, 4910},  // CLOSE
    {74296, 2291},  // KEY
    {76587, 2946},  // DEL
    {79533, 3928},  // TICK1
    {83461, 3928},  // TICK2
    {87389, 3928},  // TICK3
    {91317, 3928},  // TICK4
    {95245, 3928},  // TICK5
    {99173, 3928},  // TICK6
    {103101, 3928},  // TICK7
    {107029, 3928},  // TICK8
    {110957, 3928},  // TICK9
    {114885, 3928},  // TICK10
    {118813, 3928},  // UNTICK1
    {122741, 3928},  // UNTICK2
    {126669, 3928},  // UNTICK3
    {130597, 3928},  // UNTICK4
    {134525, 3928},  // UNTICK5
    {138453, 3928},  // UNTICK6
    {142381, 3928},  // UNTICK7
    {146309, 3928},  // UNTICK8
    {150237, 3928},  // UNTICK9
    {154165, 3928},  // UNTICK10
    {158093, 2619},  // PROG0
    {160712, 2619},  // PROG1
    {163331, 2619},  // PROG2
    {165950, 2619},  // PROG3
    {168569, 2619},  // PROG4
    {171188, 2619},  // PROG5
    {173807, 2619},  // PROG6
    {176426, 2619},  // PROG7
    {179045, 2619},  // PROG8
    {181664, 2619},  // PROG9
    {184283, 2619},  // PROG10
    {186902, 2619},  // PROG11
};
constexpr int SFX_TOTAL_SAMPLES = 189521;
