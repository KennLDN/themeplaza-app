// Decoding a theme's music (bgm.bcstm: DSP-ADPCM or PCM16 in blocks) into plain 16-bit samples.
// Layout from 3dbrew's BCSTM page; see docs/themes.md, section 5.
#pragma once
#include <3ds.h>
#include <string>
#include <vector>
#include "pack.h"

struct Decoder {
    int rate = 32000, channels = 2;
    u32 frames = 0;               // the whole length, where the file says (0: not known)
    virtual ~Decoder() {}
    virtual size_t read(s16* out, size_t frames) = 0;   // interleaved; 0 at the end
    virtual bool rewind() = 0;
};

struct Bcstm : Decoder {
    // pack: the theme's zip file or folder; reads its bgm.bcstm (or another entry of it) from start to end.
    bool open(const std::string& pack, const char* entry = "bgm.bcstm");
    size_t read(s16* out, size_t frames) override;
    bool rewind() override;

private:
    bool parse();
    bool nextBlock();
    EntryStream es;
    int encoding = 2;
    u32 blocks = 0, blockSize = 0, blockSamples = 0, lastSize = 0, lastSamples = 0, lastPadded = 0, skipToData = 0;
    s16 coef[2][16] = {}; s16 h1[2] = {}, h2[2] = {}, startH1[2] = {}, startH2[2] = {};
    std::vector<u8> raw; std::vector<s16> pcm;   // one block: bytes per channel, decoded frames
    u32 block = 0, have = 0, pos = 0;
};
