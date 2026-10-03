#include "bcstm.h"
#include <cstring>

namespace {
inline u16 rd16(const u8* p) { return p[0] | (p[1] << 8); }
inline u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
}

bool Bcstm::open(const std::string& pack, const char* entry) {
    if (!es.open(pack, entry)) return false;
    return parse();
}

bool Bcstm::parse() {
    u8 head[0x40];
    if (es.read(head, 0x40) != 0x40 || memcmp(head, "CSTM", 4) != 0 || rd16(head + 4) != 0xFEFF) return false;
    u32 infoOff = 0, dataOff = 0; int n = rd16(head + 0x10);
    for (int i = 0; i < n && i < 3; i++) {
        const u8* r = head + 0x14 + 12 * i;
        if (rd16(r) == 0x4000) infoOff = rd32(r + 4);
        if (rd16(r) == 0x4002) dataOff = rd32(r + 4);
    }
    if (infoOff < 0x40 || dataOff <= infoOff || dataOff - infoOff < 0x40 || dataOff > 2 * 1024 * 1024) return false;
    std::vector<u8> d(dataOff + 8);
    memcpy(d.data(), head, 0x40);
    if (es.read(d.data() + 0x40, d.size() - 0x40) != d.size() - 0x40) return false;
    const u8* info = &d[infoOff];
    // every offset below comes from the file: check each against the header's end before using it
    auto inside = [&](u64 off, u32 len) { return off + len <= dataOff; };
    u64 base = infoOff + 8, si = base + rd32(info + 0x0C), ct = base + rd32(info + 0x1C);
    if (!inside(si, 0x38) || !inside(ct, 4)) return false;
    const u8* s = &d[si];
    encoding = s[0]; channels = s[2]; rate = (int)rd32(s + 4);
    blocks = rd32(s + 0x10); blockSize = rd32(s + 0x14); blockSamples = rd32(s + 0x18);
    lastSize = rd32(s + 0x1C); lastSamples = rd32(s + 0x20); lastPadded = rd32(s + 0x24);
    skipToData = rd32(s + 0x34);
    if ((encoding != 1 && encoding != 2) || channels < 1 || channels > 2 || !blocks || blockSize < 8 || blockSize > 0x10000 || rate < 4000 || rate > 48000 || skipToData > 0x1000) return false;
    // a block cannot hold more samples than its bytes allow (2 bytes each as PCM, 14 per 8 bytes as ADPCM)
    u32 most = encoding == 1 ? blockSize / 2 : blockSize / 8 * 14;
    if (!blockSamples || blockSamples > most) return false;
    if (lastSamples > most) lastSamples = most;
    frames = (blocks - 1) * blockSamples + lastSamples;
    if (encoding == 2) {
        u32 count = rd32(&d[ct]);
        if ((int)count < channels) return false;
        for (int c = 0; c < channels; c++) {
            if (!inside(ct + 4 + 8 * c, 8)) return false;
            u64 ci = ct + rd32(&d[ct + 4 + 8 * c + 4]);
            if (!inside(ci, 8)) return false;
            u64 ai = ci + rd32(&d[ci + 4]);
            if (!inside(ai, 0x2E)) return false;
            for (int k = 0; k < 16; k++) coef[c][k] = (s16)rd16(&d[ai + 2 * k]);
            startH1[c] = (s16)rd16(&d[ai + 0x22]); startH2[c] = (s16)rd16(&d[ai + 0x24]);
        }
    }
    std::vector<u8> skip(skipToData);
    if (skipToData && es.read(skip.data(), skipToData) != skipToData) return false;
    raw.resize((size_t)blockSize * channels);
    pcm.resize((size_t)blockSamples * channels);
    block = 0; have = pos = 0;
    for (int c = 0; c < 2; c++) { h1[c] = startH1[c]; h2[c] = startH2[c]; }
    return true;
}

bool Bcstm::nextBlock() {
    if (block >= blocks) return false;
    bool last = block + 1 == blocks;
    u32 bytes = last ? lastPadded : blockSize, used = last ? lastSize : blockSize, samples = last ? lastSamples : blockSamples;
    if (bytes > blockSize) bytes = blockSize;
    if (used > bytes) used = bytes;
    size_t want = (size_t)bytes * channels;
    // the padding after the very last channel's data may be missing from the file
    if (es.read(raw.data(), want) < (size_t)bytes * (channels - 1) + used) return false;
    if (samples > blockSamples) samples = blockSamples;
    if (encoding == 1 && samples > bytes / 2) samples = bytes / 2;
    for (int c = 0; c < channels; c++) {
        const u8* src = &raw[(size_t)c * bytes];
        s16* out = &pcm[c];
        if (encoding == 1) { for (u32 i = 0; i < samples; i++) out[i * channels] = (s16)rd16(src + 2 * i); continue; }
        // DSP-ADPCM: frames of 8 bytes, a header byte (predictor, scale) and 14 four-bit samples
        int a1 = h1[c], a2 = h2[c];
        u32 done = 0;
        for (u32 f = 0; done < samples && (f + 1) * 8 <= bytes; f++) {
            const u8* fr = src + f * 8;
            int pred = fr[0] >> 4, scale = 1 << (fr[0] & 15);
            int c1 = coef[c][(pred & 7) * 2], c2 = coef[c][(pred & 7) * 2 + 1];
            for (int k = 0; k < 14 && done < samples; k++) {
                int nib = (k & 1) ? (fr[1 + k / 2] & 15) : (fr[1 + k / 2] >> 4);
                if (nib >= 8) nib -= 16;
                int v = (nib * scale * 2048 + 1024 + c1 * a1 + c2 * a2) >> 11;
                if (v > 32767) v = 32767; else if (v < -32768) v = -32768;
                out[done * channels] = (s16)v;
                a2 = a1; a1 = v; done++;
            }
        }
        h1[c] = (s16)a1; h2[c] = (s16)a2;
    }
    block++; have = samples; pos = 0;
    return samples > 0;
}

size_t Bcstm::read(s16* out, size_t frames) {
    size_t done = 0;
    while (done < frames) {
        if (pos >= have && !nextBlock()) break;
        size_t n = have - pos < frames - done ? have - pos : frames - done;
        memcpy(out + done * channels, &pcm[(size_t)pos * channels], n * channels * sizeof(s16));
        pos += n; done += n;
    }
    return done;
}

bool Bcstm::rewind() { return es.rewind() && parse(); }
