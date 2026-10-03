#!/usr/bin/env python3
"""Runs the damaged-file test on the emulated console itself (32-bit ARM code), where sizes that wrap around
behave differently from the 64-bit PC run under the sanitizer.

  fuzz_console.py [COPIES_PER_FILE]      default 20

The app (development build) must be running. The test files are the PC run's corpus
(tools/hosttest/out/corpus, made by tools/hosttest/fuzz.py); they are copied to sdmc:/3ds/Theme Plaza/fuzz.
A crash or an endless loop shows as the "done" line never arriving in the app's log.
"""
import os, shutil, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc/3ds/Theme Plaza')
CORPUS = os.path.join(ROOT, 'tools', 'hosttest', 'out', 'corpus')


def main():
    copies = sys.argv[1] if len(sys.argv) > 1 else '20'
    dst = os.path.join(SD, 'fuzz')
    os.makedirs(dst, exist_ok=True)
    names = sorted(f for f in os.listdir(CORPUS) if os.path.isfile(os.path.join(CORPUS, f)))
    if not names: sys.exit('no corpus: run tools/hosttest/fuzz.py first')
    for f in names: shutil.copyfile(os.path.join(CORPUS, f), os.path.join(dst, f))
    log = os.path.join(SD, 'log.txt')
    lines = lambda: open(log, errors='replace').read().splitlines()
    first = len(lines())                                   # only lines written from here on are this run's
    r = subprocess.run(['python3', os.path.join(HERE, 'ctl.py'), f'fuzz {copies}'], capture_output=True, text=True)
    if r.returncode: sys.exit('FAILED: the app did not take the command: ' + r.stderr.strip())
    started = time.time()
    while not any(l.startswith('fuzz: ') for l in lines()[first:]):
        if any('unknown command' in l for l in lines()[first:]) or time.time() - started > 60:
            sys.exit('FAILED: the app did not start the test (does this build know the "fuzz" command?)')
        time.sleep(1)
    deadline = time.time() + 3600
    seen = 0
    while time.time() < deadline:
        time.sleep(3)
        mine = lines()[first:]
        ok = [l for l in mine if l.startswith('ok ') or l.startswith('skip ')]
        if len(ok) != seen: seen = len(ok); print(f'{seen} of {len(names)} files done', flush=True)
        done = [l for l in mine if l.startswith('fuzz: done')]
        if done:
            print(done[-1])
            bad = any('FAILED' in l for l in mine) or 'done, -1' in done[-1]
            print('console fuzz:', 'FAILED (a round trip gave wrong data)' if bad else 'passed')
            sys.exit(1 if bad else 0)
    print('FAILED: no "done" line after an hour; the last log lines were:')
    print('\n'.join(open(log, errors='replace').read().splitlines()[-5:]))
    sys.exit(1)


if __name__ == '__main__':
    main()
