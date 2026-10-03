#!/usr/bin/env python3
"""Switches the emulated console between the New 3DS and the original 3DS, or changes the emulated CPU speed.

  set_model.py new | old        old: 64 MB of application memory, one application core
  set_model.py clock PERCENT    Azahar's "CPU clock speed" (100 is normal; lower leaves the app less CPU time per frame)
  set_model.py speed PERCENT    how fast emulated time runs (100 is normal; 10 shows every frame for a sixth of a second)
  set_model.py show

Only run this while Azahar is stopped: it rewrites qt-config.ini, which Azahar saves on exit.
"""
import os, subprocess, sys

CFG = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/config/azahar-emu/qt-config.ini')


def main():
    arg = sys.argv[1]
    lines = open(CFG).read().split('\n')
    if arg == 'show':
        for line in lines:
            if line.split('=')[0] in ('is_new_3ds', 'cpu_clock_percentage', 'frame_limit'): print(line)
        return
    if subprocess.run(['flatpak', 'ps', '--columns=application'], capture_output=True, text=True).stdout.find('org.azahar_emu.Azahar') >= 0:
        sys.exit('Azahar is running; stop it first')
    if arg == 'clock':
        value = str(int(sys.argv[2])); key = 'cpu_clock_percentage'; default = value == '100'
    elif arg == 'speed':
        value = str(int(sys.argv[2])); key = 'frame_limit'; default = value == '100'
    else:
        value = 'true' if arg == 'new' else 'false'; key = 'is_new_3ds'; default = arg == 'new'
    out = []
    for line in lines:
        k = line.split('=')[0]
        if k == key: line = f'{key}={value}'
        elif k == key + '\\default': line = f'{k}={"true" if default else "false"}'
        out.append(line)
    open(CFG, 'w').write('\n'.join(out))
    print(key, '=', value)


if __name__ == '__main__':
    main()
