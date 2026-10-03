#!/usr/bin/env python3
"""Build, restart the emulator on the fresh build, and run test-channel commands.

  cycle.py [--no-build] [--home] "wait 60" "shot name" ...

Default: runs app/theme-plaza.3dsx directly (fast). --home installs theme-plaza.cia and launches it from the
HOME Menu instead (slower; needed for anything touching the HOME Menu's own data).
"""
import os, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
EMU = os.path.join(ROOT, 'emulator', 'emu.py')
CTL = os.path.join(ROOT, 'emulator', 'ctl.py')


def run(*a, **k):
    return subprocess.run(a, **k)


def main():
    args = sys.argv[1:]
    build = '--no-build' not in args
    home = '--home' in args
    cmds = [a for a in args if not a.startswith('--')]
    if build:
        r = run(os.path.join(ROOT, 'app', 'build.sh'), capture_output=True, text=True)
        out = r.stdout + r.stderr
        bad = [l for l in out.splitlines() if 'error' in l.lower() or 'undefined reference' in l]
        if r.returncode != 0 or bad:
            print('\n'.join(bad[:8]) or out[-1500:])
            sys.exit('BUILD FAILED')
        print('build ok')
    run('python3', EMU, 'stop')
    if home:
        run('python3', EMU, 'install', os.path.join(ROOT, 'app', 'theme-plaza.cia'))
        run('python3', EMU, 'boot')
        time.sleep(24)
        # the cursor rests on the app icon after its first launch; A opens it (a second A confirms if asked)
        run('python3', EMU, 'key', 'a', 'w:1.5', 'a', 'w:6')
    else:
        run('python3', EMU, 'run', os.path.join(ROOT, 'app', 'theme-plaza.3dsx'))
        time.sleep(4)
    if cmds:
        r = run('python3', CTL, *cmds)
        sys.exit(r.returncode)


if __name__ == '__main__':
    main()
