#!/usr/bin/env python3
"""Builds the development CIA with a given start-up logo, installs it, boots the HOME Menu, opens the app
and records the logo: a screenshot every STEP seconds, joined into one sheet. Says whether the HOME Menu
stopped with an error.

  try_logo.py LOGO.bcma.lz NAME [COUNT] [STEP]      default 16 screenshots, 0.15 s apart
                                                    -> app/build/logo/NAME_run.png (and NAME_run_NN.png)
app/meta/logo.bcma.lz is replaced by LOGO for the build and put back afterwards.
"""
import os, shutil, subprocess, sys
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
META = os.path.join(ROOT, 'app', 'meta', 'logo.bcma.lz')
OUT = os.path.join(ROOT, 'app', 'build', 'logo')
LOG = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/log/azahar_log.txt')


def run(*a, **kw):
    return subprocess.run(list(a), capture_output=True, text=True, **kw)


def main():
    logo, name = sys.argv[1], sys.argv[2]
    count = int(sys.argv[3]) if len(sys.argv) > 3 else 16
    step = sys.argv[4] if len(sys.argv) > 4 else '0.15'
    same = os.path.realpath(logo) == os.path.realpath(META)
    keep = os.path.join(OUT, 'meta_logo_kept.bcma.lz')
    shutil.copyfile(META, keep)
    try:
        if not same: shutil.copyfile(logo, META)
        r = run(os.path.join(ROOT, 'app', 'build.sh'), cwd=os.path.join(ROOT, 'app'))
        if r.returncode: sys.exit('build failed: ' + r.stdout[-500:] + r.stderr[-500:])
    finally:
        if not same: shutil.copyfile(keep, META)
    emu = ['python3', os.path.join(HERE, 'emu.py')]
    run(*emu, 'stop')
    print(run(*emu, 'install', os.path.join(ROOT, 'app', 'theme-plaza.cia')).stdout.strip())
    print(run(*emu, 'boot').stdout.strip())
    paths = [os.path.join(OUT, f'{name}_run_{i:02d}.png') for i in range(count)]
    keys = ['w:20', 'a']
    for p in paths: keys += [f'w:{step}', f'shot:{p}']
    run(*emu, 'key', *keys)
    bad = [l.strip() for l in open(LOG, errors='replace') if 'Fatal error' in l or 'broke execution' in l or 'exception_handler' in l or 'Exception Type' in l]
    print(f'{name}: ' + ('THE HOME MENU STOPPED: ' + bad[0][-120:] if bad else 'no error logged'))
    ims = [Image.open(p).convert('RGB') for p in paths if os.path.exists(p)]
    if ims:
        w, h = ims[0].size; t = 0.5
        ims = [i.resize((int(w * t), int(h * t))) for i in ims]
        cols = 8; rows = (len(ims) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * (ims[0].width + 4), rows * (ims[0].height + 4)), (255, 0, 255))
        for k, im in enumerate(ims): sheet.paste(im, ((k % cols) * (im.width + 4), (k // cols) * (im.height + 4)))
        sheet.save(os.path.join(OUT, f'{name}_run.png'))
        print(f'{len(ims)} screenshots: {os.path.join(OUT, name + "_run.png")}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
