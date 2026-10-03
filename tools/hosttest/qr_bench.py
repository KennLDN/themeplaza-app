#!/usr/bin/env python3
"""How well does the app's QR reading cope with what a camera delivers?

Makes a clean code for a Theme Plaza link (qrgen.py), puts it into 400x240 "camera frames" at different
sizes, angles, amounts of blur, contrast and noise, runs each through what the app does to a frame (RGB565,
the green bits as brightness, the middle 224x224) and has quirc read it. Prints the share of frames read
for each condition. A real camera gives 15 frames a second, so anything above roughly one in five reads
within about a third of a second.
"""
import os, random, subprocess, sys
import numpy as np
from PIL import Image, ImageFilter

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'out')
QUIRC = os.path.join(ROOT, 'app', 'source', 'third_party', 'quirc')
TEXT = 'http://themeplaza.art/download/149689'
TRIALS = 40

objs = []
for c in ('quirc.c', 'decode.c', 'identify.c', 'version_db.c'):
    o = os.path.join(OUT, 'qb_' + c[:-2] + '.o')
    if subprocess.run(['gcc', '-O2', '-DQUIRC_FLOAT_TYPE=float', '-c', os.path.join(QUIRC, c), '-o', o]).returncode: sys.exit('build FAILED')
    objs.append(o)
exe = os.path.join(OUT, 'qr_bench')
if subprocess.run(['g++', '-O2', os.path.join(HERE, 'qr_bench.cpp')] + objs + ['-lm', '-o', exe]).returncode: sys.exit('build FAILED')

sys.path.insert(0, HERE)
import qrgen
QUIET = 4                                           # modules of white around the code
big = qrgen.image(TEXT, 16, QUIET)                  # a clean code, 16 px per module
MODULES = qrgen.SIZE
rng = random.Random(7)
nrng = np.random.default_rng(7)


def frame(width, angle, blur, contrast, noise, surround=96, offset=12):
    """A 224x224 frame as quirc gets it: the code (without its white border) `width` px wide on the 400x240 picture."""
    side = max(8, int(round(width * (MODULES + 2 * QUIET) / MODULES)))
    c = big.resize((side, side), Image.LANCZOS).rotate(angle + rng.uniform(-3, 3), resample=Image.BICUBIC, expand=True, fillcolor=surround)
    pic = Image.new('L', (400, 240), surround)
    pic.paste(c, (200 - c.width // 2 + rng.randint(-offset, offset), 120 - c.height // 2 + rng.randint(-offset, offset)))
    if blur: pic = pic.filter(ImageFilter.GaussianBlur(blur))
    a = np.asarray(pic).astype(np.float32)
    a = (a - 128) * contrast + 128
    if noise: a += nrng.normal(0, noise, a.shape)
    a = np.clip(a, 0, 255).astype(np.uint8)
    g = (a >> 2) << 2                     # six bits of green, as the app takes it from RGB565
    return g[8:232, 88:312].tobytes()     # the middle 224x224


def rate(**kw):
    """The share of frames read by the app (it reads frames alternately as they are and with the middle
    enlarged, both sharpened; a frame counts if either way reads it), and in brackets with no preparation."""
    path = os.path.join(OUT, 'qr_frames.bin')
    base = dict(width=130, angle=0, blur=0.0, contrast=1.0, noise=3.0)
    base.update(kw)
    with open(path, 'wb') as f:
        for _ in range(TRIALS): f.write(frame(**base))
    bits = {m: subprocess.run([exe, path, TEXT, m], capture_output=True, text=True).stdout.strip() for m in '012'}
    n = max(1, len(bits['0']))
    app = sum(1 for a, b in zip(bits['1'], bits['2']) if a == '1' or b == '1') / n
    global last
    last = app
    return f'{app:4.0%} ({bits["0"].count("1") / n:4.0%})'


last = 0.0


print('share of frames read by the app, in brackets with no preparation of the frame (40 frames each; code 130 px wide and a little noise unless said otherwise)')
print('width of the code on the picture:')
for w in (40, 45, 50, 60, 70, 85, 100, 130, 160, 200):
    print(f'  {w:3d} px ({w / 29:.1f} px per module): {rate(width=w)}')
print('angle:')
for a in (0, 15, 30, 45, 90, 180):
    print(f'  {a:3d} degrees: {rate(angle=a)}')
print('blur (out of focus), code 130 px and 90 px wide:')
for b in (0.5, 1.0, 1.5, 2.0, 2.5, 3.0):
    print(f'  radius {b}: {rate(blur=b)}   {rate(blur=b, width=90)}')
print('contrast (a dim screen or a faded print):')
for c in (0.6, 0.4, 0.25, 0.15):
    print(f'  x{c}: {rate(contrast=c)}')
print('sensor noise (standard deviation, of 255):')
for n in (5, 10, 20, 30):
    print(f'  {n:2d}: {rate(noise=n)}')
print('everything a little bad at once (blur 1.2, contrast 0.5, noise 10, 25 degrees):')
for w in (85, 100, 130, 160):
    print(f'  {w:3d} px: {rate(width=w, blur=1.2, contrast=0.5, noise=10, angle=25)}')

# A floor for the automatic checks: these must stay readable (well below what is measured today, so that only
# a real regression trips them).
bad = 0
for what, kw, floor in (('a sharp code 130 px wide', dict(width=130), 0.7), ('a sharp code 85 px wide', dict(width=85), 0.5),
                        ('130 px at 45 degrees', dict(angle=45), 0.6), ('130 px under blur 1.5', dict(blur=1.5), 0.5), ('130 px at low contrast', dict(contrast=0.25), 0.6)):
    rate(**kw)
    if last < floor: print(f'FAILED {what}: {last:.0%} of frames read, at least {floor:.0%} expected'); bad += 1
print('QR reading:', 'within its floors' if not bad else f'{bad} FAILED')
sys.exit(1 if bad else 0)
