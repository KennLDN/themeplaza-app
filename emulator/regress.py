#!/usr/bin/env python3
"""End-to-end check of the app in the emulator: drives it through the test channel and verifies what
ends up in the HOME Menu's data and on the SD card from the PC side.

  regress.py [--no-build] [--home]      --home: install the CIA and start the app from the HOME Menu

Needs test content on the emulated SD card (emulator/seed_sd.py) and a network connection.
Prints one line per check and exits non-zero if any failed.
"""
import os, shutil, subprocess, sys
import check_theme

HERE = os.path.dirname(os.path.abspath(__file__))
SD = check_theme.SD
LOG = os.path.join(SD, '3ds', 'Theme Plaza', 'log.txt')
failed = []


def ctl(*cmds):
    r = subprocess.run(['python3', os.path.join(HERE, 'ctl.py'), *cmds], capture_output=True, text=True)
    if r.returncode: sys.exit('test channel: ' + (r.stdout + r.stderr).strip())


def check(name, ok, detail=''):
    print(('ok    ' if ok else 'FAIL  ') + name + (f'  ({detail})' if detail and not ok else ''))
    if not ok: failed.append(name)


def smdh_name(z):
    for i in z.infolist():
        if os.path.basename(i.filename).lower() == 'info.smdh':
            d = z.read(i)
            for lang in (1, 0, 2, 3, 4, 5):
                t = d[8 + lang * 0x200:8 + lang * 0x200 + 0x80].decode('utf-16le').split('\0')[0].strip()
                if t: return t
    return None


def sorted_zips(folder):
    """Zip names in the order the Collection shows them (by name, case-insensitive)."""
    out = []
    for n, z in check_theme.zips(folder).items():
        out.append(((smdh_name(z) or n).lower(), n))
    return [n for _, n in sorted(out)]


def main():
    # start from known settings: Collection sorted by name
    subprocess.run(['python3', os.path.join(HERE, 'emu.py'), 'stop'])
    settings = os.path.join(SD, '3ds', 'Theme Plaza', 'settings.txt')
    if os.path.exists(settings):
        lines = [l for l in open(settings).read().splitlines() if not l.startswith('sort=')]
        open(settings, 'w').write('\n'.join(lines + ['sort=0']) + '\n')
    args = ['python3', os.path.join(HERE, 'cycle.py')] + [f for f in ('--no-build', '--home') if f in sys.argv] + ['wait 200']
    if subprocess.run(args).returncode: sys.exit('could not start the app')
    themes = sorted_zips('Themes'); splashes = sorted_zips('Splashes')
    check('test content present', len(themes) >= 6 and len(splashes) >= 2, f'{len(themes)} themes, {len(splashes)} splashes')

    # single theme: the second in the grid
    ctl('press right', 'wait 20', 'press a', 'wait 360')
    st = check_theme.state()
    check('single theme install', not st['shuffle'] and len(st['themes']) == 1 and st['themes'][0][0] == themes[1], str(st['themes']))
    has_bgm = any(os.path.basename(i.filename).lower() == 'bgm.bcstm' for i in check_theme.zips('Themes')[themes[1]].infolist())
    check('single theme music', (st['themes'][0][1] > 0) == has_bgm if st['themes'] else False)

    # the same without music, through the More menu (item 2)
    ctl('press start', 'wait 30', 'press down', 'press a', 'wait 300')
    st = check_theme.state()
    check('install without BGM', len(st['themes']) == 1 and st['themes'][0] == (themes[1], 0), str(st['themes']))

    # music only, from the third theme (More menu item 1)
    ctl('press right', 'wait 20', 'press start', 'wait 30', 'press a', 'wait 360')
    st = check_theme.state()
    check('install BGM only', len(st['themes']) == 1 and st['themes'][0][0] == themes[1] and st['themes'][0][1] > 0, str(st['themes']))

    # shuffle: tick the first three
    ctl('press left', 'press left', 'press b', 'press right', 'press b', 'press right', 'press b', 'wait 10', 'press a', 'wait 600')
    st = check_theme.state()
    check('shuffle of three', st['shuffle'] and [t[0] for t in st['themes']] == themes[:3], str(st['themes']))

    # dump, then delete the copy
    before = set(os.listdir(os.path.join(SD, 'Themes')))
    ctl('press start', 'wait 30', 'press up', 'press up', 'press a', 'wait 300')
    made = set(os.listdir(os.path.join(SD, 'Themes'))) - before
    check('dump the installed theme', len(made) == 1 and os.path.exists(os.path.join(SD, 'Themes', next(iter(made), 'x'), 'body_LZ.bin')), str(made))
    for name in made:   # remove the copy again so the next run starts from the same Collection
        folder = os.path.join(SD, 'Themes', name)
        if name.startswith('Dumped theme') and os.path.isdir(folder): shutil.rmtree(folder)

    # splashes
    # two ups: the cursor may be in the grid's second row (the dump selects the new theme)
    ctl('press up', 'press up', 'press right', 'press down', 'wait 60', 'press a', 'wait 240')
    st = check_theme.state()
    check('splash install', st['splash'][0] == splashes[0], str(st['splash']))
    ctl('press b', 'wait 90')
    st = check_theme.state()
    check('splash removal', st['splash'] == (None, None), str(st['splash']))

    # badges (sets installed earlier stay installed, so compare with the count before)
    # the first set may be installed from an earlier run: remove it first (B does nothing if it is not)
    ctl('press up', 'press up', 'press right', 'press down', 'wait 60', 'press b', 'wait 420')
    had = check_theme.state()
    # The HOME Menu ends up with exactly the sets this app has installed. Badge data from elsewhere is
    # replaced (and copied to a backup first), and comes back when the app's last set is removed.
    state_file = os.path.join(SD, '3ds', 'Theme Plaza', 'badges.txt')
    own = [l for l in open(state_file).read().split('\n') if l] if os.path.exists(state_file) else []
    ctl('press a', 'wait 420')
    st = check_theme.state()
    check('badge install', st['badge_sets'] == len(own) + 1 and st['badges'] > 0, f"{st['badge_sets']} sets, {st['badges']} badges, {len(own)} installed by the app before")
    ctl('press b', 'wait 720')      # removing rewrites the badge data for every set still installed: slow with many
    st = check_theme.state()
    check('badge removal', st['badge_sets'] == had['badge_sets'] and st['badges'] == had['badges'], f"{st['badge_sets']} sets, {st['badges']} badges")

    # Theme Plaza: the list arrives, a search works
    ctl('press up', 'press up', 'press left', 'press left', 'press r', 'wait 420', 'shot regress_plaza')
    log = open(LOG, errors='replace').read()
    check('no errors logged', 'failed' not in log.lower() and 'http ' not in log, log[-300:])
    print('screenshot of Theme Plaza: emulator/shots/regress_plaza.png')
    print('FAILED: ' + ', '.join(failed) if failed else 'all checks passed')
    sys.exit(1 if failed else 0)


if __name__ == '__main__':
    main()
