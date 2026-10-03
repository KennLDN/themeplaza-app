#!/usr/bin/env python3
"""Points the emulator's outer cameras at a still picture, or back to blank.

  set_camera.py PICTURE.png     the picture is copied into the emulator's data folder first
  set_camera.py blank

Only run this while Azahar is stopped: it rewrites qt-config.ini, which Azahar saves on exit.
"""
import os, shutil, subprocess, sys

CFG = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/config/azahar-emu/qt-config.ini')
DATA = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu')


def main():
    if subprocess.run(['flatpak', 'ps', '--columns=application'], capture_output=True, text=True).stdout.find('org.azahar_emu.Azahar') >= 0:
        sys.exit('Azahar is running; stop it first')
    arg = sys.argv[1]
    name, conf = 'blank', ''
    if arg != 'blank':
        dst = os.path.join(DATA, 'camera_test.png')
        shutil.copyfile(arg, dst)
        name, conf = 'image', dst
    out = []
    for line in open(CFG).read().split('\n'):
        key = line.split('=')[0]
        for cam in ('camera_outer_left', 'camera_outer_right'):
            if key == cam + '_name': line = f'{key}={name}'
            elif key == cam + '_config': line = f'{key}={conf}'
            elif key in (cam + '_name\\default', cam + '_config\\default'): line = f'{key}={"true" if name == "blank" else "false"}'
        out.append(line)
    open(CFG, 'w').write('\n'.join(out))
    print('outer cameras:', name, conf)


if __name__ == '__main__':
    main()
