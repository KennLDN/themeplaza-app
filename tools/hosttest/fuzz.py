#!/usr/bin/env python3
"""Builds tools/hosttest/fuzz.cpp with the address and undefined-behaviour sanitizers and runs it on
damaged copies of real theme, splash and badge files. Uses the build container (it has a PC compiler
with the sanitizers; the PC itself does not).

  fuzz.py [ITERATIONS]      default 300 damaged copies per file
"""
import glob, os, shutil, subprocess, sys, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
OUT = os.path.join(HERE, 'out')
CORPUS = os.path.join(OUT, 'corpus')
IMAGE = 'docker.io/devkitpro/devkitarm:latest'


def main():
    iterations = sys.argv[1] if len(sys.argv) > 1 else '300'
    os.makedirs(CORPUS, exist_ok=True)
    os.makedirs(os.path.join(OUT, 'zlib'), exist_ok=True)
    for h in ('zlib.h', 'zconf.h', 'zlib_name_mangling.h'):   # the container has the zlib library but not its headers
        shutil.copyfile('/usr/include/' + h, os.path.join(OUT, 'zlib', h))
    # corpus: a few whole zips, and the interesting files out of them
    files = []
    fixtures = sorted(glob.glob(os.path.join(ROOT, 'emulator', 'fixtures', '*', '*.zip')))
    picks = [f for f in fixtures if '/Themes/' in f][:3] + [f for f in fixtures if '/Splashes/' in f][:1] + [f for f in fixtures if '/Badges/' in f][:1]
    for i, z in enumerate(picks):
        dst = os.path.join(CORPUS, f'pack{i}.zip')
        # keep the zips small so each run is quick: drop the big music and preview files
        with zipfile.ZipFile(z) as src, zipfile.ZipFile(dst, 'w', zipfile.ZIP_DEFLATED) as out:
            for info in src.infolist()[:12]:
                if info.file_size < 400000: out.writestr(info, src.read(info))
        files.append(dst)
        with zipfile.ZipFile(z) as src:
            for name, want in (('body_LZ.bin', f'body_LZ_{i}.bin'), ('info.smdh', f'info{i}.smdh'), ('bgm.bcstm', f'bgm{i}.bcstm'), ('preview.png', f'preview{i}.png'), ('icon.png', f'icon{i}.png')):
                if name in src.namelist() and (name != 'bgm.bcstm' or i == 0):
                    p = os.path.join(CORPUS, want)
                    data = src.read(name)
                    open(p, 'wb').write(data[:600000] if name == 'bgm.bcstm' else data)
                    files.append(p)
            pngs = [n for n in src.namelist() if n.lower().endswith('.png') and 'preview' not in n and 'icon' not in n][:3]
            for k, n in enumerate(pngs):
                p = os.path.join(CORPUS, f'badge{i}_{k}.png'); open(p, 'wb').write(src.read(n)); files.append(p)
    # theme bodies of other kinds than the fixtures have (a scrolling top screen, a plain-colour bottom screen,
    # an early upload), if tools/hosttest/spread.py has fetched them
    for ident in ('36703', '96226', '551'):
        for z in glob.glob(os.path.join(OUT, 'spread', f'*({ident}).zip')):
            with zipfile.ZipFile(z) as src:
                if 'body_LZ.bin' in src.namelist():
                    p = os.path.join(CORPUS, f'body_LZ_spread_{ident}.bin'); open(p, 'wb').write(src.read('body_LZ.bin')); files.append(p)
    # interlaced and low-depth PNGs, written by png_check.py (run that first; skipped if it has not run)
    for name in ('rgba_37x21_1.png', 'pal2_37x21_1.png', 'grey1_9x9_1.png', 'rgba16_37x21_1.png', 'ga_3x17_1.png'):
        src = os.path.join(OUT, 'pngtest', name)
        if os.path.exists(src):
            p = os.path.join(CORPUS, 'adam7_' + name); shutil.copyfile(src, p); files.append(p)
    rel = [os.path.relpath(f, ROOT) for f in files]
    core = 'app/source/core'
    build = (f'g++ -std=gnu++20 -O1 -g -fsanitize=address,undefined -fno-sanitize-recover=all -Itools/hosttest/shim -Itools/hosttest/out/zlib '
             f'tools/hosttest/fuzz.cpp {core}/formats.cpp {core}/pack.cpp {core}/tex.cpp {core}/bcstm.cpp -l:libz.so.1 -o tools/hosttest/out/fuzz')
    run = 'ASAN_OPTIONS=detect_leaks=0 tools/hosttest/out/fuzz ' + iterations + ' tools/hosttest/out/work ' + ' '.join("'" + r + "'" for r in rel)
    r = subprocess.run(['podman', 'run', '--rm', '-v', f'{ROOT}:/work:Z', '-w', '/work', IMAGE, 'sh', '-c', build + ' && ' + run])
    sys.exit(r.returncode)


if __name__ == '__main__':
    main()
