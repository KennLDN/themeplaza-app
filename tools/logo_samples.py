#!/usr/bin/env python3
"""Collects working start-up logos to compare with.

  logo_samples.py        fetches into app/build/logo/samples/

Samples:
  makerom       app/meta/logo_base.darc, makerom's built-in "homebrew" logo (already in the project)
  pablomk7      the logo Steveice10's buildtools ship (used by FBI and others; reported to work on Chinese consoles)
  yellows8      the prebuilt logo in yellows8's ctr-logobuilder
  nintendo_*    the logos of titles installed in this machine's emulator (Nintendo's own; kept in the build
                folder for study only, never added to the app)
"""
import glob, os, struct, urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

SAMPLES = os.path.join(ROOT, 'app', 'build', 'logo', 'samples')
NAND = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/nand/00000000000000000000000000000000/title')
UA = {'User-Agent': 'Mozilla/5.0 (format research)'}

FILES = {
    os.path.join(SAMPLES, 'pablomk7.bcma.lz'): 'https://raw.githubusercontent.com/Steveice10/buildtools/master/3ds/logo.bcma.lz',
    os.path.join(SAMPLES, 'yellows8.bcma.lz'): 'https://raw.githubusercontent.com/yellows8/ctr-logobuilder/master/prebuilt_homebrew_logo-padded.lz11',
}


def fetch():
    for path, url in FILES.items():
        if os.path.exists(path) and os.path.getsize(path): print('have ', os.path.relpath(path, ROOT)); continue
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r: data = r.read()
        except OSError as e:
            print('FAILED', url, e); continue
        open(path, 'wb').write(data)
        print('saved', os.path.relpath(path, ROOT), len(data))


def nintendo():
    """Logos of the titles in the emulator's NAND, one file per distinct logo."""
    seen = {}
    for app in sorted(glob.glob(os.path.join(NAND, '*', '*', 'content', '*.app'))):
        try:
            with open(app, 'rb') as f:
                h = f.read(0x200)
                if h[0x100:0x104] != b'NCCH' or not h[0x18F] & 4: continue      # not a program, or encrypted
                off, size = struct.unpack_from('<II', h, 0x198)
                logo = b''
                if size:
                    f.seek(off * 0x200); logo = f.read(size * 0x200)
                else:
                    exefs = struct.unpack_from('<I', h, 0x1A0)[0] * 0x200
                    if exefs:
                        f.seek(exefs); eh = f.read(0x200)
                        for i in range(8):
                            nm, o, n = struct.unpack_from('<8sII', eh, i * 16)
                            if nm.rstrip(b'\0') == b'logo': f.seek(exefs + 0x200 + o); logo = f.read(n)
        except OSError:
            continue
        if len(logo) < 16 or logo[0] != 0x11: continue
        title = '/'.join(app.split('/')[-4:-2])
        seen.setdefault(logo, []).append(title)
    for k, (logo, titles) in enumerate(seen.items()):
        p = os.path.join(SAMPLES, f'nintendo_{k}.bcma.lz')
        open(p, 'wb').write(logo)
        print(f'nintendo_{k}: {len(logo)} bytes stored, used by {len(titles)} titles, e.g. {titles[:3]}')
    if not seen: print('no logos found in the emulator NAND')


def main():
    os.makedirs(SAMPLES, exist_ok=True)
    fetch()
    nintendo()


if __name__ == '__main__':
    main()
