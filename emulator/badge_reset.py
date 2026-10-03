#!/usr/bin/env python3
"""Takes the emulated console's badge extdata away, as on a console that never had badges, so the app's
"create it" path can be tested. The folder is moved to emulator/backup/, not deleted.

  badge_reset.py           moves extdata 0x14D1 away
  badge_reset.py show      says whether it exists and lists its files

Run only while Azahar is stopped.
"""
import os, shutil, sys, time

EXT = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/00000000/000014D1')
BACKUP = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'backup')

if len(sys.argv) > 1 and sys.argv[1] == 'show':
    if not os.path.isdir(EXT): print('no badge extdata')
    for d, _, files in os.walk(EXT):
        for f in files:
            p = os.path.join(d, f); print(os.path.relpath(p, EXT), os.path.getsize(p))
elif os.path.isdir(EXT):
    dst = os.path.join(BACKUP, 'extdata_14D1_' + time.strftime('%H%M%S'))
    shutil.move(EXT, dst)
    print('moved to', dst)
else:
    print('no badge extdata to move')
