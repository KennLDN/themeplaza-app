#!/usr/bin/env python3
"""Writers for the files of a start-up logo: the DARC archive, layouts (CLYT) and animations (CLAN),
with parsers into the same plain-dict models, so that parse -> write gives the original bytes back.

  logo_build.py roundtrip      rebuilds every layout, animation and archive of the sample logos
                               (tools/logo_samples.py) from their parsed models and compares bytes

Models
  layout    {'canvas': (w, h), 'origin': 1, 'textures': [names], 'materials': [material], 'root': pane, 'groups': group}
  material  {'name', 'colours': [7 x 4 bytes: "black" colour, "white" colour, 5 constants], 'maps': [(texture, wrapS, min, wrapT, mag)],
             'matrices': [(tx, ty, rot, sx, sy)], 'gens': [4 bytes each], 'tail': bytes (tev stages, alpha compare, blend), 'flags_hi': bits 6+ of the flags}
  pane      {'kind': 'pan1' | 'pic1', 'name', 'flags', 'origin', 'alpha', 'pad', 'data': 8 bytes, 'translate': (x, y, z), 'rotate': (x, y, z),
             'scale': (x, y), 'size': (w, h), 'children': [pane]}  + for pic1: 'vcols': [4 x 4 bytes], 'material': index, 'texcoords': [[8 floats]]
  group     {'name', 'panes': [names], 'children': [group]}
  animation {'name', 'order', 'groups': [names], 'start', 'end', 'descending', 'frames', 'loop', 'entries': [entry]}
  entry     {'name', 'kind': 0 pane | 1 material, 'tags': [{'magic': 'CLPA'..., 'targets': [{'index', 'target', 'type': 2 hermite | 1 step, 'keys': [...]}]}]}
            hermite keys are (frame, value, slope); step keys are (frame, value)
"""
import glob, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import logo as L

REVISION = 0x02020000


def cstr(b):
    return b.split(b'\0')[0].decode('ascii')


def fixed(s, n):
    b = s.encode('ascii')
    assert len(b) < n, f'name "{s}" is too long for {n} bytes'
    return b + bytes(n - len(b))


def pad4(b):
    return b + bytes(-len(b) % 4)


def _file(magic, secs):
    body = b''.join(name.encode() + struct.pack('<I', 8 + len(p)) + p for name, p in secs)
    return magic + struct.pack('<HHIII', 0xFEFF, 0x14, REVISION, 0x14 + len(body), len(secs)) + body


def _sections(d, magic):
    assert d[:4] == magic
    bom, hlen, rev, size, count = struct.unpack_from('<HHIII', d, 4)
    assert (bom, hlen, rev, size) == (0xFEFF, 0x14, REVISION, len(d)), 'unexpected header'
    out, p = [], hlen
    for _ in range(count):
        n = struct.unpack_from('<I', d, p + 4)[0]
        out.append((d[p:p + 4].decode(), d[p + 8:p + n])); p += n
    assert p == len(d)
    return out


# ---------- layout ----------
def _parse_pane(kind, b):
    p = {'kind': kind, 'flags': b[0], 'origin': b[1], 'alpha': b[2], 'pad': b[3], 'name': cstr(b[4:0x14]), 'data': b[0x14:0x1C],
         'translate': struct.unpack_from('<3f', b, 0x1C), 'rotate': struct.unpack_from('<3f', b, 0x28),
         'scale': struct.unpack_from('<2f', b, 0x34), 'size': struct.unpack_from('<2f', b, 0x3C), 'children': []}
    assert b[4:0x14] == fixed(p['name'], 0x10), 'pane name with bytes after its end'
    if kind == 'pic1':
        p['vcols'] = [b[0x44 + 4 * i:0x48 + 4 * i] for i in range(4)]
        p['material'], n = struct.unpack_from('<HH', b, 0x54)
        p['texcoords'] = [list(struct.unpack_from('<8f', b, 0x58 + 0x20 * i)) for i in range(n)]
        assert len(b) == 0x58 + 0x20 * n
    else:
        assert kind == 'pan1' and len(b) == 0x44, f'pane kind {kind} is not handled'
    return p


def _write_pane(p):
    b = bytes([p['flags'], p['origin'], p['alpha'], p['pad']]) + fixed(p['name'], 0x10) + p['data']
    b += struct.pack('<10f', *p['translate'], *p['rotate'], *p['scale'], *p['size'])
    if p['kind'] == 'pic1':
        b += b''.join(p['vcols']) + struct.pack('<HH', p['material'], len(p['texcoords']))
        for tc in p['texcoords']: b += struct.pack('<8f', *tc)
    return b


def _parse_material(b):
    m = {'name': cstr(b[:0x14]), 'colours': [b[0x14 + 4 * i:0x18 + 4 * i] for i in range(7)]}
    flags = struct.unpack_from('<I', b, 0x30)[0]
    n_map, n_mtx, n_gen = flags & 3, flags >> 2 & 3, flags >> 4 & 3
    m['flags_hi'] = flags >> 6
    p = 0x34
    m['maps'] = []
    for _ in range(n_map):
        idx, b0, b1 = struct.unpack_from('<HBB', b, p); p += 4
        assert b0 < 16 and b1 < 16
        m['maps'].append((idx, b0 & 3, b0 >> 2, b1 & 3, b1 >> 2))
    m['matrices'] = [struct.unpack_from('<5f', b, p + 20 * i) for i in range(n_mtx)]; p += 20 * n_mtx
    m['gens'] = [b[p + 4 * i:p + 4 * i + 4] for i in range(n_gen)]; p += 4 * n_gen
    m['tail'] = b[p:]
    return m


def _write_material(m):
    flags = len(m['maps']) | len(m['matrices']) << 2 | len(m['gens']) << 4 | m['flags_hi'] << 6
    b = fixed(m['name'], 0x14) + b''.join(m['colours']) + struct.pack('<I', flags)
    for idx, ws, mn, wt, mg in m['maps']: b += struct.pack('<HBB', idx, ws | mn << 2, wt | mg << 2)
    for t in m['matrices']: b += struct.pack('<5f', *t)
    return b + b''.join(m['gens']) + m['tail']


def parse_layout(d):
    lay = {'textures': [], 'materials': []}
    stack, last, gstack, glast = [], None, [], None
    for name, b in _sections(d, b'CLYT'):
        if name == 'lyt1':
            lay['origin'] = struct.unpack_from('<I', b, 0)[0]; lay['canvas'] = struct.unpack_from('<2f', b, 4)
        elif name == 'txl1':
            n = struct.unpack_from('<I', b, 0)[0]
            lay['textures'] = [cstr(b[4 + struct.unpack_from('<I', b, 4 + 4 * i)[0]:]) for i in range(n)]
        elif name == 'mat1':
            n = struct.unpack_from('<I', b, 0)[0]
            offs = [struct.unpack_from('<I', b, 4 + 4 * i)[0] - 8 for i in range(n)] + [len(b)]
            lay['materials'] = [_parse_material(b[offs[i]:offs[i + 1]]) for i in range(n)]
        elif name in ('pan1', 'pic1'):
            last = _parse_pane(name, b)
            if stack: stack[-1]['children'].append(last)
            else: lay['root'] = last
        elif name == 'pas1': stack.append(last)
        elif name == 'pae1': last = stack.pop()
        elif name == 'grp1':
            n = struct.unpack_from('<I', b, 0x10)[0]
            glast = {'name': cstr(b[:0x10]), 'panes': [cstr(b[0x14 + 0x10 * i:0x24 + 0x10 * i]) for i in range(n)], 'children': []}
            if gstack: gstack[-1]['children'].append(glast)
            else: lay['groups'] = glast
        elif name == 'grs1': gstack.append(glast)
        elif name == 'gre1': glast = gstack.pop()
        else: raise ValueError(f'layout section {name} is not handled')
    return lay


def write_layout(lay):
    secs = [('lyt1', struct.pack('<I2f', lay['origin'], *lay['canvas']))]
    names = b''; offs = []
    for t in lay['textures']:
        offs.append(4 * len(lay['textures']) + len(names)); names += t.encode('ascii') + b'\0'
    secs.append(('txl1', struct.pack('<I', len(offs)) + b''.join(struct.pack('<I', o) for o in offs) + pad4(names)))
    mats = [_write_material(m) for m in lay['materials']]
    pos = 8 + 4 + 4 * len(mats); table = b''
    for m in mats: table += struct.pack('<I', pos); pos += len(m)
    secs.append(('mat1', struct.pack('<I', len(mats)) + table + b''.join(mats)))

    def panes(p):
        secs.append((p['kind'], _write_pane(p)))
        if p['children']:
            secs.append(('pas1', b''))
            for c in p['children']: panes(c)
            secs.append(('pae1', b''))
    panes(lay['root'])

    def groups(g):
        secs.append(('grp1', fixed(g['name'], 0x10) + struct.pack('<I', len(g['panes'])) + b''.join(fixed(n, 0x10) for n in g['panes'])))
        if g['children']:
            secs.append(('grs1', b''))
            for c in g['children']: groups(c)
            secs.append(('gre1', b''))
    groups(lay['groups'])
    return _file(b'CLYT', secs)


# ---------- animation ----------
def parse_animation(d):
    a = {}
    for name, b in _sections(d, b'CLAN'):
        full = name.encode() + struct.pack('<I', 8 + len(b)) + b
        if name == 'pat1':
            order, ngroups, name_off, groups_off, start, end, descend = struct.unpack_from('<HHIIhhB', full, 8)
            assert name_off == 0x1C and full[0x19:0x1C] == b'\0\0\0'
            a.update(order=order, name=cstr(full[name_off:groups_off]), start=start, end=end, descending=descend,
                     groups=[cstr(full[groups_off + 0x14 * i:groups_off + 0x14 * (i + 1)]) for i in range(ngroups)])
        elif name == 'pai1':
            frames, loop, pad, ntex, nent, ent_off = struct.unpack_from('<HBBHHI', full, 8)
            assert ntex == 0 and pad == 0 and ent_off == 0x14, 'texture pattern animations are not handled'
            a.update(frames=frames, loop=loop, entries=[])
            for i in range(nent):
                e = struct.unpack_from('<I', full, ent_off + 4 * i)[0]
                ent = {'name': cstr(full[e:e + 0x14]), 'kind': full[e + 0x15], 'tags': []}
                for t in range(full[e + 0x14]):
                    to = e + struct.unpack_from('<I', full, e + 0x18 + 4 * t)[0]
                    tag = {'magic': full[to:to + 4].decode(), 'targets': []}
                    for k in range(full[to + 4]):
                        ko = to + struct.unpack_from('<I', full, to + 8 + 4 * k)[0]
                        index, target, dtype, nkeys, kpad, keys_off = struct.unpack_from('<BBHHHI', full, ko)
                        assert keys_off == 0xC and kpad == 0 and dtype in (1, 2)
                        if dtype == 2: keys = [struct.unpack_from('<3f', full, ko + 0xC + 12 * q) for q in range(nkeys)]
                        else: keys = [struct.unpack_from('<fHH', full, ko + 0xC + 8 * q)[:2] for q in range(nkeys)]
                        tag['targets'].append({'index': index, 'target': target, 'type': dtype, 'keys': keys})
                    ent['tags'].append(tag)
                a['entries'].append(ent)
    return a


def write_animation(a):
    name = pad4(a['name'].encode('ascii') + b'\0')
    while len(name) < 12: name += b'\0'
    pat = struct.pack('<HHIIhhB3x', a['order'], len(a['groups']), 0x1C, 0x1C + len(name), a['start'], a['end'], a['descending'])
    pat += name + b''.join(fixed(g, 0x14) for g in a['groups'])
    ents = []
    for e in a['entries']:
        tags = []
        for t in e['tags']:
            targs = []
            for g in t['targets']:
                keys = b''.join(struct.pack('<3f', *k) if g['type'] == 2 else struct.pack('<fHH', k[0], k[1], 0) for k in g['keys'])
                targs.append(struct.pack('<BBHHHI', g['index'], g['target'], g['type'], len(g['keys']), 0, 0xC) + keys)
            pos = 8 + 4 * len(targs); table = b''
            for x in targs: table += struct.pack('<I', pos); pos += len(x)
            tags.append(t['magic'].encode() + struct.pack('<B3x', len(targs)) + table + b''.join(targs))
        pos = 0x18 + 4 * len(tags); table = b''
        for x in tags: table += struct.pack('<I', pos); pos += len(x)
        ents.append(fixed(e['name'], 0x14) + struct.pack('<BBH', len(tags), e['kind'], 0) + table + b''.join(tags))
    pos = 0x14 + 4 * len(ents); table = b''
    for x in ents: table += struct.pack('<I', pos); pos += len(x)
    pai = struct.pack('<HBBHHI', a['frames'], a['loop'], 0, 0, len(ents), 0x14) + table + b''.join(ents)
    return _file(b'CLAN', [('pat1', pat), ('pai1', pai)])


# ---------- archive ----------
def darc_build(files):
    """files: [(folder, name, data)] with folders in the order they are to appear. Layout as in Nintendo's own logo:
    layouts and animations start at multiples of 0x20, pictures at multiples of 0x80."""
    folders = []
    for f, _, _ in files:
        if f not in folders: folders.append(f)
    entries = [('', None), ('.', None)]
    for f in folders:
        entries.append((f, None))
        entries += [(n, d) for ff, n, d in files if ff == f]
    names = b''; name_off = []
    for n, _ in entries:
        name_off.append(len(names)); names += n.encode('utf-16-le') + b'\0\0'
    table_len = 12 * len(entries) + len(names)
    data_off = (0x1C + table_len + 0x1F) // 0x20 * 0x20
    table = b''; blob = b''; pos = data_off
    total = len(entries)
    for i, (n, d) in enumerate(entries):
        if d is None:
            if i < 2: a, b = 0, total
            else:
                a = 1; b = i + 1
                while b < total and entries[b][1] is not None: b += 1
            table += struct.pack('<III', name_off[i] | 0x01000000, a, b)
        else:
            align = 0x80 if n.endswith('.bclim') else 0x20
            start = (pos + align - 1) // align * align
            blob += bytes(start - pos) + d; pos = start + len(d)
            table += struct.pack('<III', name_off[i], start, len(d))
    head = b'darc' + struct.pack('<HHIIIII', 0xFEFF, 0x1C, 0x01000000, pos, 0x1C, table_len, data_off)
    return head + table + names + bytes(data_off - 0x1C - table_len) + blob


def darc_files(d):
    """[(folder, name, data)] in archive order."""
    out = []
    for path, data in L.darc_list(d):
        parts = path.split('/')
        out.append((parts[-2], parts[-1], data))
    return out


# ---------- pictures ----------
def clim_a4(alpha):
    """A BCLIM in the 4-bit alpha format (what Nintendo's own logo uses for its shapes) from a PIL 'L' image whose
    sides are powers of two, at least 8. The colour comes from the material that uses it."""
    w, h = alpha.size
    assert w >= 8 and h >= 8 and w & (w - 1) == 0 and h & (h - 1) == 0, f'{w}x{h}: sides must be powers of two, 8 or more'
    px = alpha.load()
    data = bytearray(w * h // 2)
    for y in range(h):
        for x in range(w):
            i = L.tiled_index(x, y, w)
            data[i // 2] |= ((px[x, y] * 15 + 127) // 255) << (4 * (i & 1))
    foot = b'CLIM' + struct.pack('<HHIII', 0xFEFF, 0x14, REVISION, len(data) + 0x28, 1) + b'imag' + struct.pack('<IHHII', 0x10, w, h, 13, len(data))
    return bytes(data) + foot


# ---------- signing and packing ----------
KEY_FILE = os.path.expanduser('~/.3ds/logo_hmackey_text')
LIMIT = 0x2000


def finish(files):
    """The logo file for makerom's -logo from [(folder, name, data)]: archive, HMAC-SHA256 with the HOME Menu's
    key (tools/find_logo_key.py; kept in ~/.3ds, never in the project), LZ11, padded to exactly 0x2000 bytes.
    Returns (file bytes, packed size before padding)."""
    import hashlib, hmac
    if not os.path.exists(KEY_FILE): sys.exit(f'{KEY_FILE} is missing: run tools/find_logo_key.py first')
    key = bytes.fromhex(open(KEY_FILE).read().strip())
    base = open(os.path.join(ROOT, 'app', 'meta', 'logo_base.darc'), 'rb').read()      # a logo known to be signed correctly
    size = struct.unpack_from('<I', base, 0x0C)[0]
    if hmac.new(key, base[:size], hashlib.sha256).digest() != base[size:size + 32]: sys.exit('the key does not reproduce a known logo signature')
    darc = darc_build(files)
    signed = darc + hmac.new(key, darc, hashlib.sha256).digest()
    packed = L.lz11_compress(signed)
    assert L.lz11_decompress(packed) == signed, 'LZ11 round trip failed'
    if len(packed) > LIMIT: return None, len(packed)
    return packed + bytes(LIMIT - len(packed)), len(packed)


# ---------- round trip ----------
def roundtrip():
    import logo_layout as LL
    samples = [os.path.join(ROOT, 'app', 'meta', 'logo_base.darc')] + sorted(glob.glob(os.path.join(ROOT, 'app', 'build', 'logo', 'samples', '*.bcma.lz')))
    bad = 0
    for s in samples:
        darc, mac, files = LL.load_logo(s)
        ok = n = 0
        for path, data in files:
            if data[:4] == b'CLYT': again = write_layout(parse_layout(data))
            elif data[:4] == b'CLAN':
                try: again = write_animation(parse_animation(data))
                except (AssertionError, struct.error, IndexError) as e: again = b''; print(f'  {path}: cannot parse ({e!r})')
            else: continue
            n += 1
            if again == data: ok += 1
            else:
                diff = next((i for i in range(min(len(again), len(data))) if again[i] != data[i]), min(len(again), len(data)))
                print(f'  {path}: differs at {diff:#x} (ours {len(again)} bytes, theirs {len(data)})')
        whole = darc_build(darc_files(darc)) == darc
        print(f'{os.path.basename(s)}: {ok} of {n} layouts and animations identical; archive {"identical" if whole else "DIFFERS"}')
        bad += (ok != n) + (not whole)
    return bad


if __name__ == '__main__':
    if len(sys.argv) > 1 and sys.argv[1] == 'roundtrip': sys.exit(1 if roundtrip() else 0)
    sys.exit(__doc__)
