#!/usr/bin/env python3
"""Copies a few of the older Theme Plaza items fetched by tools/hosttest/spread.py onto the emulated SD card,
chosen because they differ from the regular fixtures: a scrolling top screen, a plain-colour bottom screen,
one of the site's first uploads, music present but switched off, no music at all, and a splash.

  seed_spread.py
"""
import glob, os, shutil

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc')
SRC = os.path.join(ROOT, 'tools', 'hosttest', 'out', 'spread')
PICKS = [('Themes', 36703), ('Themes', 96226), ('Themes', 551), ('Themes', 5249), ('Themes', 48343), ('Splashes', 52322),
         # a splash with a top picture only; badge sets with 187 badges, with pictures of several tiles and a palette,
         # with 128x128 pictures, and with interlaced PNGs (spread.py splashes / badges)
         ('Splashes', 63744), ('Badges', 85627), ('Badges', 113444), ('Badges', 138438), ('Badges', 2287)]

for folder, ident in PICKS:
    for src in glob.glob(os.path.join(SRC, f'*({ident}).zip')):
        dst = os.path.join(SD, folder, os.path.basename(src))
        if os.path.exists(dst): print('already there:', os.path.basename(src)); continue
        shutil.copyfile(src, dst)
        print('copied:', folder, os.path.basename(src))
