#!/usr/bin/env python3
"""Leaves the emulator the way a person would want to find it after the automatic tests:

  - Azahar stopped
  - its settings back to normal: New 3DS model, full CPU speed, no keys mapped to touch-screen taps;
    the outer cameras show the QR test picture (emulator/fixtures/qr_camera.png), so scanning can be tried
  - the emulated SD card tidied (clean_sd.py: dumped themes, duplicate downloads, test screenshots,
    saved settings and the test-server setting removed; seed_sd.py: the fixture files back in place)
  - the release build installed (app/theme-plaza-release.cia; build it first with app/build.sh release)

  handoff.py
"""
import os, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)


def run(*args):
    r = subprocess.run(['python3', os.path.join(HERE, args[0]), *args[1:]], capture_output=True, text=True)
    print(f'{" ".join(args)}: {(r.stdout + r.stderr).strip().splitlines()[-1] if (r.stdout + r.stderr).strip() else "done"}')
    return r.returncode == 0


def main():
    cia = os.path.join(ROOT, 'app', 'theme-plaza-release.cia')
    if not os.path.exists(cia): sys.exit('build the release first: app/build.sh release')
    run('emu.py', 'stop')
    run('set_touchkeys.py', 'off')
    run('set_model.py', 'new')
    run('set_model.py', 'clock', '100')
    run('set_camera.py', os.path.join(HERE, 'fixtures', 'qr_camera.png'))
    run('use_mock.py', 'off')
    run('clean_sd.py')
    run('seed_sd.py')       # puts back fixture files the random-input tests deleted (nothing is downloaded)
    run('emu.py', 'install', cia)
    run('set_model.py', 'show')


if __name__ == '__main__':
    main()
