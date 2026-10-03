#!/usr/bin/env python3
"""Makes the emulated SD card look like one the app has never run on, and puts it back.

  first_run.py away     /Themes, /Splashes, /Badges and /3ds/Theme Plaza are renamed to "<name>.away"
  first_run.py back     what the app made meanwhile is removed (only if it is small: a first run's own
                        files), and the folders get their names back

Run only while Azahar is stopped.
"""
import os, shutil, sys

SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc')
NAMES = ['Themes', 'Splashes', 'Badges', os.path.join('3ds', 'Theme Plaza')]


def size(path):
    return sum(os.path.getsize(os.path.join(d, f)) for d, _, fs in os.walk(path) for f in fs)


def main():
    what = sys.argv[1] if len(sys.argv) > 1 else ''
    for name in NAMES:
        p = os.path.join(SD, name); away = p + '.away'
        if what == 'away':
            if os.path.exists(away): print('already away:', name); continue
            if os.path.exists(p): os.rename(p, away); print('moved away:', name)
        elif what == 'back':
            if not os.path.exists(away): print('nothing to put back:', name); continue
            if os.path.exists(p):
                n = size(p)
                if n > 64 * 1024 * 1024: sys.exit(f'{name} made during the test holds {n} bytes; not removing that. Look at it first.')
                shutil.rmtree(p); print(f'removed what the test made: {name} ({n} bytes)')
            os.rename(away, p); print('put back:', name)
        else:
            sys.exit(__doc__)


if __name__ == '__main__':
    main()
