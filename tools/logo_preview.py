#!/usr/bin/env python3
"""Plays a start-up logo on the PC: reads the built file (the same bytes the console gets), and renders
both screens frame by frame the way the HOME Menu runs it: animation A once, B looped, C once.

  logo_preview.py LOGO.bcma.lz OUT.png [LOOPS] [EVERY]
        a sheet of frames (every EVERY-th frame, default 6; B is looped LOOPS times, default 1)
  logo_preview.py LOGO.bcma.lz OUT.gif [LOOPS]
        the whole run as an animated GIF at 30 fps (every second frame)

It handles what tools/make_logo.py produces: null panes and picture panes with one 4-bit-alpha texture,
tinted by the material's first colour and the four corner colours; position, rotation, scale, size, alpha
and visibility; and animation of those. Anything else in a file is reported and skipped.
"""
import math, os, sys
import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import logo as L, logo_build as B, logo_layout as LL

WARNED = set()


def warn(msg):
    if msg not in WARNED: WARNED.add(msg); print('preview: not handled:', msg)


def texture_alpha(clim):
    w, h, fmt = L.clim_info(clim)
    if fmt != 13: warn(f'texture format {L.CLIM_FORMATS.get(fmt, fmt)}'); return np.ones((h, w))
    a = np.zeros((h, w))
    for y in range(h):
        for x in range(w):
            i = L.tiled_index(x, y, w)
            a[y, x] = (clim[i // 2] >> (4 * (i & 1)) & 15) / 15
    return a


def hermite(keys, f):
    if f <= keys[0][0]: return keys[0][1]
    if f >= keys[-1][0]: return keys[-1][1]
    for k0, k1 in zip(keys, keys[1:]):
        if k0[0] <= f <= k1[0] and k1[0] > k0[0]:
            d = k1[0] - k0[0]; t = (f - k0[0]) / d
            h00, h10, h01, h11 = 2 * t**3 - 3 * t**2 + 1, t**3 - 2 * t**2 + t, -2 * t**3 + 3 * t**2, t**3 - t**2
            return h00 * k0[1] + h10 * d * k0[2] + h01 * k1[1] + h11 * d * k1[2]
    return keys[-1][1]


def step(keys, f):
    v = keys[0][1]
    for k in keys:
        if k[0] <= f: v = k[1]
    return v


class Screen:
    def __init__(self, layout, anims, textures):
        self.lay, self.anims = layout, anims
        self.tex = [textures[n] for n in layout['textures']]
        self.w, self.h = int(layout['canvas'][0]), int(layout['canvas'][1])
        self.panes = {}
        def walk(p):
            self.panes[p['name']] = p
            p['_s'] = {'t': list(p['translate']), 'r': list(p['rotate']), 's': list(p['scale']), 'size': list(p['size']), 'alpha': p['alpha'],
                       'visible': bool(p['flags'] & 1), 'vc': [list(c) for c in p.get('vcols', [])]}
            for c in p['children']: walk(c)
        walk(layout['root'])
        ys, xs = np.mgrid[0:self.h, 0:self.w]
        self.X = xs + 0.5 - self.w / 2; self.Y = self.h / 2 - (ys + 0.5)

    def apply(self, anim, f):
        for e in anim['entries']:
            if e['kind'] != 0: warn('material animation'); continue
            p = self.panes.get(e['name'])
            if not p: warn(f'animation of unknown pane {e["name"]}'); continue
            s = p['_s']
            for tag in e['tags']:
                for g in tag['targets']:
                    v = hermite(g['keys'], f) if g['type'] == 2 else step(g['keys'], f)
                    m, t = tag['magic'], g['target']
                    if m == 'CLPA':
                        if t < 3: s['t'][t] = v
                        elif t < 6: s['r'][t - 3] = v
                        elif t < 8: s['s'][t - 6] = v
                        else: s['size'][t - 8] = v
                    elif m == 'CLVI': s['visible'] = bool(v)
                    elif m == 'CLVC':
                        if t == 16: s['alpha'] = max(0, min(255, v))
                        elif s['vc']: s['vc'][t // 4][t % 4] = max(0, min(255, v))
                    else: warn(f'animation tag {m}')

    def render(self):
        img = np.zeros((self.h, self.w, 3))
        def draw(p, M, alpha):
            s = p['_s']
            if not s['visible']: return
            a = math.radians(s['r'][2]); c, n = math.cos(a), math.sin(a)
            T = np.array([[c * s['s'][0], -n * s['s'][1], s['t'][0]], [n * s['s'][0], c * s['s'][1], s['t'][1]], [0, 0, 1]])
            M = M @ T
            mine = s['alpha'] / 255 * alpha
            if p['kind'] == 'pic1' and mine > 0 and s['size'][0] > 0 and s['size'][1] > 0 and abs(np.linalg.det(M)) > 1e-9:
                w, h = s['size']; o = p['origin']
                x0 = (0, -w / 2, -w)[o % 3]; y1 = (0, h / 2, h)[o // 3]          # left edge and top edge in the pane's own space
                inv = np.linalg.inv(M)
                lx = inv[0, 0] * self.X + inv[0, 1] * self.Y + inv[0, 2]; ly = inv[1, 0] * self.X + inv[1, 1] * self.Y + inv[1, 2]
                u = (lx - x0) / w; v = (y1 - ly) / h
                inside = (u >= 0) & (u <= 1) & (v >= 0) & (v <= 1)
                if inside.any():
                    mat = self.lay['materials'][p['material']]
                    if len(mat['maps']) != 1 or mat['tail']: warn(f'material {mat["name"]} with several textures or blend stages')
                    tc = p['texcoords'][0]
                    tu = (tc[0] * (1 - u) + tc[2] * u) * (1 - v) + (tc[4] * (1 - u) + tc[6] * u) * v
                    tv = (tc[1] * (1 - u) + tc[3] * u) * (1 - v) + (tc[5] * (1 - u) + tc[7] * u) * v
                    tex = self.tex[mat['maps'][0][0]]; th, tw = tex.shape
                    fx = np.clip(tu * tw - 0.5, 0, tw - 1); fy = np.clip(tv * th - 0.5, 0, th - 1)        # bilinear, clamped at the edges
                    ix = np.minimum(fx.astype(int), tw - 2) if tw > 1 else np.zeros_like(fx, int); iy = np.minimum(fy.astype(int), th - 2) if th > 1 else np.zeros_like(fy, int)
                    dx = fx - ix; dy = fy - iy
                    ta = (tex[iy, ix] * (1 - dx) + tex[iy, np.minimum(ix + 1, tw - 1)] * dx) * (1 - dy) + (tex[np.minimum(iy + 1, th - 1), ix] * (1 - dx) + tex[np.minimum(iy + 1, th - 1), np.minimum(ix + 1, tw - 1)] * dx) * dy
                    vc = np.array(s['vc']) / 255
                    col = ((vc[0][None, None, :] * (1 - u)[..., None] + vc[1][None, None, :] * u[..., None]) * (1 - v)[..., None]
                           + (vc[2][None, None, :] * (1 - u)[..., None] + vc[3][None, None, :] * u[..., None]) * v[..., None])
                    tint = np.array(list(mat['colours'][0][:3])) / 255
                    cover = np.where(inside, ta * col[..., 3] * mine, 0)[..., None]
                    img[:] = img * (1 - cover) + col[..., :3] * tint * cover
            child_alpha = mine if p['flags'] & 2 else alpha
            for ch in p['children']: draw(ch, M, child_alpha)
        draw(self.lay['root'], np.eye(3), 1.0)
        return Image.fromarray(np.clip(img * 255 + 0.5, 0, 255).astype(np.uint8))


def load(path):
    darc, mac, files = LL.load_logo(path)
    by = {p.split('/')[-1]: d for p, d in files}
    tex = {n: texture_alpha(d) for n, d in by.items() if n.endswith('.bclim')}
    screens = {}
    for key in ('U', 'D'):
        lay = B.parse_layout(by[f'NintendoLogo_{key}_00.bclyt'])
        anims = {x: B.parse_animation(by[f'NintendoLogo_{key}_00_SceneOut{x}.bclan']) for x in 'ABC'}
        screens[key] = Screen(lay, anims, tex)
    return screens


def frames(screens, loops=1, b_frames=None):
    """[(label, both screens as one 400x480 image)] for A once, B looped, C once. The console leaves B at whatever
    frame it is on when the app is ready (seen in the emulator), and what B moved stays as it was through C:
    b_frames plays exactly that many frames of B (default: whole loops)."""
    out = []
    nb = screens['U'].anims['B']['frames']
    total_b = b_frames if b_frames is not None else loops * nb
    for x in 'ABC':
        n = screens['U'].anims[x]['frames']
        assert screens['D'].anims[x]['frames'] == n, f'animation {x} has different lengths on the two screens'
        count = total_b if x == 'B' else n
        for i in range(count):
            f = i % n
            both = Image.new('RGB', (400, 480), (0, 0, 0))
            for key, y in (('U', 0), ('D', 240)):
                s = screens[key]; s.apply(s.anims[x], f)
                im = s.render(); both.paste(im, ((400 - im.width) // 2, y))
            out.append((f'{x}{f}', both))
    return out


def main():
    if len(sys.argv) < 3: sys.exit(__doc__)
    loops = int(sys.argv[3]) if len(sys.argv) > 3 else 1
    fr = frames(load(sys.argv[1]), loops)
    out = sys.argv[2]
    if out.endswith('.gif'):
        ims = [im for _, im in fr[::2]]
        ims[0].save(out, save_all=True, append_images=ims[1:], duration=33, loop=0)
    else:
        every = int(sys.argv[4]) if len(sys.argv) > 4 else 6
        pick = fr[::every]
        cols = min(len(pick), 8); rows = (len(pick) + cols - 1) // cols
        sheet = Image.new('RGB', (cols * 404, rows * 484), (255, 0, 255))
        for k, (label, im) in enumerate(pick): sheet.paste(im, ((k % cols) * 404, (k // cols) * 484))
        sheet.save(out)
    print(f'{len(fr)} frames ({len(fr) / 60:.2f} s with B looped {loops}x): {out}')


if __name__ == '__main__':
    main()
