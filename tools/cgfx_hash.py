#!/usr/bin/env python3
"""The 13 hashes a CGFX material carries. The console compares them between materials to decide which
settings it can skip sending again, so two materials may only share a hash if they share the settings.

Each hash is the MD5 of a description of one group of settings, folded to 32 bits (0 becomes 1). The
descriptions follow the public-domain SPICA library; `cgfx_hash.py FILE.cgfx...` reports, for every
material in working banners, which of them reproduce the stored values.
Two hashes (texture mappers, lighting tables) are left 0 in files: the console fills them in.
"""
import hashlib, struct, sys

NAMES = ['flags', 'shader parameters', 'texture coordinates', 'texture samplers', 'texture mappers', 'colours',
         'rasterisation', 'lighting', 'lighting tables', 'lighting table inputs', 'combiner stages', 'alpha test',
         'fragment operations']
SRC = {0: 0x8577, 1: 0x6210, 2: 0x6211, 3: 0x84C0, 4: 0x84C1, 5: 0x84C2, 6: 0x84C3, 13: 0x8579, 14: 0x8576, 15: 0x8578}
COLOR_OP = {0: 0x300, 1: 0x301, 2: 0x302, 3: 0x303, 4: 0x8580, 5: 0x8583, 8: 0x8581, 9: 0x8584, 12: 0x8582, 13: 0x8585}
ALPHA_OP = {0: 0x302, 1: 0x303, 2: 0x8580, 3: 0x8583, 4: 0x8581, 5: 0x8584, 6: 0x8582, 7: 0x8585}
TEST = {0: 0, 1: 1, 2: 4, 3: 7, 4: 2, 5: 3, 6: 6, 7: 5}     # the GPU's comparison numbers -> the format's own
BLEND = {0: 0, 1: 1, 2: 0x300, 3: 0x301, 4: 0x306, 5: 0x307, 6: 0x302, 7: 0x303, 8: 0x304, 9: 0x305, 10: 0x8001,
         11: 0x8002, 12: 0x8003, 13: 0x8004, 14: 0x308}
EQUATION = {0: 0x8006, 1: 0x800A, 2: 0x800B, 3: 0x8007, 4: 0x8008}


def fold(data):
    h = 0
    for i, x in enumerate(hashlib.md5(data).digest()): h ^= x << ((i & 3) * 8)
    return h or 1


def descriptions(m, alpha_ref=None, with_buffer_color=False):
    """The bytes each hash is made from, for material m (None where the file holds 0).
    alpha_ref: the alpha test's reference as the number it was authored as (0..1); the file only keeps it
    as a byte, so without it the byte / 255 is used.
    with_buffer_color: 21 of the 96 materials checked (older banners) also hash the combiner buffer colour."""
    f = lambda *v: struct.pack(f'<{len(v)}f', *v)
    u = lambda *v: struct.pack(f'<{len(v)}I', *[x & 0xFFFFFFFF for x in v])
    out = [None] * 13
    out[0] = u(m.flags | 0x20)
    out[1] = b''
    d = u(m.texCoordConfig)
    for i, c in enumerate(m.texCoords):
        used = i < m.usedTexCoords
        d += u(c.source, c.mapping, c.refCamera, c.matrixMode) + (f(*c.scale) if used else b'') + f(c.rotation)
        d += (f(*c.translation) if used else b'') + b'\0' + f(*c.matrix) + u(m.texCoordConfig)
    out[2] = d
    d = u(m.texCoordConfig)
    for mp in (m.mapper0, m.mapper1, m.mapper2):
        if mp is None or mp.sampler is None: continue
        filt, lod = mp.cmd[5], mp.cmd[7]
        wrap = [2, 3, 0, 1]
        bias = ((lod & 0x1FFF) - (0x2000 if lod & 0x1000 else 0)) / 256.0
        d += f(*mp.sampler.border) + u(wrap[filt >> 12 & 3], wrap[filt >> 8 & 3]) + f(float(lod >> 24 & 0xF), bias)
        d += u(mp.sampler.minFilter, filt >> 1 & 1)
    out[3] = d
    out[5] = f(*m.emission[:3], *m.ambient[:3], m.ambient[3], *m.diffuse, *m.specular0[:3], *m.specular1[:3], *m.constant)
    out[6] = u(m.rasterFlags & 1, m.rasterCmd[0], m.rasterCmd[1]) + f(m.polygonOffset)
    s = m.fragShader
    out[7] = u(s.lightFlags, s.layerConfig, s.fresnel, s.bumpTexture, s.bumpMode) + bytes([s.bumpRenorm & 1])
    d = b''
    if s.lutTable is not None:
        for name in ('reflectR', 'reflectG', 'reflectB', 'dist0', 'dist1', 'fresnel'):
            t = getattr(s.lutTable, name)
            if t is not None: d += u(t.input, t.scale)
    out[9] = d
    d = f(*s.bufferColor) if with_buffer_color else b''
    for st in s.stages:
        src, op = st.source, st.operand
        d += u(st.constant, *[SRC.get(src >> k & 15, 0) for k in (0, 4, 8)], *[COLOR_OP.get(op >> k & 15, 0) for k in (0, 4, 8)],
               *[SRC.get(src >> k & 15, 0) for k in (16, 20, 24)], *[ALPHA_OP.get(op >> k & 7, 0) for k in (12, 16, 20)])
        d += u(st.source, st.header, st.operand, st.combine, st.color, st.scale)
    out[10] = d
    a = s.alphaTest[0]
    out[11] = bytes([a & 1]) + u(TEST[a >> 4 & 7]) + f((a >> 8 & 0xFF) / 255.0 if alpha_ref is None else alpha_ref)
    depth, blend = m.depthCmd[0], m.blendCmd[2]
    d = u(m.depthFlags, TEST[depth >> 4 & 7], 0, m.blendMode) + f(*m.blendColor) + u(1)      # 1: logical operation "copy"
    fn = lambda k: BLEND[blend >> k & 15]
    eq = lambda k: EQUATION[blend >> k & 7]
    if m.blendMode == 1: d += u(fn(16), fn(20), eq(0), 1, 0, 0x8006)
    elif m.blendMode == 2: d += u(fn(16), fn(20), eq(0), fn(24), fn(28), eq(8))      # no banner checked uses this mode
    else: d += u(0x302, 0x303, 0x8006, 1, 0, 0x8006)
    out[12] = d + u(*m.stencilCmd)
    return out


def hashes(m, alpha_ref=None, with_buffer_color=False):
    return tuple(0 if d is None else fold(d) for d in descriptions(m, alpha_ref, with_buffer_color))


def main():
    import cgfx
    total, good = 0, [0] * 13
    for path in sys.argv[1:]:
        scene = cgfx.read(open(path, 'rb').read())
        for _, model in scene.models:
            for name, m in model.materials:
                total += 1
                ref = m.fragShader.alphaTest[0] >> 8 & 0xFF
                # the authored reference is a round number that became this byte
                refs = [None] + [k / 1000.0 for k in range(1001) if int(k / 1000.0 * 255) == ref or round(k / 1000.0 * 255) == ref]
                tries = [hashes(m, r, wb) for r in refs for wb in (False, True)]
                for i in range(13): good[i] += any(t[i] == m.hashes[i] for t in tries)
    print(f'{total} materials')
    for i in range(13): print(f'  {NAMES[i]:24s} {good[i]} reproduced')


if __name__ == '__main__':
    main()
