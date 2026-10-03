#!/usr/bin/env python3
"""Reader for the two file kinds inside a start-up logo besides its pictures:
the screen layout (.bclyt, "CLYT") and its animations (.bclan, "CLAN").

Sources: 3dbrew (CLYT format, CLAN format), yellows8's ctr-logobuilder.c, and the closely related, well
documented Wii formats (BRLYT/BRLAN).

  logo_layout.py dump FILE.bcma.lz|FILE.darc      prints every layout and animation in a logo
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import logo as L


def cstr(b):
    return b.split(b'\0')[0].decode('ascii', 'replace')


def sections(d):
    """(magic, header fields, [(name, payload_offset, payload)]) of a CLYT or CLAN file."""
    magic = d[:4]
    bom, hlen, rev, size, count = struct.unpack_from('<HHIII', d, 4)
    assert bom == 0xFEFF and size == len(d), f'{magic}: bom {bom:#x}, size {size} of {len(d)}'
    out, p = [], hlen
    for _ in range(count):
        name = d[p:p + 4]; n = struct.unpack_from('<I', d, p + 4)[0]
        out.append((name.decode('ascii'), p, d[p + 8:p + n]))
        p += n
    assert p == len(d), f'{magic}: sections end at {p}, file is {len(d)}'
    return magic.decode(), {'revision': rev, 'header': hlen}, out


# ---------- CLYT ----------
def pane(b):
    flags, origin, alpha, pad = b[0], b[1], b[2], b[3]
    name = cstr(b[4:0x14]); data = b[0x14:0x1C]
    tx, ty, tz, rx, ry, rz, sx, sy, w, h = struct.unpack_from('<10f', b, 0x1C)
    return {'name': name, 'flags': flags, 'origin': origin, 'alpha': alpha, 'pad': pad, 'data': data.hex(),
            'translate': (tx, ty, tz), 'rotate': (rx, ry, rz), 'scale': (sx, sy), 'size': (w, h)}


def material(b):
    name = cstr(b[:0x14])
    cols = [b[0x14 + 4 * i:0x18 + 4 * i].hex() for i in range(7)]
    flags = struct.unpack_from('<I', b, 0x30)[0]
    n_map, n_mtx, n_gen, n_tev = flags & 3, flags >> 2 & 3, flags >> 4 & 3, flags >> 6 & 7
    p = 0x34
    maps = []
    for _ in range(n_map):
        idx, b0, b1 = struct.unpack_from('<HBB', b, p); p += 4
        maps.append({'texture': idx, 'wrapS': b0 & 3, 'min': b0 >> 2 & 3, 'wrapT': b1 & 3, 'mag': b1 >> 2 & 3})
    mtx = []
    for _ in range(n_mtx):
        mtx.append(struct.unpack_from('<5f', b, p)); p += 20
    gens = []
    for _ in range(n_gen):
        gens.append(tuple(b[p:p + 4])); p += 4
    return {'name': name, 'colours': cols, 'flags': f'{flags:#x}', 'maps': maps, 'matrices': mtx, 'coordgen': gens,
            'tev_stages': n_tev, 'alpha_compare': bool(flags >> 9 & 1), 'blend': bool(flags >> 10 & 1),
            'rest': b[p:].hex()}


def dump_clyt(d, out=print):
    magic, hdr, secs = sections(d)
    out(f'  CLYT revision {hdr["revision"]:#x}, {len(secs)} sections, {len(d)} bytes')
    depth = 0
    for name, off, b in secs:
        ind = '  ' + '  ' * depth
        if name == 'lyt1':
            origin = struct.unpack_from('<I', b, 0)[0]; w, h = struct.unpack_from('<2f', b, 4)
            out(f'{ind}lyt1 origin {origin} canvas {w:g}x{h:g}')
        elif name in ('txl1', 'fnl1'):
            n = struct.unpack_from('<I', b, 0)[0]
            names = [cstr(b[4 + struct.unpack_from("<I", b, 4 + 4 * i)[0]:]) for i in range(n)]
            out(f'{ind}{name} {names}')
        elif name == 'mat1':
            n = struct.unpack_from('<I', b, 0)[0]
            offs = [struct.unpack_from('<I', b, 4 + 4 * i)[0] - 8 for i in range(n)] + [len(b)]
            for i in range(n):
                m = material(b[offs[i]:offs[i + 1]])
                out(f'{ind}mat1[{i}] {m["name"]} flags {m["flags"]} maps {m["maps"]} matrices {[tuple(round(x, 3) for x in t) for t in m["matrices"]]} '
                    f'gen {m["coordgen"]} tev {m["tev_stages"]} acmp {m["alpha_compare"]} blend {m["blend"]}')
                out(f'{ind}        colours {m["colours"]} rest {m["rest"]}')
        elif name in ('pan1', 'pic1', 'bnd1', 'wnd1', 'txt1'):
            p = pane(b)
            line = (f'{ind}{name} "{p["name"]}" flags {p["flags"]:#x} origin {p["origin"]} alpha {p["alpha"]} pos {tuple(round(x, 2) for x in p["translate"])} '
                    f'rot {tuple(round(x, 2) for x in p["rotate"])} scale {tuple(round(x, 3) for x in p["scale"])} size {tuple(round(x, 2) for x in p["size"])}')
            if name == 'pic1':
                cols = [b[0x44 + 4 * i:0x48 + 4 * i].hex() for i in range(4)]
                mat, ntc = struct.unpack_from('<HH', b, 0x54)
                tcs = [tuple(round(x, 3) for x in struct.unpack_from('<8f', b, 0x58 + 0x20 * i)) for i in range(ntc)]
                line += f' vertex colours {cols} material {mat} texcoords {tcs}'
            if p['data'] != '00' * 8 or p['pad']: line += f' data {p["data"]} pad {p["pad"]}'
            out(line)
        elif name == 'grp1':
            n = struct.unpack_from('<I', b, 0x10)[0]
            out(f'{ind}grp1 "{cstr(b[:0x10])}" panes {[cstr(b[0x14 + 0x10 * i:0x24 + 0x10 * i]) for i in range(n)]}')
        elif name in ('pas1', 'grs1'):
            out(f'{ind}{name}'); depth += 1
        elif name in ('pae1', 'gre1'):
            depth -= 1; out(f'  {"  " * depth}{name}')
        else:
            out(f'{ind}{name} ({len(b)} bytes) {b[:48].hex()}')


# ---------- CLAN ----------
TAG_TARGETS = {
    'CLPA': ['translate x', 'translate y', 'translate z', 'rotate x', 'rotate y', 'rotate z', 'scale x', 'scale y', 'size w', 'size h'],
    'CLVI': ['visible'],
    'CLVC': ['top-left r', 'top-left g', 'top-left b', 'top-left a', 'top-right r', 'top-right g', 'top-right b', 'top-right a',
             'bottom-left r', 'bottom-left g', 'bottom-left b', 'bottom-left a', 'bottom-right r', 'bottom-right g', 'bottom-right b', 'bottom-right a', 'pane alpha'],
    'CLTS': ['translate s', 'translate t', 'rotate', 'scale s', 'scale t'],
    'CLMC': ['buffer r', 'buffer g', 'buffer b', 'buffer a', 'konst0 r', 'konst0 g', 'konst0 b', 'konst0 a'],
    'CLTP': ['image'],
}


def parse_clan(d):
    magic, hdr, secs = sections(d)
    res = {'revision': hdr['revision'], 'pat1': None, 'pai1': None}
    for name, off, b in secs:
        full = d[off:off + 8 + len(b)]
        if name == 'pat1':
            order, ngroups, name_off, groups_off, start, end, descend = struct.unpack_from('<HHIIhhB', full, 8)
            res['pat1'] = {'order': order, 'name': cstr(full[name_off:]), 'groups': [cstr(full[groups_off + 0x14 * i:groups_off + 0x14 * (i + 1)]) for i in range(ngroups)],
                           'start': start, 'end': end, 'descending': descend, 'raw': full.hex()}
        elif name == 'pai1':
            frames, loop, pad, ntex, nent, ent_off = struct.unpack_from('<HBBHHI', full, 8)
            tex = [cstr(full[0x14 + struct.unpack_from('<I', full, 0x14 + 4 * i)[0]:]) for i in range(ntex)] if ntex else []
            entries = []
            for i in range(nent):
                e = struct.unpack_from('<I', full, ent_off + 4 * i)[0]
                if e + 0x18 > len(full):       # yellows8's tool writes its second entry's offset 12 bytes too far
                    entries.append({'name': f'(offset {e:#x} runs past the section)', 'kind': 0, 'tags': []}); continue
                ename = cstr(full[e:e + 0x14]); ntags, kind = full[e + 0x14], full[e + 0x15]
                tags = []
                for t in range(ntags):
                    to = e + struct.unpack_from('<I', full, e + 0x18 + 4 * t)[0]
                    tmagic = full[to:to + 4].decode('ascii', 'replace'); ntarg = full[to + 4]
                    targets = []
                    for k in range(ntarg):
                        ko = to + struct.unpack_from('<I', full, to + 8 + 4 * k)[0]
                        index, target, dtype, nkeys, kpad, keys_off = struct.unpack_from('<BBHHHI', full, ko)
                        keys = []
                        for q in range(nkeys):
                            if dtype == 2: keys.append(struct.unpack_from('<3f', full, ko + keys_off + 12 * q))
                            else:
                                fr, val = struct.unpack_from('<fH', full, ko + keys_off + 8 * q); keys.append((fr, val))
                        targets.append({'index': index, 'target': target, 'type': dtype, 'keys': keys})
                    tags.append({'magic': tmagic, 'targets': targets})
                entries.append({'name': ename, 'kind': kind, 'tags': tags})
            res['pai1'] = {'frames': frames, 'loop': loop, 'textures': tex, 'entries': entries}
    return res


def dump_clan(d, out=print):
    a = parse_clan(d)
    p, i = a['pat1'], a['pai1']
    out(f'  CLAN revision {a["revision"]:#x}, {len(d)} bytes')
    if p: out(f'    pat1 "{p["name"]}" order {p["order"]} groups {p["groups"]} start {p["start"]} end {p["end"]} descending {p["descending"]}')
    if i:
        out(f'    pai1 {i["frames"]} frames, loop {i["loop"]}, textures {i["textures"]}, {len(i["entries"])} entries')
        for e in i['entries']:
            out(f'      {"material" if e["kind"] else "pane"} "{e["name"]}"')
            for t in e['tags']:
                for g in t['targets']:
                    names = TAG_TARGETS.get(t['magic'], [])
                    what = names[g['target']] if g['target'] < len(names) else f'target {g["target"]}'
                    keys = ', '.join(f'{k[0]:g}:{k[1]:g}' + (f'/{k[2]:.3g}' if len(k) > 2 and k[2] else '') for k in g['keys'])
                    out(f'        {t["magic"]} {what}{f" [{g["index"]}]" if g["index"] else ""} ({"hermite" if g["type"] == 2 else "step"}): {keys}')


def load_logo(path):
    """(archive bytes without the HMAC, HMAC, files) of a logo given packed or unpacked."""
    d = open(path, 'rb').read()
    if d[:4] != b'darc': d = L.lz11_decompress(d)
    size = struct.unpack_from('<I', d, 0x0C)[0]
    return d[:size], d[size:size + 32], L.darc_list(d[:size])


def main():
    if len(sys.argv) < 3 or sys.argv[1] != 'dump': sys.exit(__doc__)
    darc, mac, files = load_logo(sys.argv[2])
    print(f'{sys.argv[2]}: archive {len(darc)} bytes, {len(files)} files')
    for path, data in files:
        print(f'{path} ({len(data)} bytes)')
        if data[:4] == b'CLYT': dump_clyt(data)
        elif data[:4] == b'CLAN': dump_clan(data)
        elif path.endswith('.bclim'):
            w, h, fmt = L.clim_info(data); print(f'  picture {w}x{h} {L.CLIM_FORMATS.get(fmt, fmt)}')


if __name__ == '__main__':
    main()
