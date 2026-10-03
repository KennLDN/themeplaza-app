#!/usr/bin/env python3
"""A small QR code generator for the tests (byte mode, version 3, error correction level L, mask 0).
Enough for a Theme Plaza download link. matrix(text) returns 29 rows of 29 values, 1 = dark.

  qrgen.py TEXT OUT.png [SCALE]     writes the code with a four-module quiet zone
"""
import sys

SIZE = 29                 # version 3
DATA_WORDS, EC_WORDS = 55, 15

EXP, LOG = [0] * 512, [0] * 256
x = 1
for i in range(255):
    EXP[i] = x; LOG[x] = i
    x <<= 1
    if x & 0x100: x ^= 0x11D
for i in range(255, 512): EXP[i] = EXP[i - 255]


def rs(data, n):
    gen = [1]
    for i in range(n):
        nxt = [0] * (len(gen) + 1)
        for j, g in enumerate(gen):
            nxt[j] ^= g
            if g: nxt[j + 1] ^= EXP[LOG[g] + i]
        gen = nxt
    rem = [0] * n
    for d in data:
        f = d ^ rem[0]
        rem = rem[1:] + [0]
        if f:
            for j in range(n):
                if gen[j + 1]: rem[j] ^= EXP[LOG[gen[j + 1]] + LOG[f]]
    return rem


def matrix(text):
    raw = text.encode()
    assert len(raw) <= DATA_WORDS - 2, 'too long for version 3-L'
    bits = '0100' + format(len(raw), '08b') + ''.join(format(b, '08b') for b in raw)
    bits += '0' * min(4, DATA_WORDS * 8 - len(bits))
    bits += '0' * (-len(bits) % 8)
    words = [int(bits[i:i + 8], 2) for i in range(0, len(bits), 8)]
    pad = [0xEC, 0x11]
    while len(words) < DATA_WORDS: words.append(pad[(len(words) - len(bits) // 8) % 2])
    words += rs(words, EC_WORDS)
    stream = ''.join(format(w, '08b') for w in words) + '0' * 7      # 7 remainder bits in version 3

    m = [[None] * SIZE for _ in range(SIZE)]

    def finder(r0, c0):
        for r in range(-1, 8):
            for c in range(-1, 8):
                rr, cc = r0 + r, c0 + c
                if not (0 <= rr < SIZE and 0 <= cc < SIZE): continue
                inside = 0 <= r <= 6 and 0 <= c <= 6
                m[rr][cc] = 1 if inside and (r in (0, 6) or c in (0, 6) or (2 <= r <= 4 and 2 <= c <= 4)) else 0
    finder(0, 0); finder(0, SIZE - 7); finder(SIZE - 7, 0)
    for r in range(-2, 3):            # alignment pattern centred on (22, 22)
        for c in range(-2, 3):
            m[22 + r][22 + c] = 1 if max(abs(r), abs(c)) != 1 else 0
    for i in range(8, SIZE - 8):      # timing
        if m[6][i] is None: m[6][i] = 1 - i % 2
        if m[i][6] is None: m[i][6] = 1 - i % 2
    m[SIZE - 8][8] = 1                # the dark module
    # format information: level L (01), mask 0 (000), BCH(15,5), xor 101010000010010
    fmt = 0b01000
    rem = fmt << 10
    for i in range(14, 9, -1):
        if rem & (1 << i): rem ^= 0x537 << (i - 10)
    fbits = ((fmt << 10) | rem) ^ 0x5412
    f = [(fbits >> i) & 1 for i in range(15)]         # f[0] is the least significant bit
    # first copy, around the top-left finder
    pos1 = [(8, 0), (8, 1), (8, 2), (8, 3), (8, 4), (8, 5), (8, 7), (8, 8), (7, 8), (5, 8), (4, 8), (3, 8), (2, 8), (1, 8), (0, 8)]
    for i, (r, c) in enumerate(pos1): m[r][c] = f[14 - i]
    # second copy: down the left of the top-right finder is not it; it runs along the bottom-left and top-right
    pos2 = [(SIZE - 1 - i, 8) for i in range(7)] + [(8, SIZE - 8 + i) for i in range(8)]
    for i, (r, c) in enumerate(pos2): m[r][c] = f[14 - i]

    # data, in the zigzag from the bottom right, with mask 0 ((row + column) even)
    k = 0
    c = SIZE - 1
    up = True
    while c > 0:
        if c == 6: c -= 1
        rows = range(SIZE - 1, -1, -1) if up else range(SIZE)
        for r in rows:
            for cc in (c, c - 1):
                if m[r][cc] is None:
                    bit = int(stream[k]) if k < len(stream) else 0
                    k += 1
                    m[r][cc] = bit ^ (1 if (r + cc) % 2 == 0 else 0)
        up = not up
        c -= 2
    return m


def image(text, scale=8, quiet=4):
    from PIL import Image
    m = matrix(text)
    n = SIZE + 2 * quiet
    im = Image.new('L', (n, n), 255)
    px = im.load()
    for r in range(SIZE):
        for c in range(SIZE):
            if m[r][c]: px[c + quiet, r + quiet] = 0
    return im.resize((n * scale, n * scale), Image.NEAREST)


if __name__ == '__main__':
    image(sys.argv[1], int(sys.argv[3]) if len(sys.argv) > 3 else 8).save(sys.argv[2])
