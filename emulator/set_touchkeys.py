#!/usr/bin/env python3
"""Maps a few keyboard keys to taps on the emulator's touch screen, so that scripts can use touch-only
parts of the HOME Menu (its settings button, the theme and badge pickers) without moving the mouse.

  set_touchkeys.py on      adds the mapping below and switches Azahar's "touch from button" on
  set_touchkeys.py off     switches it off again (the entries stay, unused)

Only run while Azahar is stopped. Keys are sent with emulator/emu.py key NAME as usual.
"""
import os, subprocess, sys

CFG = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/config/azahar-emu/qt-config.ini')
# key name -> (Qt key code, x, y) in bottom-screen pixels
POINTS = {
    '3': (51, 16, 14),     # HOME Menu settings button (top left)
    '4': (52, 160, 60), '5': (53, 160, 100), '6': (54, 160, 140), '7': (55, 160, 180),
    '8': (56, 160, 225),   # bottom middle
    '9': (57, 60, 225),    # bottom left
    '0': (48, 260, 225),   # bottom right
    'c': (67, 80, 120), 'e': (69, 240, 120), 'r': (82, 40, 60), 'u': (85, 280, 60), 'y': (89, 160, 20),
}


def main():
    if subprocess.run(['flatpak', 'ps', '--columns=application'], capture_output=True, text=True).stdout.find('org.azahar_emu.Azahar') >= 0:
        sys.exit('Azahar is running; stop it first')
    on = len(sys.argv) > 1 and sys.argv[1] == 'on'
    lines = open(CFG).read().split('\n')
    out = []
    for line in lines:
        key = line.split('=')[0]
        if key.startswith('touch_from_button_maps\\1\\entries'): continue      # rewritten below
        if key == 'profiles\\1\\use_touch_from_button': line = f'{key}={"true" if on else "false"}'
        elif key == 'profiles\\1\\use_touch_from_button\\default': line = f'{key}={"false" if on else "true"}'
        elif key == 'touch_from_button_maps\\1\\name':
            for i, (name, (code, x, y)) in enumerate(POINTS.items(), 1):
                out.append(f'touch_from_button_maps\\1\\entries\\{i}\\bind="code:{code},engine:keyboard,x:{x},y:{y}"')
                out.append(f'touch_from_button_maps\\1\\entries\\{i}\\bind\\default=false')
            out.append(f'touch_from_button_maps\\1\\entries\\size={len(POINTS)}')
        out.append(line)
    open(CFG, 'w').write('\n'.join(out))
    print('touch keys', 'on' if on else 'off', ' '.join(f'{k}=({x},{y})' for k, (_, x, y) in POINTS.items()))


if __name__ == '__main__':
    main()
