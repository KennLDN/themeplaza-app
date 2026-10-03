#!/usr/bin/env python3
"""Send commands to the running app through its development test channel and collect screenshots.

  ctl.py "press a" "wait 30" "shot home"
  ctl.py --file script.txt

Commands: press BTN | hold BTN FRAMES | touch X Y [FRAMES] | wait FRAMES | shot NAME | exit
Screenshots come back as PNG in emulator/shots/NAME.png (400x480: top screen over bottom screen),
plus NAME@2x.png for viewing.
"""
import os, sys, time
from PIL import Image

HOME = os.path.expanduser('~')
CTL = f'{HOME}/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/Theme Plaza/ctl'
SHOTS = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'shots')


def main():
    args = sys.argv[1:]
    if not args:
        sys.exit(__doc__)
    if args[0] == '--file':
        cmds = [l.strip() for l in open(args[1]) if l.strip() and not l.startswith('#')]
    else:
        cmds = args
    os.makedirs(CTL, exist_ok=True)
    os.makedirs(SHOTS, exist_ok=True)
    seq_file = os.path.join(CTL, 'seq.host')
    seq = int(open(seq_file).read()) + 1 if os.path.exists(seq_file) else int(time.time()) % 100000
    open(seq_file, 'w').write(str(seq))
    names = [c.split()[1] for c in cmds if c.startswith('shot ')]
    tmp = os.path.join(CTL, 'cmd.tmp')
    with open(tmp, 'w') as f:
        f.write(f'{seq}\n' + '\n'.join(cmds) + '\n')
    os.replace(tmp, os.path.join(CTL, 'cmd.txt'))
    ack = os.path.join(CTL, 'ack.txt')
    deadline = time.time() + 120
    while time.time() < deadline:
        try:
            if int(open(ack).read().strip() or -1) == seq:
                break
        except (OSError, ValueError):
            pass
        time.sleep(0.2)
    else:
        sys.exit('timeout: the app did not acknowledge (is it running?)')
    for n in names:
        bmp = os.path.join(CTL, n + '.bmp')
        if os.path.exists(bmp):
            im = Image.open(bmp).convert('RGB')
            im.save(os.path.join(SHOTS, n + '.png'))
            im.resize((800, 960), Image.NEAREST).save(os.path.join(SHOTS, n + '@2x.png'))
            print('shot', n)
        else:
            print('missing', n)


if __name__ == '__main__':
    main()
