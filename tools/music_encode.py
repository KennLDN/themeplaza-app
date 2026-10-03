#!/usr/bin/env python3
"""Writes sound as a BCSTM file with DSP-ADPCM samples: the format 3DS themes carry their music in, which
the app already plays (app/source/core/bcstm.cpp). A quarter of the size of plain 16-bit samples.

  write_bcstm(path, pcm, rate)     pcm: int16 array, (frames, channels); the file loops from its start
  music_encode.py check FILE.bcstm ORIGINAL.wav     decodes with the app's own decoder and compares

Layout from 3dbrew's BCSTM page (see docs/themes.md). The encoder is our own:
tools/dspadpcm.c for the frames, and here the choice of the 8 predictors each channel may use.
"""
import ctypes, os, struct, subprocess, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
BUILD = os.path.join(ROOT, 'app', 'build', 'music')
BLOCK_FRAMES = 1024                     # frames of 14 samples in a block: 0x2000 bytes, 0x3800 samples
_lib = None


def lib():
    global _lib
    if _lib is None:
        os.makedirs(BUILD, exist_ok=True)
        so, src = os.path.join(BUILD, 'libdspadpcm.so'), os.path.join(HERE, 'dspadpcm.c')
        if not os.path.exists(so) or os.path.getmtime(so) < os.path.getmtime(src):
            subprocess.run(['gcc', '-O2', '-shared', '-fPIC', '-o', so, src], check=True)
        _lib = ctypes.CDLL(so)
        _lib.dsp_encode.restype = ctypes.c_double
        _lib.dsp_encode.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_void_p]
    return _lib


def predictors(x, rounds=24):
    """Eight pairs (c1, c2) such that predicting each sample as c1 * previous + c2 * one-before leaves as
    little over as possible, each frame using whichever pair suits it. Frames are sorted to the pair that
    predicts them best and each pair is refitted to its frames, in turn, until that settles."""
    x = x.astype(np.float64)
    n = len(x) // 14 * 14
    f = (n - 14) // 14
    a = x[14:14 + f * 14].reshape(f, 14); b = x[13:13 + f * 14].reshape(f, 14); c = x[12:12 + f * 14].reshape(f, 14)
    r11, r22, r12 = (b * b).sum(1), (c * c).sum(1), (b * c).sum(1)
    p1, p2, e0 = (a * b).sum(1), (a * c).sum(1), (a * a).sum(1)
    coefs = np.array([[0.0, 0.0], [0.95, 0.0], [1.6, -0.7], [1.8, -0.85], [1.9, -0.93], [1.4, -0.5], [1.0, -0.3], [0.5, 0.2]])
    for _ in range(rounds):
        err = (e0[:, None] - 2 * (p1[:, None] * coefs[:, 0] + p2[:, None] * coefs[:, 1]) + r11[:, None] * coefs[:, 0] ** 2
               + 2 * r12[:, None] * coefs[:, 0] * coefs[:, 1] + r22[:, None] * coefs[:, 1] ** 2)
        owner = err.argmin(1)
        for k in range(8):
            m = owner == k
            if not m.any(): continue
            A = np.array([[r11[m].sum(), r12[m].sum()], [r12[m].sum(), r22[m].sum()]]) + np.eye(2) * 1e-3
            sol = np.linalg.solve(A, [p1[m].sum(), p2[m].sum()])
            coefs[k] = [min(1.999, max(-1.999, sol[0])), min(0.999, max(-0.999, sol[1]))]
    return np.round(coefs * 2048).astype('<i2').reshape(-1)


def encode_channel(x):
    """(coefficients, bytes, history at the start of every block, starting history)."""
    x = np.ascontiguousarray(x, dtype='<i2')
    coef = np.ascontiguousarray(predictors(x), dtype='<i2')
    # the file loops: what comes before its first sample is its last two
    start = np.array([x[-1], x[-2]], dtype='<i2')
    hist = start.copy()
    out = np.zeros((len(x) + 13) // 14 * 8, dtype=np.uint8)
    seek, per = [], BLOCK_FRAMES * 14
    err = 0.0
    for at in range(0, len(x), per):
        seek.append(tuple(int(v) for v in hist))
        chunk = np.ascontiguousarray(x[at:at + per])
        part = np.zeros((len(chunk) + 13) // 14 * 8, dtype=np.uint8)
        err += lib().dsp_encode(chunk.ctypes.data, len(chunk), coef.ctypes.data, hist.ctypes.data, part.ctypes.data)
        out[at // 14 * 8:at // 14 * 8 + len(part)] = part
    snr = 10 * np.log10((x.astype(np.float64) ** 2).sum() / max(err, 1e-9))
    return coef, out.tobytes(), seek, start, snr


def write_bcstm(path, pcm, rate):
    pcm = np.asarray(pcm, dtype='<i2')
    if pcm.ndim == 1: pcm = pcm[:, None]
    n, channels = pcm.shape
    enc = [encode_channel(pcm[:, c]) for c in range(channels)]
    per = BLOCK_FRAMES * 14
    blocks = (n + per - 1) // per
    last_samples = n - (blocks - 1) * per
    last_size = (last_samples + 13) // 14 * 8
    last_padded = (last_size + 0x1F) // 0x20 * 0x20
    # INFO
    stream = struct.pack('<BBBBIIIIIIIIIIIHHI', 2, 1, channels, 0, rate, 0, n, blocks, BLOCK_FRAMES * 8, per, last_size, last_samples, last_padded,
                         4, per, 0x1F00, 0, 0x18)
    table = struct.pack('<I', channels)
    infos = b''
    info_at = 4 + 8 * channels                             # channel infos follow the table of references
    adpcm_at = info_at + 8 * channels
    for c in range(channels):
        table += struct.pack('<HHI', 0x4102, 0, info_at + 8 * c)
        infos += struct.pack('<HHI', 0x0300, 0, adpcm_at + 0x2E * c - (info_at + 8 * c))
    for c in range(channels):
        coef, data, seek, start, snr = enc[c]
        infos += coef.tobytes() + struct.pack('<BBhhBBhhH', data[0], 0, int(start[0]), int(start[1]), data[0], 0, int(start[0]), int(start[1]), 0)
    body = stream + table + infos
    body += b'\0' * (-(8 + 0x18 + len(body)) % 0x20)
    info = b'INFO' + struct.pack('<I', 8 + 0x18 + len(body)) + struct.pack('<HHIHHiHHI', 0x4100, 0, 0x18, 0x0101, 0, -1, 0x0101, 0, 0x18 + len(stream)) + body
    # SEEK: the two samples before each block, per channel
    seek = b''.join(struct.pack('<hh', *enc[c][2][b]) for b in range(blocks) for c in range(channels))
    seek += b'\0' * (-(8 + len(seek)) % 0x20)
    seek = b'SEEK' + struct.pack('<I', 8 + len(seek)) + seek
    # DATA: block by block, channel by channel
    data = b''
    for b in range(blocks):
        for c in range(channels):
            piece = enc[c][1][b * BLOCK_FRAMES * 8:(b + 1) * BLOCK_FRAMES * 8]
            if b == blocks - 1: piece += b'\0' * (last_padded - len(piece))
            data += piece
    data = b'DATA' + struct.pack('<I', 0x20 + len(data)) + b'\0' * 0x18 + data
    head_size = 0x40
    offs = [head_size, head_size + len(info), head_size + len(info) + len(seek)]
    total = offs[2] + len(data)
    head = struct.pack('<4sHHIIHH', b'CSTM', 0xFEFF, 0x40, 0x02000000, total, 3, 0)
    for typ, off, size in ((0x4000, offs[0], len(info)), (0x4001, offs[1], len(seek)), (0x4002, offs[2], len(data))):
        head += struct.pack('<HHII', typ, 0, off, size)
    head += b'\0' * (head_size - len(head))
    os.makedirs(os.path.dirname(path), exist_ok=True)
    open(path, 'wb').write(head + info + seek + data)
    return [e[4] for e in enc], total


def decode_with_app(path):
    """The file as the app's own decoder (bcstm.cpp, built for the PC) plays it: int16 (frames, channels)."""
    import shutil, tempfile
    out = os.path.join(HERE, 'hosttest', 'out'); os.makedirs(out, exist_ok=True)
    exe = os.path.join(out, 'bcstm_dump')
    core = os.path.join(ROOT, 'app', 'source', 'core')
    subprocess.run(['g++', '-O2', '-std=gnu++20', '-I', os.path.join(HERE, 'hosttest', 'shim'), os.path.join(HERE, 'hosttest', 'bcstm_main.cpp'),
                    os.path.join(core, 'bcstm.cpp'), os.path.join(core, 'pack.cpp'), '-lz', '-o', exe], check=True)
    with tempfile.TemporaryDirectory() as tmp:
        shutil.copyfile(path, os.path.join(tmp, 'bgm.bcstm'))
        raw = os.path.join(tmp, 'out.raw')
        r = subprocess.run([exe, tmp, raw], capture_output=True, text=True)
        if r.returncode: raise RuntimeError('the app\'s decoder could not open it: ' + r.stdout)
        channels = int(r.stdout.split(',')[1].split()[0])
        return np.fromfile(raw, dtype='<i2').reshape(-1, channels), r.stdout.strip()


if __name__ == '__main__':
    import wave
    if len(sys.argv) == 4 and sys.argv[1] == 'check':
        dec, info = decode_with_app(sys.argv[2])
        w = wave.open(sys.argv[3]); orig = np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').reshape(-1, w.getnchannels())
        print(info, '; original', orig.shape)
        m = min(len(dec), len(orig))
        d = dec[:m].astype(float) - orig[:m].astype(float)
        print('signal to error: %.1f dB' % (10 * np.log10((orig[:m].astype(float) ** 2).sum() / (d ** 2).sum())), '; lengths', len(dec), len(orig))
