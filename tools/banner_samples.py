#!/usr/bin/env python3
"""Collects working HOME Menu banners to study: the banner of every title installed in this machine's
emulator (Nintendo's own; kept in app/build/banner/samples for study only, never added to the app) and
the app's own current banner (app/build/banner.bnr, made by bannertool).

  banner_samples.py        extracts them and prints an overview of what each contains

A banner (ExeFS file "banner") is a CBMD container: an LZ11-packed CGFX scene for all regions, optional
CGFX files per language, and a BCWAV sound.
"""
import glob, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import logo as L

OUT = os.path.join(ROOT, 'app', 'build', 'banner', 'samples')
NAND = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/nand/00000000000000000000000000000000/title')
DICTS = ['models', 'textures', 'luts', 'materials', 'shaders', 'cameras', 'lights', 'fogs', 'scenes', 'skeletal anims',
         'material anims', 'visibility anims', 'camera anims', 'light anims', 'fog anims', 'emitters']


def exefs_file(app, want):
    with open(app, 'rb') as f:
        h = f.read(0x200)
        if h[0x100:0x104] != b'NCCH' or not h[0x18F] & 4: return None
        exefs = struct.unpack_from('<I', h, 0x1A0)[0] * 0x200
        if not exefs: return None
        f.seek(exefs); eh = f.read(0x200)
        for i in range(8):
            nm, o, n = struct.unpack_from('<8sII', eh, i * 16)
            if nm.rstrip(b'\0') == want: f.seek(exefs + 0x200 + o); return f.read(n)
    return None


def cbmd_parts(b):
    """(common CGFX unpacked, {language slot: CGFX unpacked}, BCWAV bytes) of a CBMD."""
    assert b[:4] == b'CBMD', 'not CBMD'
    offs = struct.unpack_from('<14I', b, 8)
    wav = struct.unpack_from('<I', b, 0x84)[0]
    ends = sorted([o for o in offs if o] + ([wav] if wav else []) + [len(b)])
    def part(o):
        return b[o:next(e for e in ends if e > o)]
    common = L.lz11_decompress(part(offs[0])) if offs[0] else b''
    langs = {i: L.lz11_decompress(part(o)) for i, o in enumerate(offs[1:], 1) if o}
    return common, langs, (b[wav:] if wav else b'')


def cstr(d, o):
    e = d.index(b'\0', o)
    return d[o:e].decode('ascii', 'replace')


def cgfx_overview(d):
    """{dict name: [entry names]} of a CGFX."""
    assert d[:4] == b'CGFX'
    hlen = struct.unpack_from('<H', d, 6)[0]
    assert d[hlen:hlen + 4] == b'DATA'
    out = {}
    for i, name in enumerate(DICTS):
        p = hlen + 8 + i * 8
        if p + 8 > len(d): break
        count, rel = struct.unpack_from('<II', d, p)
        if not count: continue
        dict_off = p + 4 + rel
        if d[dict_off:dict_off + 4] != b'DICT': out[name] = [f'({count} entries, table not found)']; continue
        n = struct.unpack_from('<I', d, dict_off + 8)[0]
        names = []
        for k in range(n):
            e = dict_off + 0x1C + k * 0x10          # after the header and the root node
            name_rel = struct.unpack_from('<I', d, e + 8)[0]
            names.append(cstr(d, e + 8 + name_rel))
        out[name] = names
    return out


def main():
    os.makedirs(OUT, exist_ok=True)
    seen = {}
    sources = [('ours', os.path.join(ROOT, 'app', 'build', 'banner.bnr'))]
    for app in sorted(glob.glob(os.path.join(NAND, '*', '*', 'content', '*.app'))):
        sources.append(('/'.join(app.split('/')[-4:-2]), app))
    for title, path in sources:
        try:
            b = open(path, 'rb').read() if title == 'ours' else exefs_file(path, b'banner')
        except OSError:
            continue
        if not b or b[:4] != b'CBMD': continue
        try: common, langs, wav = cbmd_parts(b)
        except Exception as e: print(title, 'cannot unpack:', repr(e)); continue
        if common in seen: seen[common].append(title); continue
        seen[common] = [title]
        name = title.replace('/', '_')
        open(os.path.join(OUT, name + '.cgfx'), 'wb').write(common)
        open(os.path.join(OUT, name + '.bcwav'), 'wb').write(wav)
        for i, lg in langs.items(): open(os.path.join(OUT, f'{name}.lang{i}.cgfx'), 'wb').write(lg)
        ov = cgfx_overview(common)
        print(f'{title}: banner {len(b)} bytes, CGFX {len(common)} unpacked, {len(langs)} language files, sound {len(wav)} bytes')
        for k, v in ov.items(): print(f'    {k}: {v}')
    print(f'{len(seen)} distinct banners in {OUT}')


if __name__ == '__main__':
    main()
