#!/usr/bin/env python3
"""A small way to describe a start-up logo and turn it into the files of one (see tools/logo_build.py for
the formats and tools/make_logo.py for the app's own logo).

A logo has two screens (Screen(400) for the top, Screen(320) for the bottom; both 240 high, origin in
the middle, y upwards) and three animations the HOME Menu plays in this order (found by comparing
Nintendo's own logo with the homebrew ones):
    A  once, as the logo appears
    B  over and over while the app loads
    C  once, as the logo leaves
Everything drawn is a picture pane: a rectangle showing a 4-bit mask texture in one colour (Nintendo's
logo draws its shapes the same way), times a colour per corner, which gives gradients for free.

    s = Screen(400)
    g = s.null('all')                                   # a group; its alpha applies to what is inside it
    s.pic('bg', 'px', 0, 0, 400, 240, parent=g, corners=['#46aafb', '#46aafb', '#00496e', '#00496e'])
    s.pic('mark', 'tp', 0, 20, 128, 64, colour='#ffffff', parent=g)
    s.anim('A', 'mark', 'y', [(0, -40), (20, 26), (30, 20)])      # keys: (frame, value) or (frame, value, slope)
    s.anim('C', 'all', 'alpha', [(0, 255), (14, 0)])
Properties: x y rot sx sy w h alpha visible, and c0r..c3a for the four corner colours (0 top left,
1 top right, 2 bottom left, 3 bottom right; r g b a).
"""
import os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import logo_build as B

PROPS = {'x': ('CLPA', 0), 'y': ('CLPA', 1), 'rot': ('CLPA', 5), 'sx': ('CLPA', 6), 'sy': ('CLPA', 7), 'w': ('CLPA', 8), 'h': ('CLPA', 9),
         'visible': ('CLVI', 0), 'alpha': ('CLVC', 16)}
for _c in range(4):
    for _k, _ch in enumerate('rgba'): PROPS[f'c{_c}{_ch}'] = ('CLVC', _c * 4 + _k)
TAG_ORDER = ['CLPA', 'CLVI', 'CLVC']


def rgb(c):
    c = c.lstrip('#')
    return bytes(int(c[i:i + 2], 16) for i in (0, 2, 4))


def linear(points):
    """Keys with slopes that join the points with straight lines."""
    out = []
    for i, (f, v) in enumerate(points):
        if i + 1 < len(points): s = (points[i + 1][1] - v) / (points[i + 1][0] - f)
        else: s = (v - points[i - 1][1]) / (f - points[i - 1][0])
        out.append((f, v, s))
    return out


class Screen:
    def __init__(self, width):
        self.width = width
        self.textures, self.materials = [], []
        self.root = self._pane('pan1', 'RootPane', 0, 0, width, 240, flags=1)
        self.by_name = {'RootPane': self.root}
        self.tracks = {'A': {}, 'B': {}, 'C': {}}

    def _pane(self, kind, name, x, y, w, h, flags, alpha=255, scale=(1, 1), rot=0):
        return {'kind': kind, 'name': name, 'flags': flags, 'origin': 4, 'alpha': alpha, 'pad': 0, 'data': bytes(8),
                'translate': (x, y, 0.0), 'rotate': (0.0, 0.0, rot), 'scale': tuple(scale), 'size': (w, h), 'children': []}

    def _add(self, p, parent):
        assert p['name'] not in self.by_name, f'two panes named {p["name"]}'
        (self.by_name[parent] if parent else self.root)['children'].append(p)
        self.by_name[p['name']] = p
        return p['name']

    def null(self, name, x=0, y=0, parent=None, alpha=255):
        """A pane that draws nothing; what is inside it moves with it and takes its alpha."""
        return self._add(self._pane('pan1', name, x, y, 32, 32, flags=3, alpha=alpha), parent)

    def pic(self, name, texture, x, y, w, h, colour='#ffffff', corners=None, alpha=255, scale=(1, 1), rot=0, parent=None, visible=True):
        if texture not in self.textures: self.textures.append(texture)
        p = self._pane('pic1', name, x, y, w, h, flags=1 if visible else 0, alpha=alpha, scale=scale, rot=rot)
        p['vcols'] = [rgb(c) + b'\xff' if isinstance(c, str) else bytes(c) for c in (corners or ['#ffffff'] * 4)]
        p['material'] = len(self.materials)
        p['texcoords'] = [[0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 1.0, 1.0]]
        # one texture, shown as it is: the form of the simplest materials in Nintendo's logo
        self.materials.append({'name': name, 'colours': [rgb(colour) + b'\x00'] + [b'\xff' * 4] * 6, 'flags_hi': 0,
                               'maps': [(self.textures.index(texture), 0, 1, 0, 1)], 'matrices': [(0.0, 0.0, 0.0, 1.0, 1.0)], 'gens': [bytes(4)], 'tail': b''})
        return self._add(p, parent)

    def anim(self, which, pane, prop, keys):
        assert pane in self.by_name and prop in PROPS, f'{pane}.{prop}'
        keys = [tuple(k) if len(k) == 3 or prop == 'visible' else (k[0], k[1], 0.0) for k in keys]
        self.tracks[which].setdefault(pane, {})[prop] = keys

    def hold(self, which, pane, prop, value):
        """One key: the property simply has this value throughout the animation."""
        self.anim(which, pane, prop, [(0, value)])

    def layout(self):
        groups = {'name': 'RootGroup', 'panes': [], 'children': [
            {'name': f'G_{x}_00', 'panes': list(self.tracks[x]) or [self.root['children'][0]['name']], 'children': []} for x in 'ABC']}
        return {'origin': 1, 'canvas': (float(self.width), 240.0), 'textures': [t + '.bclim' for t in self.textures],
                'materials': self.materials, 'root': self.root, 'groups': groups}

    def animation(self, which, frames, lengths):
        # the positions on the authoring timeline, laid out as in the homebrew logo: B first, then A, then C
        start = {'B': 0, 'A': lengths['B'] + 1, 'C': lengths['B'] + 1 + lengths['A']}[which]
        end = start + frames - (0 if which == 'B' else 1)
        entries = []
        for pane, props in self.tracks[which].items():
            tags = {}
            for prop, keys in props.items():
                magic, target = PROPS[prop]
                step = prop == 'visible'
                tags.setdefault(magic, []).append({'index': 0, 'target': target, 'type': 1 if step else 2,
                                                   'keys': [(float(k[0]), int(k[1])) if step else (float(k[0]), float(k[1]), float(k[2])) for k in keys]})
            entries.append({'name': pane, 'kind': 0, 'tags': [{'magic': m, 'targets': sorted(tags[m], key=lambda t: t['target'])} for m in TAG_ORDER if m in tags]})
        return {'name': f'SceneOut{which}', 'order': 'ABC'.index(which), 'groups': [f'G_{which}_00'], 'start': start, 'end': end, 'descending': 1,
                'frames': frames, 'loop': 1 if which == 'B' else 0, 'entries': entries}


def build(top, bottom, textures, lengths):
    """The archive's files from the two screens, {texture name: PIL 'L' mask} and {'A': frames, 'B': frames, 'C': frames}."""
    files = []
    for key, s in (('D', bottom), ('U', top)):
        for x in 'ABC':
            files.append(('anim', f'NintendoLogo_{key}_00_SceneOut{x}.bclan', B.write_animation(s.animation(x, lengths[x], lengths))))
    files.sort(key=lambda f: f[1])
    for key, s in (('D', bottom), ('U', top)):
        files.append(('blyt', f'NintendoLogo_{key}_00.bclyt', B.write_layout(s.layout())))
    used = sorted(set(top.textures) | set(bottom.textures))
    for t in used: files.append(('timg', t + '.bclim', B.clim_a4(textures[t])))
    return files
