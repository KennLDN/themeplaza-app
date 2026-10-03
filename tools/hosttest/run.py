#!/usr/bin/env python3
"""Builds the host test of the app's format readers, runs it on the test fixtures, and makes a contact sheet.

  run.py [FILE...]      default: everything in emulator/fixtures/
Output: tools/hosttest/out/*.png and tools/hosttest/out/sheet.png
"""
import glob, os, re, subprocess, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'out')
core = os.path.join(ROOT, 'app', 'source', 'core')
exe = os.path.join(OUT, 'hosttest')
r = subprocess.run(['g++', '-std=gnu++20', '-O1', '-g', '-Wall', '-D_GLIBCXX_ASSERTIONS', '-I', os.path.join(HERE, 'shim'),
                    os.path.join(HERE, 'main.cpp'), os.path.join(core, 'formats.cpp'), os.path.join(core, 'pack.cpp'), os.path.join(core, 'tex.cpp'),
                    '-lz', '-o', exe])
if r.returncode: sys.exit('build failed')
for f in glob.glob(os.path.join(OUT, '*.rgba')) + glob.glob(os.path.join(OUT, '*.png')): os.unlink(f)
# default: the fixtures, and the sample from across Theme Plaza's history if tools/hosttest/spread.py has fetched it
files = sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'emulator', 'fixtures', '*', '*.zip'))) + sorted(glob.glob(os.path.join(OUT, 'spread', '*.zip')))
r = subprocess.run([exe, OUT] + files)
print('exit', r.returncode)
ims = []
for f in sorted(glob.glob(os.path.join(OUT, '*.rgba')), key=lambda s: [int(t) if t.isdigit() else t for t in re.split(r'(\d+)', s)]):
    m = re.search(r'\.(\d+)x(\d+)\.rgba$', f)
    w, h = int(m.group(1)), int(m.group(2))
    im = Image.frombytes('RGBA', (w, h), open(f, 'rb').read())
    bg = Image.new('RGBA', (w, h), (255, 0, 255, 255)); bg.alpha_composite(im)
    bg.convert('RGB').save(f[:-5] + '.png'); os.unlink(f)
    ims.append((os.path.basename(f).split('.')[0], bg.convert('RGB')))
# contact sheet: one row per file tag
rows = {}
for name, im in ims: rows.setdefault(name.split('_')[0], []).append(im)
H = sum(max(min(im.height, 120) for im in r) + 4 for r in rows.values())
W = max(sum(int(im.width * min(1, 120 / im.height)) + 4 for im in r) for r in rows.values())
sheet = Image.new('RGB', (W, H), (40, 40, 40)); y = 0
for tag, r in rows.items():
    x = 0; rh = max(min(im.height, 120) for im in r)
    for im in r:
        s = min(1, 120 / im.height); t = im.resize((max(1, int(im.width * s)), max(1, int(im.height * s))))
        sheet.paste(t, (x, y)); x += t.width + 4
    y += rh + 4
sheet.save(os.path.join(OUT, 'sheet.png'))
print('sheet', sheet.size)
