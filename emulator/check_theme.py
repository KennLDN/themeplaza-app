#!/usr/bin/env python3
"""Prints what the emulated console's HOME Menu data says about themes, splashes and badges, and matches
it against the zips on the emulated SD card. Used to verify installs made by the app.
"""
import os, struct, zipfile, zlib

SD = os.path.expanduser('~/.var/app/org.azahar_emu.Azahar/data/azahar-emu/sdmc')
EXT = SD + '/Nintendo 3DS/00000000000000000000000000000000/00000000000000000000000000000000/extdata/00000000/'


def zips(folder):
    out = {}
    d = os.path.join(SD, folder)
    for f in sorted(os.listdir(d)) if os.path.isdir(d) else []:
        if f.endswith('.zip'):
            try: out[f] = zipfile.ZipFile(os.path.join(d, f))
            except zipfile.BadZipFile: pass
    return out


def crc_of(z, name):
    for i in z.infolist():
        if os.path.basename(i.filename).lower() == name.lower(): return i.CRC, i.file_size
    return None, 0


def state():
    """What is applied now, as data: {'shuffle': bool, 'themes': [(body zip or None, bgm size)], 'splash': (top zip, bottom zip), 'badge_sets': n, 'badges': n}."""
    themes = zips('Themes')
    by_crc = {crc_of(z, 'body_LZ.bin')[0]: n for n, z in themes.items()}
    m = open(EXT + '000002CE/user/ThemeManage.bin', 'rb').read()
    s = open(EXT + '00000098/user/SaveData.dat', 'rb').read()
    out = {'shuffle': bool(s[0x141B]), 'themes': [], 'splash': (None, None), 'badge_sets': 0, 'badges': 0}
    if not out['shuffle']:
        size, bgm = struct.unpack('<2I', m[8:16])
        if size and s[0x13B8 + 5] >= 2:
            body = open(EXT + '000002CE/user/BodyCache.bin', 'rb').read()
            out['themes'].append((by_crc.get(zlib.crc32(body[:size])), bgm))
    else:
        sizes = struct.unpack('<10I', m[0x338:0x360]); bsizes = struct.unpack('<10I', m[0x360:0x388])
        rd = open(EXT + '000002CE/user/BodyCache_rd.bin', 'rb').read()
        for i in range(10):
            if sizes[i] and s[0x13C0 + 8 * i + 5] >= 2:
                out['themes'].append((by_crc.get(zlib.crc32(rd[i * 0x150000:i * 0x150000 + sizes[i]])), bsizes[i]))
    sp = []
    for name in ('splash.bin', 'splashbottom.bin'):
        p = os.path.join(SD, 'luma', name)
        if not os.path.exists(p): sp.append(None); continue
        c = zlib.crc32(open(p, 'rb').read())
        sp.append(next((n for n, z in zips('Splashes').items() if crc_of(z, name)[0] == c), 'unknown'))
    out['splash'] = tuple(sp)
    b = EXT + '000014D1/user/BadgeMngFile.dat'
    if os.path.exists(b):
        d = open(b, 'rb').read()
        out['badge_sets'], out['badges'] = struct.unpack('<2I', d[4:12])
    return out


def main():
    themes = zips('Themes')
    by_crc = {crc_of(z, 'body_LZ.bin')[0]: n for n, z in themes.items()}
    bgm_by_crc = {crc_of(z, 'bgm.bcstm')[0]: n for n, z in themes.items()}
    m = open(EXT + '000002CE/user/ThemeManage.bin', 'rb').read()
    s = open(EXT + '00000098/user/SaveData.dat', 'rb').read()
    head = struct.unpack('<8I', m[:32])
    print('ThemeManage:', ' '.join('%X' % x for x in head))
    shuffle = s[0x141B]
    print('SaveData: shuffle flag', shuffle, '| theme entry', s[0x13B8:0x13C0].hex())

    def describe(body, size, bgm, bgm_size, label):
        c = zlib.crc32(body[:size])
        line = f'{label}: body {size} bytes -> {by_crc.get(c, "NOT A THEME ON THE SD CARD")}'
        if bgm_size:
            ok = bgm[:4] == b'CSTM'
            line += f' | bgm {bgm_size} bytes, magic {"ok" if ok else "BAD"} -> {bgm_by_crc.get(zlib.crc32(bgm[:bgm_size]), "no match")}'
        else:
            line += ' | no bgm'
        print(line)

    if not shuffle:
        if head[2]:
            describe(open(EXT + '000002CE/user/BodyCache.bin', 'rb').read(), head[2], open(EXT + '000002CE/user/BgmCache.bin', 'rb').read(), head[3], 'single')
        else:
            print('single: no custom theme')
    else:
        sizes = struct.unpack('<10I', m[0x338:0x360]); bsizes = struct.unpack('<10I', m[0x360:0x388])
        rd = open(EXT + '000002CE/user/BodyCache_rd.bin', 'rb').read()
        for i in range(10):
            entry = s[0x13C0 + 8 * i:0x13C8 + 8 * i]
            if not sizes[i]:
                if any(entry): print(f'slot {i}: entry {entry.hex()} but no body size')
                continue
            bgm = open(EXT + f'000002CE/user/BgmCache_{i:02d}.bin', 'rb').read() if bsizes[i] else b''
            describe(rd[i * 0x150000:], sizes[i], bgm, bsizes[i], f'slot {i} (entry {entry.hex()})')

    for name, size in (('splash.bin', 288000), ('splashbottom.bin', 230400)):
        p = os.path.join(SD, 'luma', name)
        if not os.path.exists(p): print(name + ': not present'); continue
        data = open(p, 'rb').read()
        who = [n for n, z in zips('Splashes').items() if crc_of(z, name)[0] == zlib.crc32(data)]
        print(f'{name}: {len(data)} bytes ({"right size" if len(data) == size else "WRONG SIZE"}) -> {who or "no match"}')

    b = EXT + '000014D1/user/BadgeMngFile.dat'
    if os.path.exists(b):
        d = open(b, 'rb').read()
        sets, uniq, placed, _, _, total, nnid = struct.unpack('<7I', d[4:32])
        print(f'badges: file {len(d)} bytes, {sets} sets, {uniq} unique, {total} total, {placed} placed, principal id {nnid}')
        names = open(EXT + '000014D1/user/BadgeData.dat', 'rb').read()
        print('badge data file', len(names), 'bytes')
        for i in range(min(sets, 100)):
            e = d[0xA028 + i * 0x30:0xA028 + (i + 1) * 0x30]
            sid, idx, _, n, tot, start = struct.unpack('<6I', e[0x10:0x28])
            nm = names[idx * 0x8A0 + 0x8A:idx * 0x8A0 + 2 * 0x8A].decode('utf-16le').split('\0')[0]
            print(f'  set {i}: id {sid:08X} "{nm}" {n} badges from slot {start}')
    else:
        print('badges: no badge data')


if __name__ == '__main__':
    main()
