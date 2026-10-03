#!/usr/bin/env python3
"""Drive the Azahar emulator for testing.

  emu.py stop                 kill the running emulator
  emu.py install FILE.cia     install a CIA (emulator must be stopped)
  emu.py boot                 boot the HOME Menu and wait for the window
  emu.py run FILE.3dsx        run a 3dsx directly (no HOME Menu)
  emu.py key NAME [NAME...]   press 3DS buttons in order (a b x y l r start select up down left right home)
                              NAME:MS holds the button for MS milliseconds
  emu.py shot OUT.png         screenshot the emulator window (both screens)
  emu.py screens OUT.png      screenshot cropped to the two 3DS screens only

Keys are sent to the Azahar window through Hyprland, so the window does not need focus.
"""
import json, os, subprocess, sys, time

HOME = os.path.expanduser('~')
AZ = f'{HOME}/.var/app/org.azahar_emu.Azahar/data/azahar-emu'
HOME_MENU = f'{AZ}/nand/00000000000000000000000000000000/title/00040030/00009802/content/00000089.app'
CLASS = 'org.azahar_emu.Azahar'
LOG = '/tmp/azahar.log'
# 3DS button -> key name in Azahar's default keyboard profile
KEYS = dict(a='A', b='S', x='Z', y='X', l='Q', r='W', start='M', select='N', up='T', down='G', left='F', right='H', home='B')
# taps on the touch screen, once emulator/set_touchkeys.py has mapped these keys (see the points there)
KEYS.update(t3='3', t4='4', t5='5', t6='6', t7='7', t8='8', t9='9', t0='0', tc='C', te='E', tr='R', tu='U', ty='Y')


def sh(*a, **k):
    return subprocess.run(a, capture_output=True, text=True, **k)


def window():
    try:
        for c in json.loads(sh('hyprctl', 'clients', '-j').stdout):
            if c.get('class') == CLASS and c['title'].startswith('Azahar'):
                return c
    except Exception:
        pass
    return None


def stop():
    sh('flatpak', 'kill', 'org.azahar_emu.Azahar')  # not pkill -f: that would also match the calling shell
    for _ in range(40):
        if not window():
            break
        time.sleep(0.25)


def launch(path):
    with open(LOG, 'w') as log:
        subprocess.Popen(['flatpak', 'run', '--command=azahar', 'org.azahar_emu.Azahar', path], stdout=log, stderr=log, start_new_session=True)
    for _ in range(120):
        if window():
            return True
        time.sleep(0.25)
    return False


def send(key, state):
    lua = "hl.dispatch(hl.dsp.send_key_state({mods='', key='%s', state='%s', window='class:%s'}))" % (key, state, CLASS)
    sh('hyprctl', 'eval', lua)


def press(name):
    ms = 90
    if ':' in name:
        name, ms = name.split(':'); ms = int(ms)
    k = KEYS[name.lower()]
    send(k, 'down'); time.sleep(ms / 1000); send(k, 'up'); time.sleep(0.12)


def shot(out, crop=False):
    c = window()
    if not c:
        sys.exit('Azahar window not found')
    x, y = c['at']; w, h = c['size']
    sh('grim', '-g', f'{x},{y} {w}x{h}', out)
    if crop:
        from PIL import Image
        im = Image.open(out)
        # menu bar on top and status bar at the bottom are window chrome; the screens sit between them
        im.crop((0, 21, im.width, im.height - 26)).save(out)


def main():
    a = sys.argv[1:]
    if not a:
        sys.exit(__doc__)
    cmd = a[0]
    if cmd == 'stop':
        stop()
    elif cmd == 'install':
        r = sh('flatpak', 'run', '--command=azahar', 'org.azahar_emu.Azahar', '-i', os.path.realpath(a[1]))
        print((r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else 'no output')
    elif cmd == 'boot':
        print('window' if launch(HOME_MENU) else 'no window')
    elif cmd == 'run':
        print('window' if launch(os.path.realpath(a[1])) else 'no window')
    elif cmd == 'key':
        # tokens: button names, "w:SECONDS" to wait, "shot:PATH" to capture the two screens
        for n in a[1:]:
            if n.startswith('w:'):
                time.sleep(float(n[2:]))
            elif n.startswith('shot:'):
                shot(n[5:], crop=True)
            else:
                press(n)
    elif cmd == 'wait':
        time.sleep(float(a[1]))
    elif cmd == 'shot':
        shot(a[1])
    elif cmd == 'screens':
        shot(a[1], crop=True)
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
