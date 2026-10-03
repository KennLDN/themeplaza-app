#!/usr/bin/env python3
"""The start-up logo of the CIA (the short animation the HOME Menu plays before an installed title starts).

  logo.py extract CIA OUTDIR         takes the logo out of a built CIA, unpacks it (LZ11, then the DARC archive)
                                     into OUTDIR, and lists what is inside
  logo.py show FILE.bclim [OUT.png]  prints a picture's size and format, and optionally converts it

A logo is a DARC archive packed with LZ11: a screen layout (.bclyt), its animation (.bclan) and its
pictures (.bclim). Formats from the public 3DS documentation (3dbrew: NCCH, CIA, DARC, LZ11, CLIM).
"""
import os, struct, sys


def lz11_decompress(d):
    assert d[0] == 0x11, 'not LZ11'
    size = d[1] | d[2] << 8 | d[3] << 16
    p = 4
    if size == 0: size = struct.unpack_from('<I', d, 4)[0]; p = 8
    out = bytearray()
    while len(out) < size:
        flags = d[p]; p += 1
        for bit in range(8):
            if len(out) >= size: break
            if not flags & (0x80 >> bit):
                out.append(d[p]); p += 1; continue
            b0 = d[p]; ind = b0 >> 4
            if ind == 0:
                n = ((b0 & 0xF) << 4 | d[p + 1] >> 4) + 0x11; disp = ((d[p + 1] & 0xF) << 8 | d[p + 2]) + 1; p += 3
            elif ind == 1:
                n = ((b0 & 0xF) << 12 | d[p + 1] << 4 | d[p + 2] >> 4) + 0x111; disp = ((d[p + 2] & 0xF) << 8 | d[p + 3]) + 1; p += 4
            else:
                n = ind + 1; disp = ((b0 & 0xF) << 8 | d[p + 1]) + 1; p += 2
            for _ in range(n): out.append(out[-disp])
    return bytes(out)


def cia_ncch(cia):
    """The first content of a CIA (the program's NCCH)."""
    hdr, cert, tik, tmd, meta = struct.unpack_from('<IxxxxIIIIQ', cia, 0)[:5]
    content = struct.unpack_from('<Q', cia, 0x18)[0]
    align = lambda v: (v + 63) // 64 * 64
    off = align(hdr) + align(cert) + align(tik) + align(tmd)
    return cia[off:off + content]


def ncch_logo(ncch):
    assert ncch[0x100:0x104] == b'NCCH', 'no NCCH'
    off, size = struct.unpack_from('<II', ncch, 0x198)
    if size: return ncch[off * 0x200:(off + size) * 0x200]
    # built with -exefslogo: the logo is the ExeFS file "logo"
    exefs = struct.unpack_from('<I', ncch, 0x1A0)[0] * 0x200
    for i in range(8):
        nm, o, n = struct.unpack_from('<8sII', ncch, exefs + i * 16)
        if nm.rstrip(b'\0') == b'logo': return ncch[exefs + 0x200 + o:exefs + 0x200 + o + n]
    return b''


def lz11_compress(d):
    """LZ11 (what the console's decoder accepts: matches within the last 4 KB), choosing the split into literals
    and matches that gives the fewest bytes: the longest match at every place is found first, then the cheapest
    way from each place to the end is worked out backwards. About 3 % smaller than always taking the longest match,
    which matters for the start-up logo's 8 KB."""
    n = len(d)
    # the longest match starting at each position, and how far back it is
    longest, back = [0] * n, [0] * n
    heads = {}
    for p in range(n):
        key = d[p:p + 3]
        if len(key) == 3:
            best_n = best_q = 0
            cands = heads.get(key, ())
            for q in reversed(cands[-48:]):            # the nearest 48 places that start the same way
                if p - q > 0x1000: break
                m = 3
                while m < 0x10110 and p + m < n and d[q + m] == d[p + m]: m += 1
                if m > best_n:
                    best_n, best_q = m, q
                    if p + m >= n or m >= 0x10110: break
            longest[p], back[p] = best_n, p - best_q
            heads.setdefault(key, []).append(p)
    bits = lambda m: 17 if m <= 0x10 else 25 if m <= 0x110 else 33
    cost, take = [0] * (n + 1), [1] * (n + 1)
    for p in range(n - 1, -1, -1):
        cost[p] = 9 + cost[p + 1]
        L = longest[p]
        # every short length, the lengths where a match grows a byte, and the longest: where the cheapest split lies
        for m in [*range(3, min(L, 40) + 1), 0x10, 0x110, L] if L >= 3 else ():
            if m > L: continue
            c = bits(m) + cost[p + m]
            if c <= cost[p]: cost[p], take[p] = c, m
    out = bytearray([0x11, n & 0xFF, n >> 8 & 0xFF, n >> 16 & 0xFF])
    p = 0
    while p < n:
        flag_at = len(out); out.append(0); flags = 0
        for bit in range(8):
            if p >= n: break
            m = take[p]
            if m >= 3:
                flags |= 0x80 >> bit; disp = back[p] - 1
                if m <= 0x10:
                    out += bytes([(m - 1) << 4 | disp >> 8, disp & 0xFF])
                elif m <= 0x110:
                    k = m - 0x11; out += bytes([k >> 4, (k & 0xF) << 4 | disp >> 8, disp & 0xFF])
                else:
                    k = m - 0x111; out += bytes([0x10 | k >> 12, k >> 4 & 0xFF, (k & 0xF) << 4 | disp >> 8, disp & 0xFF])
            else:
                out.append(d[p])
            p += m
        out[flag_at] = flags
    while len(out) % 4: out.append(0)
    return bytes(out)


def darc_list(d):
    """[(path, data)] of a DARC archive."""
    assert d[:4] == b'darc', 'not DARC'
    table_off, table_len, data_off = struct.unpack_from('<III', d, 0x10)     # after magic, byte order, header size, version, file size
    n = struct.unpack_from('<III', d, table_off)[2]                          # the root entry's end is the number of entries
    names_off = table_off + n * 12
    def name(o):
        s = bytearray(); p = names_off + o
        while d[p] or d[p + 1]: s += d[p:p + 2]; p += 2
        return s.decode('utf-16-le')
    out, stack = [], []
    for i in range(n):
        w0, a, b = struct.unpack_from('<III', d, table_off + i * 12)
        is_dir = w0 >> 24; nm = name(w0 & 0xFFFFFF)
        while stack and stack[-1][1] <= i: stack.pop()
        path = '/'.join(s[0] for s in stack if s[0]) + ('/' if stack and any(s[0] for s in stack) else '') + nm
        if is_dir: stack.append((nm, b))
        else: out.append((path, d[a:a + b]))
    return out


CLIM_FORMATS = {0: 'L8', 1: 'A8', 2: 'LA4', 3: 'LA8', 4: 'HILO8', 5: 'RGB565', 6: 'RGB8', 7: 'RGBA5551', 8: 'RGBA4', 9: 'RGBA8',
                10: 'ETC1', 11: 'ETC1A4', 12: 'L4', 13: 'A4'}


def clim_info(d):
    """(width, height, format) from a BCLIM's footer."""
    assert d[-0x28:-0x24] == b'CLIM', 'not BCLIM'
    w, h, fmt = struct.unpack_from('<HHI', d, len(d) - 0x14 + 8)
    return w, h, fmt


def morton(x, y):
    m = 0
    for i in range(3): m |= ((x >> i) & 1) << (2 * i) | ((y >> i) & 1) << (2 * i + 1)
    return m


def tiled_index(x, y, w):
    return ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + morton(x & 7, y & 7)


ETC_TABLES = [(2, 8), (5, 17), (9, 29), (13, 42), (18, 60), (24, 80), (33, 106), (47, 183)]


def etc1_block(v):
    """16 RGB pixels (index = x * 4 + y) of one ETC1 block given as a 64-bit number."""
    hi, lo = v >> 32, v & 0xFFFFFFFF
    diff, flip = hi >> 1 & 1, hi & 1
    t1, t2 = hi >> 5 & 7, hi >> 2 & 7
    if diff:
        def ch(s):
            a = hi >> s & 0x1F; d = (hi >> (s - 3)) & 7
            if d >= 4: d -= 8
            b = (a + d) & 0x1F
            return (a << 3 | a >> 2), (b << 3 | b >> 2)
        (r1, r2), (g1, g2), (b1, b2) = ch(27), ch(19), ch(11)
    else:
        def ch(s):
            a = hi >> (s + 4) & 0xF; b = hi >> s & 0xF
            return a * 17, b * 17
        (r1, r2), (g1, g2), (b1, b2) = ch(24), ch(16), ch(8)
    out = []
    for x in range(4):
        for y in range(4):
            i = x * 4 + y
            second = (y >= 2) if flip else (x >= 2)
            base = (r2, g2, b2) if second else (r1, g1, b1)
            tab = ETC_TABLES[t2 if second else t1]
            msb, lsb = lo >> (i + 16) & 1, lo >> i & 1
            m = (tab[0], tab[1], -tab[0], -tab[1])[msb * 2 + lsb]
            out.append(tuple(max(0, min(255, c + m)) for c in base))
    return out


def etc1_image(d, w, h, alpha):
    from PIL import Image
    img = Image.new('RGBA', (w, h)); px = img.load()
    step = 16 if alpha else 8; p = 0
    for ty in range(0, h, 8):
        for tx in range(0, w, 8):
            for bx, by in ((0, 0), (4, 0), (0, 4), (4, 4)):
                a = struct.unpack_from('<Q', d, p)[0] if alpha else None
                v = struct.unpack_from('<Q', d, p + (8 if alpha else 0))[0]; p += step
                pix = etc1_block(v)
                for x in range(4):
                    for y in range(4):
                        X, Y = tx + bx + x, ty + by + y
                        if X < w and Y < h:
                            al = ((a >> (4 * (x * 4 + y))) & 15) * 17 if alpha else 255
                            px[X, Y] = pix[x * 4 + y] + (al,)
    return img


def clim_to_rgba(d):
    """A PIL image of a BCLIM (the formats the logo uses)."""
    from PIL import Image
    w, h, fmt = clim_info(d)
    if fmt in (10, 11):
        pw = 8
        while pw < w: pw *= 2
        ph = 8
        while ph < h: ph *= 2
        return etc1_image(d, pw, ph, fmt == 11)
    img = Image.new('RGBA', (w, h))
    px = img.load()
    for y in range(h):
        for x in range(w):
            i = tiled_index(x, y, w)
            if fmt == 0: v = d[i]; c = (v, v, v, 255)
            elif fmt == 1: c = (255, 255, 255, d[i])
            elif fmt == 2: v = d[i]; c = ((v >> 4) * 17,) * 3 + ((v & 15) * 17,)
            elif fmt == 3: c = (d[2 * i + 1],) * 3 + (d[2 * i],)
            elif fmt == 5: v = struct.unpack_from('<H', d, 2 * i)[0]; c = ((v >> 11) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31, 255)
            elif fmt == 7: v = struct.unpack_from('<H', d, 2 * i)[0]; c = ((v >> 11) * 255 // 31, (v >> 6 & 31) * 255 // 31, (v >> 1 & 31) * 255 // 31, 255 * (v & 1))
            elif fmt == 8: v = struct.unpack_from('<H', d, 2 * i)[0]; c = ((v >> 12) * 17, (v >> 8 & 15) * 17, (v >> 4 & 15) * 17, (v & 15) * 17)
            elif fmt == 9: c = (d[4 * i + 3], d[4 * i + 2], d[4 * i + 1], d[4 * i])
            elif fmt == 12: v = d[i // 2] >> (4 * (i & 1)) & 15; c = (v * 17,) * 3 + (255,)
            elif fmt == 13: v = d[i // 2] >> (4 * (i & 1)) & 15; c = (255, 255, 255, v * 17)
            else: raise SystemExit(f'format {CLIM_FORMATS.get(fmt, fmt)} not handled')
            px[x, y] = c
    return img


def main():
    if len(sys.argv) >= 3 and sys.argv[1] == 'show':
        d = open(sys.argv[2], 'rb').read(); w, h, fmt = clim_info(d)
        print(f'{w}x{h} {CLIM_FORMATS.get(fmt, fmt)}')
        if len(sys.argv) > 3: clim_to_rgba(d).save(sys.argv[3])
        return
    if len(sys.argv) < 4 or sys.argv[1] != 'extract': sys.exit(__doc__)
    cia = open(sys.argv[2], 'rb').read()
    logo = ncch_logo(cia_ncch(cia)).rstrip(b'\0')
    out = sys.argv[3]; os.makedirs(out, exist_ok=True)
    open(os.path.join(out, 'logo.bcma.lz'), 'wb').write(logo)
    darc = lz11_decompress(logo)
    open(os.path.join(out, 'logo.darc'), 'wb').write(darc)
    for path, data in darc_list(darc):
        p = os.path.join(out, 'files', path); os.makedirs(os.path.dirname(p), exist_ok=True)
        open(p, 'wb').write(data)
        print(f'{path:40s} {len(data):7d} bytes  {data[:4]}')


if __name__ == '__main__':
    main()
