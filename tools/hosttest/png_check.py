#!/usr/bin/env python3
"""Checks the app's PNG reader against PIL on pictures written in every form a PNG can take.

Writes one source picture as: RGBA, RGB, RGB with a transparent colour, grey (1, 2, 4, 8 bit), grey with alpha,
palette (1, 2, 4, 8 bit, with transparency), 16-bit RGBA; each with every row filter, at an odd size and at
1x1 .. 9x9, both as a normal PNG and as an interlaced (Adam7) one. Prints FAILED lines for any difference.
"""
import os, struct, subprocess, sys, zlib
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'out', 'pngtest')
os.makedirs(OUT, exist_ok=True)
for f in os.listdir(OUT): os.unlink(os.path.join(OUT, f))


def chunk(t, d): return struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d))


def paeth(a, b, c):
    p = a + b - c; pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    return a if pa <= pb and pa <= pc else (b if pb <= pc else c)


def filt(kind, row, prev, bpp):
    out = bytearray(len(row))
    for i, v in enumerate(row):
        a = row[i - bpp] if i >= bpp else 0; b = prev[i]; c = prev[i - bpp] if i >= bpp else 0
        out[i] = (v - (0, a, b, (a + b) // 2, paeth(a, b, c))[kind]) & 255
    return bytes([kind]) + bytes(out)


def write(name, w, h, ctype, depth, interlace, pixel, palette=None, trns=None, filters=(0,)):
    """pixel(x, y) gives the sample values of one pixel (a tuple)."""
    chans = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    bpp = max(1, chans * depth // 8)

    def rowbytes(xs, y):
        vals = [v for x in xs for v in pixel(x, y)]
        if depth == 8: return bytes(vals)
        if depth == 16: return b''.join(struct.pack('>H', v) for v in vals)
        bits = ''.join(format(v, f'0{depth}b') for v in vals)
        bits += '0' * (-len(bits) % 8)
        return bytes(int(bits[i:i + 8], 2) for i in range(0, len(bits), 8))
    raw = b''; k = 0
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)] if interlace else [(0, 0, 1, 1)]
    for x0, y0, dx, dy in passes:
        xs = list(range(x0, w, dx))
        if not xs: continue
        prev = None
        for y in range(y0, h, dy):
            row = rowbytes(xs, y)
            raw += filt(filters[k % len(filters)], row, prev or bytes(len(row)), bpp); k += 1
            prev = row
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, depth, ctype, 0, 0, interlace))
    if palette: data += chunk(b'PLTE', palette)
    if trns: data += chunk(b'tRNS', trns)
    z = zlib.compress(raw)
    cut = max(1, len(z) // 3)          # several IDAT chunks, as real files have
    for i in range(0, len(z), cut): data += chunk(b'IDAT', z[i:i + cut])
    data += chunk(b'IEND', b'')
    open(os.path.join(OUT, name), 'wb').write(data)


# the source picture: a badge out of the first badge set among the test fixtures (emulator/seed_sd.py fetches them)
import glob, io, zipfile
fixture = sorted(glob.glob(os.path.join(ROOT, 'emulator', 'fixtures', 'Badges', '*.zip')))
if not fixture: sys.exit('no badge zips in emulator/fixtures/Badges: run emulator/seed_sd.py first')
with zipfile.ZipFile(fixture[0]) as z:
    name = next(n for n in z.namelist() if n.lower().endswith('.png') and not n.startswith('_'))
    src = Image.open(io.BytesIO(z.read(name))).convert('RGBA')
sizes = [(src.width, src.height), (37, 21)] + [(n, n) for n in range(1, 10)] + [(1, 9), (9, 1), (3, 17)]
for w, h in sizes:
    im = src.crop((0, 0, w, h)); px = im.load()
    grey = im.convert('L').load()
    tag = f'{w}x{h}'
    for il in (0, 1):
        allf = (0, 1, 2, 3, 4)
        write(f'rgba_{tag}_{il}.png', w, h, 6, 8, il, lambda x, y: px[x, y], filters=allf)
        write(f'rgb_{tag}_{il}.png', w, h, 2, 8, il, lambda x, y: px[x, y][:3], filters=allf)
        key = px[0, 0][:3]
        write(f'rgbkey_{tag}_{il}.png', w, h, 2, 8, il, lambda x, y: px[x, y][:3], trns=struct.pack('>3H', *key), filters=allf)
        write(f'ga_{tag}_{il}.png', w, h, 4, 8, il, lambda x, y: (grey[x, y], px[x, y][3]), filters=allf)
        write(f'rgba16_{tag}_{il}.png', w, h, 6, 16, il, lambda x, y: tuple(v * 257 for v in px[x, y]), filters=allf)
        for depth in (1, 2, 4, 8):
            write(f'grey{depth}_{tag}_{il}.png', w, h, 0, depth, il, lambda x, y: (grey[x, y] >> (8 - depth),), filters=allf if depth == 8 else (0, 2))
            colours = 1 << depth
            q = im.convert('RGB').quantize(colours); qp = q.load()
            pal = bytes((q.getpalette() + [0] * 768)[:colours * 3])
            write(f'pal{depth}_{tag}_{il}.png', w, h, 3, depth, il, lambda x, y: (qp[x, y],), palette=pal, trns=bytes([0, 128]), filters=allf if depth == 8 else (0, 2))

exe = os.path.join(HERE, 'out', 'pngtest_bin')
core = os.path.join(ROOT, 'app', 'source', 'core')
r = subprocess.run(['g++', '-std=gnu++20', '-O1', '-g', '-Wall', '-D_GLIBCXX_ASSERTIONS', '-I', os.path.join(HERE, 'shim'), '-I', core,
                    os.path.join(HERE, 'png_main.cpp'), os.path.join(core, 'formats.cpp'), os.path.join(core, 'tex.cpp'), '-lz', '-o', exe])
if r.returncode: sys.exit('build FAILED')
files = sorted(os.path.join(OUT, f) for f in os.listdir(OUT) if f.endswith('.png'))
r = subprocess.run([exe] + files, capture_output=True, text=True)
refused = [l for l in r.stdout.splitlines() if 'refused' in l or 'cannot' in l]
if r.stderr.strip(): print('FAILED sanitizer output:', r.stderr.strip()[:2000])
bad = 0
for f in files:
    name = os.path.basename(f)
    if not os.path.exists(f + '.rgba'): print('FAILED refused', name); bad += 1; continue
    want = Image.open(f)
    if name.startswith('rgba16'):      # PIL cannot give 16-bit RGBA; the reader keeps the high byte, which is the source picture
        w, h = want.size; want = src.crop((0, 0, w, h))
    want = want.convert('RGBA').tobytes()
    got = open(f + '.rgba', 'rb').read()
    if got != want:
        n = sum(1 for i in range(0, min(len(got), len(want)), 4) if got[i:i + 4] != want[i:i + 4])
        print('FAILED differs', name, f'{n} pixels' if len(got) == len(want) else f'size {len(got)} vs {len(want)}'); bad += 1
print(f'{len(files)} pictures, {bad} wrong, {len(refused)} refused')
sys.exit(1 if bad or r.stderr.strip() else 0)
