#!/usr/bin/env python3
"""Builds the development CIA with a given banner scene, installs it, boots the HOME Menu (whose cursor rests
on the app, so its banner shows on the top screen) and records it: a screenshot every STEP seconds, joined
into one sheet. Says whether the HOME Menu stopped with an error.

  try_banner.py SCENE.cgfx NAME [COUNT] [STEP]     default 12 screenshots, 0.4 s apart
                                                   -> app/build/banner/NAME_run.png (and NAME_run_NN.png)
app/meta/banner.cgfx is replaced by SCENE for the build and put back (or removed) afterwards.
"""
import os, shutil, subprocess, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
META = os.path.join(ROOT, 'app', 'meta', 'banner.cgfx')
OUT = os.path.join(ROOT, 'app', 'build', 'banner')
LOG = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/log/azahar_log.txt')


def run(*a, **kw):
    return subprocess.run(list(a), capture_output=True, text=True, **kw)


def main():
    scene, name = sys.argv[1], sys.argv[2]
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 12
    step = sys.argv[4] if len(sys.argv) > 4 else '0.4'
    had = os.path.exists(META)
    keep = os.path.join(OUT, 'meta_banner_kept.cgfx')
    same = had and os.path.realpath(scene) == os.path.realpath(META)
    if had and not same: shutil.copyfile(META, keep)
    try:
        if not same: shutil.copyfile(scene, META)
        r = run(os.path.join(ROOT, 'app', 'build.sh'), cwd=os.path.join(ROOT, 'app'))
        if r.returncode: sys.exit('build failed: ' + r.stdout[-500:] + r.stderr[-500:])
    finally:
        if not same:
            if had: shutil.copyfile(keep, META)
            else: os.unlink(META)
    emu = ['python3', os.path.join(HERE, 'emu.py')]
    run(*emu, 'stop')
    print(run(*emu, 'install', os.path.join(ROOT, 'app', 'theme-plaza.cia')).stdout.strip())
    print(run(*emu, 'boot').stdout.strip())
    paths = [os.path.join(OUT, f'{name}_run_{i:02d}.png') for i in range(count)]
    keys = ['w:24']
    for p in paths: keys += [f'w:{step}', f'shot:{p}']
    run(*emu, 'key', *keys)
    bad = [l.strip() for l in open(LOG, errors='replace') if 'Fatal error' in l or 'broke execution' in l or 'exception_handler' in l or 'Exception Type' in l]
    print(f'{name}: ' + ('THE HOME MENU STOPPED: ' + bad[0][-120:] if bad else 'no error logged'))
    ims = [Image.open(p).convert('RGB') for p in paths if os.path.exists(p)]
    if ims:
        # the top screen only: the upper half of each screenshot
        tops = [im.crop((0, 0, im.width, im.height // 2)) for im in ims]
        w, h = tops[0].size
        cols = 4; rows = (len(tops) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * (w + 4), rows * (h + 4)), (255, 0, 255))
        for k, im in enumerate(tops): sheet.paste(im, ((k % cols) * (w + 4), (k // cols) * (h + 4)))
        sheet.save(os.path.join(OUT, f'{name}_run.png'))
        print(f'{len(ims)} screenshots: {os.path.join(OUT, name + "_run.png")}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
