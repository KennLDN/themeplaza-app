#!/usr/bin/env python3
"""Checks the app's BCSTM decoder (app/source/core/bcstm.cpp) on the PC.

Each Theme Plaza theme zip carries its music twice: bgm.bcstm for the console and bgm.ogg, a copy
the site made from it. The script decodes the BCSTM with the app's code, decodes the Ogg with
ffmpeg, and reports how well the two agree (1.0 = identical; the Ogg is lossy and mono, so a good
decode gives about 0.97 to 1.0, a wrong one stays near 0).

  bcstm_check.py [ZIP...]     default: the theme zips in emulator/fixtures/Themes
"""
import glob, os, subprocess, sys, zipfile
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'out')
core = os.path.join(ROOT, 'app', 'source', 'core')
exe = os.path.join(OUT, 'bcstm_dump')


def main():
    os.makedirs(OUT, exist_ok=True)
    r = subprocess.run(['g++', '-O2', '-std=gnu++20', '-I', os.path.join(HERE, 'shim'), os.path.join(HERE, 'bcstm_main.cpp'),
                        os.path.join(core, 'bcstm.cpp'), os.path.join(core, 'pack.cpp'), '-lz', '-o', exe])
    if r.returncode: sys.exit('build failed')
    bad = 0
    for z in sys.argv[1:] or sorted(glob.glob(os.path.join(ROOT, 'emulator', 'fixtures', 'Themes', '*.zip'))):
        names = zipfile.ZipFile(z).namelist()
        if 'bgm.bcstm' not in names or 'bgm.ogg' not in names: continue
        r = subprocess.run([exe, z, os.path.join(OUT, 'app.raw')], capture_output=True, text=True)
        if r.returncode: print(f'FAIL  {os.path.basename(z)}: {r.stdout.strip()}'); bad += 1; continue
        rate = int(r.stdout.split(' Hz')[0]); ch = int(r.stdout.split(', ')[1].split(' ')[0])
        open(os.path.join(OUT, 'ref.ogg'), 'wb').write(zipfile.ZipFile(z).read('bgm.ogg'))
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-i', os.path.join(OUT, 'ref.ogg'), '-ac', '1', '-ar', '8000', '-f', 's16le', os.path.join(OUT, 'ogg.raw')])
        subprocess.run(['ffmpeg', '-v', 'error', '-y', '-f', 's16le', '-ar', str(rate), '-ac', str(ch), '-i', os.path.join(OUT, 'app.raw'),
                        '-ac', '1', '-ar', '8000', '-f', 's16le', os.path.join(OUT, 'app8k.raw')])
        a = np.fromfile(os.path.join(OUT, 'app8k.raw'), dtype=np.int16).astype(np.float32)
        b = np.fromfile(os.path.join(OUT, 'ogg.raw'), dtype=np.int16).astype(np.float32)
        n = min(len(a), len(b), 8000 * 20)
        a, b = a[:n] - a[:n].mean(), b[:n] - b[:n].mean()
        best = max(float(np.dot(a[max(0, s):n + min(0, s)], b[max(0, -s):n - max(0, s)]) / (np.linalg.norm(a) * np.linalg.norm(b) + 1e-9)) for s in range(-400, 401, 4))
        ok = best > 0.9
        bad += not ok
        print(f'{"ok  " if ok else "FAIL"}  {os.path.basename(z)[:44]:44s} {rate} Hz x{ch}  agreement {best:.3f}')
    sys.exit(1 if bad else 0)


if __name__ == '__main__':
    main()
