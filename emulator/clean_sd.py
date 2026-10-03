#!/usr/bin/env python3
"""Removes what the tests leave on the emulated SD card: "Dumped theme" folders, second copies of downloads,
the test channel's screenshots, the damaged-file test's folder, the app's saved settings (so it starts with
its defaults) and the test-server setting.
Themes, splashes and badges that were downloaded stay.

  clean_sd.py          does it
  clean_sd.py show     only lists what it would remove

Run only while Azahar is stopped.
"""
import os, re, sys

SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc')
show = len(sys.argv) > 1 and sys.argv[1] == 'show'
files, dirs = [], []
themes = os.path.join(SD, 'Themes')
for name in sorted(os.listdir(themes)):
    d = os.path.join(themes, name)
    if os.path.isdir(d) and re.fullmatch(r'Dumped theme( \d+)?', name):
        inside = os.listdir(d)
        if all(f in ('body_LZ.bin', 'bgm.bcstm') for f in inside):      # only what a dump writes
            files += [os.path.join(d, f) for f in inside]; dirs.append(d)
# Second copies of the same Theme Plaza item (the random-input tests download things that are already there
# under another file name): of the files ending in "(ID).zip" with the same id, the one that is also in
# emulator/fixtures stays, or else the first by name.
FIX = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'fixtures')
for kind in ('Themes', 'Splashes', 'Badges'):
    d = os.path.join(SD, kind)
    if not os.path.isdir(d): continue
    by_id = {}
    for name in sorted(os.listdir(d)):
        m = re.search(r'\((\d+)\)\.zip$', name)
        if m: by_id.setdefault(m.group(1), []).append(name)
    for names in by_id.values():
        if len(names) < 2: continue
        keep = next((n for n in names if os.path.exists(os.path.join(FIX, kind, n))), names[0])
        files += [os.path.join(d, n) for n in names if n != keep]
# the test themes with Korean, Chinese and Japanese text (cjk_test.py)
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cjk_test
for name in cjk_test.NAMES:
    if os.path.exists(os.path.join(themes, name)): files.append(os.path.join(themes, name))
ctl = os.path.join(SD, '3ds', 'Theme Plaza', 'ctl')
if os.path.isdir(ctl): files += [os.path.join(ctl, f) for f in os.listdir(ctl) if f.endswith('.bmp')]
for f in ('settings.txt', 'plaza_server.txt', os.path.join('cache', 'preview.ogg')):     # cache/preview.ogg: left behind by older builds
    p = os.path.join(SD, '3ds', 'Theme Plaza', f)
    if os.path.exists(p): files.append(p)
# the damaged-file test's folder (fuzz_console.py): its test files and what the test wrote
fuzz = os.path.join(SD, '3ds', 'Theme Plaza', 'fuzz')
for d, sub, names in os.walk(fuzz, topdown=False):
    files += [os.path.join(d, f) for f in names]
    dirs.append(d)
size = sum(os.path.getsize(f) for f in files)
print(f'{len(files)} files ({size // (1024 * 1024)} MB) and {len(dirs)} folders' + (' would be removed' if show else ' removed'))
if not show:
    for f in files: os.unlink(f)
    for d in dirs: os.rmdir(d)
