#!/usr/bin/env python3
"""Runs every automatic check there is, one after the other, and says which passed.

  check_all.py            PC-side checks, then the end-to-end run in the emulator
  check_all.py --quick    PC-side checks only (no emulator, no network)

PC side: file readers on every fixture, the BCSTM decoder against the themes' Ogg copies, and the readers
on damaged files under the address sanitizer. Emulator: emulator/regress.py (needs Azahar and a network).
"""
import os, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
steps = [
    ('file readers on the fixtures', ['python3', os.path.join(ROOT, 'tools', 'hosttest', 'run.py')]),
    ('PNG reader against PIL', ['python3', os.path.join(ROOT, 'tools', 'hosttest', 'png_check.py')]),
    ('SD-card caches, written, read back and damaged', ['python3', os.path.join(ROOT, 'tools', 'hosttest', 'cache_check.py')]),
    ('QR reading under camera-like conditions', ['python3', os.path.join(ROOT, 'tools', 'hosttest', 'qr_bench.py')]),
    ('BCSTM decoder', ['python3', os.path.join(ROOT, 'tools', 'hosttest', 'bcstm_check.py')]),
    ('damaged files under the sanitizer', ['python3', os.path.join(ROOT, 'tools', 'hosttest', 'fuzz.py'), '200']),
]
if '--quick' not in sys.argv:
    steps.append(('end to end in the emulator', ['python3', os.path.join(ROOT, 'emulator', 'regress.py')]))
    # the app is still running after the regression: the damaged-file test once more, by the app itself (32-bit)
    steps.append(('damaged files on the emulated console', ['python3', os.path.join(ROOT, 'emulator', 'fuzz_console.py'), '10']))
results = []
for name, cmd in steps:
    print(f'--- {name}', flush=True)
    r = subprocess.run(cmd, capture_output=True, text=True)
    out = r.stdout + r.stderr
    ok = r.returncode == 0 and 'FAILED' not in out and 'FAIL ' not in out
    print('\n'.join(out.strip().splitlines()[-6:]))
    results.append((name, ok))
print('\n=== summary')
for name, ok in results: print(('ok    ' if ok else 'FAIL  ') + name)
sys.exit(0 if all(ok for _, ok in results) else 1)
