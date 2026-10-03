#!/usr/bin/env python3
"""The app's HOME Menu banner: the 3D scene shown on the top screen while the app's icon is selected.
Built from nothing with tools/banner_scene.py and tools/banner_shapes.py; see docs/banner.md for how the
HOME Menu treats a banner.

  make_banner.py            writes app/build/banner/design.cgfx and prints what is in it
  make_banner.py install    the same, and copies it to app/meta/banner.cgfx (what the build packs into the CIA)

What it shows: the TP mark alone, in the middle of the screen, as a soft, inflated solid: white faces, the
icon's lavender round the edge. It is held facing the viewer (it turns against the HOME Menu's turning of
the scene) and rocks and floats gently over its shadow. No wordmark: the HOME Menu already prints the
app's name on the bottom screen. Round it, quietly: a soft glow behind the mark, small sparkles that
twinkle at different distances, and faint dots drifting up. These are not held still, so the HOME Menu's
turning carries them slowly round the mark, in front of it and behind.
When the icon is selected the letters pop up from nothing, T then P, as on the start-up screen. The
animation is 30 s long (three turns of the HOME Menu); at its end the letters pop away again so the loop
joins, and they pop back in.
"""
import math, os, re, shutil, sys
from PIL import Image
from shapely.geometry import Polygon
from shapely import affinity

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from banner_scene import *
import banner_shapes as S

OUT = os.path.join(ROOT, 'app', 'build', 'banner')
ICON_SVG = os.path.join(ROOT, 'app', 'meta', 'brand', 'tp-icon-dsi-blue.svg')
FRAMES = 1800                                   # three of the HOME Menu's 600-frame turns
LOOP = 600                                      # the idle motions repeat every turn
TURN = 2 * math.pi
POP_T, POP_P, POP_OUT = 0, 10, FRAMES - 52      # when the letters pop in (at once: 16 frames later reads as late), and when they pop away

# colours (the icon's): each a ramp from its lit to its shaded end, picked per vertex
RAMPS = [
    ('face', '#ffffff', '#eef0fb'),
    ('belt', '#c9cff4', '#b3bbec'),
]
RAMP_W, RAMP_H = 32, 8
SHADOW = '#03416a'

def rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (1, 3, 5))


def ramp_texture():
    im = Image.new('RGBA', (RAMP_W, RAMP_H * 8), (0, 0, 0, 255))
    for r, (_, a, b) in enumerate(RAMPS):
        a, b = rgb(a), rgb(b)
        for x in range(RAMP_W):
            t = x / (RAMP_W - 1)
            c = tuple(round(a[k] + (b[k] - a[k]) * t) for k in range(3))
            for y in range(RAMP_H): im.putpixel((x, r * RAMP_H + y), c + (255,))
    return im


def ramp_uv(name, t=0.0):
    r = [n for n, _, _ in RAMPS].index(name)
    return ((0.5 + max(0.0, min(1.0, t)) * (RAMP_W - 1)) / RAMP_W, 1 - (r * RAMP_H + RAMP_H / 2) / (RAMP_H * 8))


def spark_texture(size=32):
    """A four-pointed star with a soft centre."""
    im = Image.new('RGBA', (size, size))
    for y in range(size):
        for x in range(size):
            u, v = (x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1
            d = math.hypot(u, v)
            star = max(0.0, 1 - (abs(u) + abs(v)) * 1.05) ** 1.6                      # the points
            glow = max(0.0, 1 - d * 2.2) ** 2 * 0.9                                     # the centre
            im.putpixel((x, y), (255, 255, 255, int(255 * min(1.0, star + glow))))
    return im


def blob_texture(size=32):
    im = Image.new('RGBA', (size, size))
    for y in range(size):
        for x in range(size):
            d = math.hypot((x + 0.5) / size * 2 - 1, (y + 0.5) / size * 2 - 1)
            a = max(0.0, 1 - d)
            im.putpixel((x, y), (0, 0, 0, int(255 * (a * a * (3 - 2 * a)))))
    return im


def letters(width):
    """The icon's T and P as outlines in scene units, the pair centred on the origin and `width` wide."""
    svg = open(ICON_SVG).read()
    raw = {}
    for m in re.finditer(r'<path id="([TP])"[^>]*d="([^"]+)"', svg):
        rings = [Polygon([(x * 0.05, y * 0.05) for x, y in r]) for r in S.svg_path_polygons(m.group(2), 8)]
        rings.sort(key=lambda p: -p.area)
        raw[m.group(1)] = Polygon(rings[0].exterior, [q.exterior for q in rings[1:]])
    x0 = min(p.bounds[0] for p in raw.values()); x1 = max(p.bounds[2] for p in raw.values())
    y0 = min(p.bounds[1] for p in raw.values()); y1 = max(p.bounds[3] for p in raw.values())
    k = width / (x1 - x0)
    out = {}
    for name, p in raw.items():
        p = affinity.translate(p, -(x0 + x1) / 2, -(y0 + y1) / 2)
        out[name] = S.simplify(affinity.scale(p, k, k, origin=(0, 0)), 0.01)
    return out, (y1 - y0) * k


def quad_centred(x, y, z, w, h):
    return S.quad_xy(x - w / 2, y - h / 2, x + w / 2, y + h / 2, z)


def pop(at, out, soft=False):
    """Scale keys: nothing until `at`, then up with a bounce (or, soft, a plain rise) to full size, held, and
    away again quickly from `out` so the loop can start from nothing."""
    rise = [(at, 0.0, 0.0), (at + 9, 1.0 if soft else 1.16, 0.0), (at + 15, 1.0 if soft else 0.95, 0.0), (at + 21, 1.0, 0.0)]
    away = [(out, 1.0, 0.0), (out + 7, 1.0 if soft else 1.1, 0.0), (out + 17, 0.0, 0.0)]
    return ([(0.0, 0.0, 0.0)] if at > 0 else []) + rise + away + [(float(FRAMES), 0.0, 0.0)]


def twinkle(at, width):
    """Scale keys that sit at 0 and rise to 1 for `width` frames around each frame in `at`, looping."""
    keys = []
    for f in sorted(at):
        keys += [(f - width, 0.0, 0.0), (f, 1.0, 0.0), (f + width, 0.0, 0.0)]
    keys = [k for k in keys if 0 <= k[0] <= FRAMES]
    if not keys or keys[0][0] > 0: keys.insert(0, (0.0, 0.0, 0.0))
    if keys[-1][0] < FRAMES: keys.append((float(FRAMES), 0.0, 0.0))
    return keys


def saw(fn, per, phase):
    """Keys for fn over the loop, where fn repeats every `per` frames and jumps back at the end of each
    repeat: sampled along each repeat, with a key just before and just after every jump so it stays sharp."""
    jumps = [j for j in (((1 - phase) % 1.0) * per + c * per for c in range(int(round(FRAMES / per)) + 1)) if 0 < j < FRAMES]
    edges = [0.0] + jumps + [float(FRAMES)]
    keys = []
    for a, b in zip(edges, edges[1:]):
        lo, hi = (a + 0.5 if a else 0.0), (b - 0.5 if b < FRAMES else float(FRAMES))
        n = max(2, int((hi - lo) / per * 12))
        for i in range(n + 1):
            f = lo + (hi - lo) * i / n
            keys.append((f, fn(f), (fn(min(f + 0.1, hi)) - fn(max(f - 0.1, lo))) / 0.2))
    return keys


def wave(fn, count=None):
    """Keys for a smooth function of the frame (0..FRAMES), slopes taken from the function: 24 a turn."""
    count = count or 24 * FRAMES // LOOP
    out = []
    for i in range(count + 1):
        f = FRAMES * i / count
        out.append((f, fn(f), (fn(f + 0.05) - fn(f - 0.05)) / 0.1))
    return out


def build():
    b = Banner(FRAMES)
    b.texture('COMMON1', ramp_texture(), 'RGB565')
    b.texture('COMMON2', blob_texture(), 'A8')

    # light from above, a little to the left and in front; what it does not reach falls to the icon's lavender
    b.light('Light1', (0.3, -0.78, -0.55), ambient=(0.80, 0.83, 0.97), diffuse=(0.22, 0.19, 0.05), specular0=(1, 1, 1), specular1=(0, 0, 0))
    ramps = ('COMMON1', {'wrap': (CLAMP, CLAMP)})
    b.material('soft', [ramps], [stage(0, (MODULATE, (LIGHT, TEX0)), (REPLACE, (VERTEX,)))], lit=True)
    shadow_c = tuple(c / 255 for c in rgb(SHADOW)) + (1.0,)
    b.material('shadow', [('COMMON2', {'wrap': (CLAMP, CLAMP)})],
               [stage(0, (REPLACE, (CONST,)), (MODULATE, (VERTEX, TEX0)), constant=0)], blend=True, constants=[shadow_c])

    root = b.bone('root')
    # everything under `hold` keeps still while the HOME Menu turns the scene once per 600 frames
    hold = b.bone('hold', root)
    turns = FRAMES / LOOP
    b.animate(hold, 'ry', [(0, 0.0, TURN / LOOP), (FRAMES, turns * TURN, TURN / LOOP)])
    floor = b.bone('floor', hold)

    # ---- the mark
    MARK_W, MARK_Y = 15.0, 1.0                  # y = 1 is the middle of the screen
    shapes, mark_h = letters(MARK_W)
    mark = b.bone('mark', hold, translation=(0, MARK_Y, 0))
    for name, z in (('P', -0.45), ('T', 0.45)):
        bone = b.bone('letter' + name, mark, translation=(0, 0, z))
        for ch in ('sx', 'sy', 'sz'): b.animate(bone, ch, pop(POP_T if name == 'T' else POP_P, POP_OUT + (0 if name == 'P' else 12)))
        y_lo, y_hi = shapes[name].bounds[1], shapes[name].bounds[3]
        face_uv = lambda x, y, zz: ramp_uv('face', (y_hi - y) / (y_hi - y_lo) * 0.8)
        front, band, back = S.puffy(shapes[name], height=1.15, belt=0.9, back_height=0.8, uv_face=face_uv, uv_side=ramp_uv('belt', 0.3),
                                    spacing=0.34, roundness=2.3)
        for part, mesh in (('front', front), ('band', band), ('back', back)):
            b.mesh(bone, 'soft', mesh.pos, mesh.tri, uvs=mesh.uv, normals=mesh.nrm)
    sway = lambda f: 0.34 * math.sin(TURN * f / LOOP)
    b.animate(mark, 'ry', wave(sway))
    b.animate(mark, 'rx', wave(lambda f: 0.10 * math.sin(TURN * 2 * f / LOOP + 0.6)))
    b.animate(mark, 'ty', wave(lambda f: MARK_Y + 0.35 * math.sin(TURN * 2 * f / LOOP)))

    # ---- its shadow on the floor
    FLOOR_Y = -7.9
    m = S.quad_floor(0, 0, 8.5, 3.6, FLOOR_Y)
    b.mesh(floor, 'shadow', m.pos, m.tri, uvs=m.uv, colors=[(1, 1, 1, 0.42)] * 4, priority=10)
    for ch in ('sx', 'sz'): b.animate(floor, ch, pop(POP_T, POP_OUT, soft=True))

    # ---- a soft glow behind the mark
    b.texture('COMMON3', spark_texture(), 'A8')
    glow_c = (1.0, 1.0, 1.0, 1.0)
    b.material('glow', [('COMMON2', {'wrap': (CLAMP, CLAMP)})],
               [stage(0, (REPLACE, (CONST,)), (MODULATE, (VERTEX, TEX0)), constant=0)], blend=True, constants=[glow_c])
    halo = b.bone('halo', root, billboard=Banner.Y_AXIAL)
    m = quad_centred(0, MARK_Y + 0.4, -4.0, 13.0, 9.5)
    b.mesh(halo, 'glow', m.pos, m.tri, uvs=m.uv, colors=[(1, 1, 1, 0.16)] * 4, priority=5)
    for ch in ('sx', 'sy'): b.animate(halo, ch, pop(POP_T, POP_OUT, soft=True))

    # ---- sparkles: each on its own billboard bone, twinkling (scaled up and down) at its own moments
    spark_c = (0.95, 0.97, 1.0, 1.0)
    b.material('spark', [('COMMON3', {'wrap': (CLAMP, CLAMP)})],
               [stage(0, (REPLACE, (CONST,)), (MODULATE, (VERTEX, TEX0)), constant=0)], blend=True, constants=[spark_c])
    sparks = [(-8.2, 6.4, 3.0, 1.5, (40, 330)), (8.6, 7.0, -4.0, 1.3, (160, 470)), (-9.8, -1.5, -6.0, 1.1, (250,)),
              (9.4, -2.6, 6.0, 1.4, (90, 400)), (-5.0, 8.4, -8.0, 0.9, (300, 560)), (6.2, -6.2, 7.5, 1.0, (10, 210, 520)),
              (0.8, 9.6, 6.5, 1.2, (130, 440)), (-9.0, 4.0, -9.0, 0.8, (370,))]
    # the same twinkles every turn, and a few with the letters' arrival
    sparks = [(x, y, z, size, tuple(a + k * LOOP for k in range(FRAMES // LOOP) for a in at) + ((POP_T + 10, POP_P + 10) if i % 3 == 0 else ()))
              for i, (x, y, z, size, at) in enumerate(sparks)]
    for i, (x, y, z, size, at) in enumerate(sparks):
        bone = b.bone(f'spark{i}', root, translation=(x, y, z), billboard=Banner.Y_AXIAL)
        m = quad_centred(0, 0, 0, size, size)
        b.mesh(bone, 'spark', m.pos, m.tri, uvs=m.uv, colors=[(1, 1, 1, 0.85)] * 4, priority=20)
        keys = twinkle(at, 22)
        b.animate(bone, 'sx', keys); b.animate(bone, 'sy', keys)
        b.animate(bone, 'rz', [(0, 0.0, 0.5 * TURN / LOOP), (FRAMES, 0.5 * TURN * turns, 0.5 * TURN / LOOP)] if i % 2 else
                  [(0, 0.0, -0.5 * TURN / LOOP), (FRAMES, -0.5 * TURN * turns, -0.5 * TURN / LOOP)])

    # ---- faint dots drifting up, fading in at the bottom and out at the top
    dot_c = (0.86, 0.90, 1.0, 1.0)
    b.material('dot', [('COMMON2', {'wrap': (CLAMP, CLAMP)})],
               [stage(0, (REPLACE, (CONST,)), (MODULATE, (VERTEX, TEX0)), constant=0)], blend=True, constants=[dot_c])
    dots = [(-7.0, -8.0, 5.0, 0.9, 1, 0.0), (7.8, -7.0, -3.0, 0.7, 1, 0.45), (-3.5, -9.0, -7.0, 0.6, 2, 0.2),
            (10.5, -8.5, 4.5, 0.55, 2, 0.7), (3.0, -9.5, 8.0, 0.8, 1, 0.8), (-10.8, -6.0, -5.0, 0.65, 1, 0.3)]
    dots = [(x, y0, z, size, cycles * (FRAMES // LOOP), phase) for x, y0, z, size, cycles, phase in dots]   # cycles are per turn
    for i, (x, y0, z, size, cycles, phase) in enumerate(dots):
        bone = b.bone(f'dot{i}', root, translation=(x, y0, z), billboard=Banner.Y_AXIAL)
        m = quad_centred(0, 0, 0, size, size)
        b.mesh(bone, 'dot', m.pos, m.tri, uvs=m.uv, colors=[(1, 1, 1, 0.34)] * 4, priority=20)
        rise = 15.0
        per = FRAMES / cycles
        def ty(f, y0=y0, per=per, phase=phase): return y0 + rise * (((f / per) + phase) % 1.0)
        def grow(f, per=per, phase=phase):
            t = ((f / per) + phase) % 1.0
            return min(1.0, t / 0.18, (1 - t) / 0.22)
        b.animate(bone, 'ty', saw(ty, per, phase))
        b.animate(bone, 'sx', saw(grow, per, phase)); b.animate(bone, 'sy', saw(grow, per, phase))

    return b


def main():
    os.makedirs(OUT, exist_ok=True)
    b = build()
    data = b.build()
    path = os.path.join(OUT, 'design.cgfx')
    open(path, 'wb').write(data)
    tris = sum(len(st.data) // (1 if st.format == 0x1401 else 2) for sh in b.model.shapes for ps in sh.primSets for pr in ps.prims for st in pr.streams) // 3
    print(f'{path}: {len(data)} bytes of {0x80000} allowed, {len(b.model.meshes)} meshes, {tris} triangles, {len(b.bones)} bones')
    if len(sys.argv) > 1 and sys.argv[1] == 'install':
        shutil.copyfile(path, os.path.join(ROOT, 'app', 'meta', 'banner.cgfx'))
        print('copied to app/meta/banner.cgfx')


if __name__ == '__main__':
    main()
