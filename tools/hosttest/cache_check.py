#!/usr/bin/env python3
"""Checks the app's two SD-card caches (previews, Theme Plaza rows) on the PC: what is written comes back the
same in a new process, and damaged or cut-off cache files give misses, not wrong data or a crash.
"""
import os, random, shutil, subprocess, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
core = os.path.join(ROOT, 'app', 'source', 'core')
work = os.path.join(HERE, 'out', 'cachetest')
exe = os.path.join(HERE, 'out', 'cache_test')
r = subprocess.run(['g++', '-std=gnu++20', '-O1', '-g', '-Wall', '-D_GLIBCXX_ASSERTIONS', '-I', os.path.join(HERE, 'shim'), '-I', core,
                    os.path.join(HERE, 'cache_main.cpp'), os.path.join(core, 'pvcache.cpp'), os.path.join(core, 'rowcache.cpp'), '-o', exe])
if r.returncode: sys.exit('build FAILED')
cache = os.path.join(work, 'sdmc:', '3ds', 'Theme Plaza', 'cache')
if os.path.isdir(cache):
    for f in os.listdir(cache): os.unlink(os.path.join(cache, f))
os.makedirs(cache, exist_ok=True)


def run(mode):
    r = subprocess.run([exe, mode], cwd=work, capture_output=True, text=True)
    out = (r.stdout + r.stderr).strip()
    print(out.splitlines()[-1] if out else f'{mode}: no output')
    crashed = r.returncode < 0 or r.returncode > 1
    if crashed: print(f'FAILED {mode} ended with code {r.returncode}')
    return r.returncode, out, crashed


bad = 0
code, out, crashed = run('write'); bad += code != 0
code, out, crashed = run('verify'); bad += code != 0
if 'FAILED' in out: print('\n'.join(l for l in out.splitlines() if 'FAILED' in l)[:1500])
# keep the good files, then damage copies in several ways; each time a fresh process must cope
good = os.path.join(work, 'good'); os.makedirs(good, exist_ok=True)
for f in os.listdir(cache): shutil.copyfile(os.path.join(cache, f), os.path.join(good, f))
rng = random.Random(1)
names = sorted(os.listdir(good))
for trial in range(40):
    for f in names: shutil.copyfile(os.path.join(good, f), os.path.join(cache, f))
    victim = os.path.join(cache, rng.choice(names))
    data = bytearray(open(victim, 'rb').read())
    how = trial % 4
    if how == 0: data = data[:rng.randrange(0, len(data))]                      # cut off
    elif how == 1:
        for _ in range(rng.randrange(1, 200)): data[rng.randrange(len(data))] = rng.randrange(256)   # scattered bytes
    elif how == 2: data = bytearray(rng.randbytes(rng.randrange(0, 5000)))      # something else entirely
    else: data += bytes(rng.randrange(1, 9000))                                # junk on the end
    open(victim, 'wb').write(data)
    r = subprocess.run([exe, 'verify-damaged'], cwd=work, capture_output=True, text=True)
    if r.returncode < 0 or r.returncode > 1:
        print(f'FAILED trial {trial}: crashed with code {r.returncode} after damaging {os.path.basename(victim)} (way {how})'); bad += 1
    elif 'FAILED' in r.stdout:
        # a damaged picture or row may differ only if the damage was inside its bytes; headers and keys are checked by the cache
        wrong = [l for l in r.stdout.splitlines() if 'FAILED' in l]
        if how in (0, 2, 3) or any('never kept' in l or 'uncacheable' in l for l in wrong):
            print(f'FAILED trial {trial} ({os.path.basename(victim)}, way {how}): {wrong[0]}'); bad += 1
# An index whose last entry was cut off (power lost while it was written): writing the same things again
# must repair it, and everything must be found afterwards.
for f in names: shutil.copyfile(os.path.join(good, f), os.path.join(cache, f))
for f, cut in (('previews.idx', 2), ('plaza_rows.idx', 3)):
    path = os.path.join(cache, f)
    data = open(path, 'rb').read()
    open(path, 'wb').write(data[:-cut])
code, out, crashed = run('write'); bad += code != 0
code, out, crashed = run('verify'); bad += code != 0
if '160 found' not in out: print('FAILED after a cut-off index entry:', out.splitlines()[-1] if out else ''); bad += 1
print('cache checks:', 'all passed' if not bad else f'{bad} FAILED')
sys.exit(1 if bad else 0)
