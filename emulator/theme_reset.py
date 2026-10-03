#!/usr/bin/env python3
"""Takes the emulated console's theme extdata away, as on a console whose "Change Theme" screen was never
opened, so the app's message for that case can be tested; or puts it back.

  theme_reset.py away      moves extdata 0x2CC/0x2CD/0x2CE (whichever exist) to emulator/backup/theme_extdata/
  theme_reset.py back      moves them back
  theme_reset.py show

Run only while Azahar is stopped.
"""
import os, shutil, sys

EXT = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/00000000')
HOLD = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'backup', 'theme_extdata')
IDS = ('000002CC', '000002CD', '000002CE')

cmd = sys.argv[1] if len(sys.argv) > 1 else 'show'
os.makedirs(HOLD, exist_ok=True)
for i in IDS:
    here, held = os.path.join(EXT, i), os.path.join(HOLD, i)
    if cmd == 'away' and os.path.isdir(here) and not os.path.exists(held): shutil.move(here, held); print('moved away', i)
    elif cmd == 'back' and os.path.isdir(held):
        if os.path.isdir(here): print('not restored, the console has made a new one:', i)
        else: shutil.move(held, here); print('moved back', i)
    elif cmd == 'show': print(i, 'on the card' if os.path.isdir(here) else 'not on the card', '| held' if os.path.isdir(held) else '')
