#!/usr/bin/env python3
"""Builds the 3D scene of a HOME Menu banner from plain descriptions: textures, materials, bones, meshes
and bone animation. Everything the format needs besides (animation groups, matrices, hashes, colour bytes,
bounding boxes, layout) is derived here, following what the system's own banners contain; tools/cgfx.py
writes the file.

What the HOME Menu does with the scene (see docs/banner.md):
  * it draws the model named COMMON with its own camera: 30 degrees tall, at (0, 1, 44.786) looking down -z,
    so at z = 0 the screen shows 40 x 24 units (1 unit = 10 pixels) centred on y = 1
  * it plays the skeletal animation named COMMON, 60 frames a second, looping
  * a bone with a billboard mode keeps facing the viewer (logos: Banner.Y_AXIAL)

Conventions kept from the system's banners because consoles are known to accept them: one rigid mesh per
bone (no skinning), float vertex data in one interleaved buffer, the three animation groups listing every
bone, mesh and material value, hermite keys for bone animation.

  b = Banner()
  b.texture('COMMON1', image)                         # a PIL image; format chosen with fmt=
  b.material_flat('logo', 'COMMON1')                  # texture x vertex colour, alpha blended
  root = b.bone('root')
  card = b.bone('logo', root, translation=(0, 1, 0), billboard=Banner.Y_AXIAL)
  b.mesh(card, 'logo', positions, triangles, uvs=...)
  b.animate(card, 'ty', [(0, 1.0), (300, 1.4), (600, 1.0)])
  open('banner.cgfx', 'wb').write(b.build())
"""
import math, os, struct, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cgfx, cgfx_hash
from cgfx import Obj, Segment

FORMATS = {'RGBA8': 0, 'RGB8': 1, 'RGBA5551': 2, 'RGB565': 3, 'RGBA4': 4, 'LA8': 5, 'L8': 7, 'A8': 8, 'LA4': 9, 'L4': 10, 'A4': 11}
GL_FORMAT = [0x6752, 0x6754, 0x6752, 0x6754, 0x6752, 0x6758, 0x6759, 0x6757, 0x6756, 0x6758, 0x6757, 0x6756, 0x675A, 0x675B]
GL_TYPE = [0x1401, 0x1401, 0x8034, 0x8363, 0x8033, 0x1401, 0x1401, 0x1401, 0x1401, 0x6760, 0x6761, 0x6761, 0, 0]
BPP = [32, 24, 16, 16, 16, 16, 16, 8, 8, 8, 4, 4, 4, 8]
# texture wrapping, as the filter word holds it
CLAMP, BORDER, REPEAT, MIRROR = 0, 1, 2, 3
# combiner sources and ways to combine
VERTEX, LIGHT, SPECULAR, TEX0, TEX1, TEX2, CONST, PREV = 0, 1, 2, 3, 4, 5, 14, 15
REPLACE, MODULATE, ADD, ADD_SIGNED, INTERPOLATE, SUBTRACT, MULT_ADD, ADD_MULT = 0, 1, 2, 3, 4, 5, 8, 9
RGB, ONE_MINUS_RGB, ALPHA, ONE_MINUS_ALPHA = 0, 1, 2, 3          # what to take from a colour source


def morton(x, y):
    v = 0
    for i in range(3): v |= (x >> i & 1) << (2 * i) | (y >> i & 1) << (2 * i + 1)
    return v


def encode_texture(image, fmt):
    """Pixels of a PIL image in the console's layout: 8x8 tiles, Z-order inside a tile; the image's top row
    comes first, which the console addresses as v = 1."""
    w, h = image.size
    if w & (w - 1) or h & (h - 1) or w < 8 or h < 8: raise ValueError(f'texture size {w}x{h}: each side must be a power of two, 8 or more')
    px = image.convert('RGBA').load()
    f = FORMATS[fmt]
    size = {4: 2, 3: 2, 2: 2, 5: 2, 0: 4, 1: 3, 7: 1, 8: 1, 9: 1}.get(f)
    if f in (10, 11):
        out = bytearray(w * h // 2)
        for y in range(h):
            for x in range(w):
                r, g, b, a = px[x, y]
                v = (a if f == 11 else (r * 299 + g * 587 + b * 114) // 1000) >> 4
                i = ((y >> 3) * (w >> 3) + (x >> 3)) * 64 + morton(x & 7, y & 7)
                out[i >> 1] |= v << (4 * (i & 1))
        return bytes(out)
    out = bytearray(w * h * size)
    for y in range(h):
        for x in range(w):
            r, g, b, a = px[x, y]
            lum = (r * 299 + g * 587 + b * 114) // 1000
            i = (((y >> 3) * (w >> 3) + (x >> 3)) * 64 + morton(x & 7, y & 7)) * size
            if f == 4: v = struct.pack('<H', (r >> 4) << 12 | (g >> 4) << 8 | (b >> 4) << 4 | a >> 4)
            elif f == 3: v = struct.pack('<H', (r >> 3) << 11 | (g >> 2) << 5 | b >> 3)
            elif f == 2: v = struct.pack('<H', (r >> 3) << 11 | (g >> 3) << 6 | (b >> 3) << 1 | a >> 7)
            elif f == 5: v = bytes([a, lum])
            elif f == 0: v = bytes([a, b, g, r])
            elif f == 1: v = bytes([b, g, r])
            elif f == 7: v = bytes([lum])
            elif f == 8: v = bytes([a])
            elif f == 9: v = bytes([(lum >> 4) << 4 | a >> 4])
            out[i:i + size] = v
    return bytes(out)


def srt_matrix(s, r, t):
    """3x4 matrix of scale, then rotation about x, y, z in turn, then translation (matches every bone in the
    system's banners)."""
    cx, sx, cy, sy, cz, sz = math.cos(r[0]), math.sin(r[0]), math.cos(r[1]), math.sin(r[1]), math.cos(r[2]), math.sin(r[2])
    rot = [[cz * cy, cz * sy * sx - sz * cx, cz * sy * cx + sz * sx],
           [sz * cy, sz * sy * sx + cz * cx, sz * sy * cx - cz * sx],
           [-sy, cy * sx, cy * cx]]
    return [[rot[i][0] * s[0] + 0.0, rot[i][1] * s[1] + 0.0, rot[i][2] * s[2] + 0.0, t[i]] for i in range(3)]


def mat_mul(a, b):
    a4, b4 = a + [[0, 0, 0, 1]], b + [[0, 0, 0, 1]]
    return [[sum(a4[i][k] * b4[k][j] for k in range(4)) for j in range(4)] for i in range(3)]


def mat_inverse(m):
    a = [m[i][:3] for i in range(3)]
    det = (a[0][0] * (a[1][1] * a[2][2] - a[1][2] * a[2][1]) - a[0][1] * (a[1][0] * a[2][2] - a[1][2] * a[2][0])
           + a[0][2] * (a[1][0] * a[2][1] - a[1][1] * a[2][0]))
    def minor(r, c):
        (r0, r1), (c0, c1) = [k for k in range(3) if k != r], [k for k in range(3) if k != c]
        return a[r0][c0] * a[r1][c1] - a[r0][c1] * a[r1][c0]
    inv = [[(-1.0 if (i + j) & 1 else 1.0) * minor(j, i) / det for j in range(3)] for i in range(3)]
    t = [m[i][3] for i in range(3)]
    return [inv[i] + [-(inv[i][0] * t[0]) - inv[i][1] * t[1] - inv[i][2] * t[2]] for i in range(3)]


def flat(m):
    return tuple(float(v) for row in m for v in row)


def stage(index, rgb=None, alpha=None, constant=0, rgb_scale=0, alpha_scale=0):
    """One combiner stage. rgb and alpha are (way to combine, sources); a source is a number (VERTEX, TEX0,
    ...) or (source, what to take from it). Left out, the stage passes the previous result on."""
    def side(spec, is_alpha):
        if spec is None: return 0, 0xE1F, 0
        how, srcs = spec
        src = op = 0
        for k in range(3):
            s = srcs[k] if k < len(srcs) else CONST
            s, o = s if isinstance(s, tuple) else (s, ALPHA if is_alpha else RGB)
            src |= s << (4 * k)
            # the alpha side numbers its choices from "alpha" = 0
            op |= (o - 2 << (12 + 4 * k)) if is_alpha else (o << (4 * k))
        return how, src, op
    ch, cs, co = side(rgb, False)
    ah, as_, ao = side(alpha, True)
    return Obj('TexEnv', constant=constant, source=cs | as_ << 16, header=0x804F0000 | [0xC0, 0xC8, 0xD0, 0xD8, 0xF0, 0xF8][index],
               operand=co | ao, combine=ch | ah << 16, color=0xFF000000, scale=rgb_scale | alpha_scale << 16)


class Banner:
    WORLD, WORLD_VIEWPOINT, SCREEN, SCREEN_VIEWPOINT, Y_AXIAL, Y_AXIAL_VIEWPOINT = 1, 2, 3, 4, 5, 6

    def __init__(self, frames=600):
        self.frames = float(frames)
        self.scene = cgfx.Scene()
        self.model = Obj('CMDL', name='COMMON')
        self.scene.models.add('COMMON', self.model)
        self.model.skeleton = Obj('Skeleton')
        self.bones, self.tracks, self.alpha_refs = [], {}, {}

    # ---- textures
    def texture(self, name, image, fmt='RGBA4'):
        f = FORMATS[fmt]
        data = encode_texture(image, fmt)
        img = Obj('Image', height=image.height, width=image.width, data=data, bpp=BPP[f])
        self.scene.textures.add(name, Obj('TexImage', name=name, height=image.height, width=image.width, glFormat=GL_FORMAT[f],
                                          glType=GL_TYPE[f], format=f, image=img))

    def texture_raw(self, name, width, height, fmt, data):
        f = FORMATS[fmt]
        img = Obj('Image', height=height, width=width, data=data, bpp=BPP[f])
        self.scene.textures.add(name, Obj('TexImage', name=name, height=height, width=width, glFormat=GL_FORMAT[f],
                                          glType=GL_TYPE[f], format=f, image=img))

    # ---- materials
    def material(self, name, textures=(), stages=(), blend=False, depth_write=None, layer=None, lit=False, double_sided=False,
                 alpha_test=None, constants=(), diffuse=(1, 1, 1, 1), ambient=(1, 1, 1, 1), emission=(0, 0, 0, 0),
                 specular0=(1, 1, 1, 0), specular1=(0, 0, 0, 0), lighting=None):
        """textures: up to 3 of (texture name, options): wrap=(u, v), filter (True = smooth), mapping ('uv' or
        'sphere': looked up by the surface's direction as seen by the camera, for painted-on shading), uv set.
        stages: Obj made by stage(); the rest pass through.
        blend: alpha blending, drawn after the solid parts and without writing depth.
        alpha_test: discard pixels whose alpha is below this (0..1).
        lit: the scene's lights (light()) shade the surface; combiner stages then read the result as LIGHT
        (emission + ambient + diffuse by how far the surface faces each light) and SPECULAR (highlights).
        lighting: for highlights, {'flags': .., 'layer': .., 'tables': {'dist0': (table name, input, scale), ..}}
        naming tables added with table(); see the comment at LIGHTING below."""
        m = Obj('MTOB', name=name)
        m.flags = 1 if lit else 0
        m.renderLayer = (1 if blend else 0) if layer is None else layer
        f = lambda c: tuple(float(x) for x in c)
        m.emission, m.ambient, m.diffuse, m.specular0, m.specular1 = f(emission), f(ambient), f(diffuse), f(specular0), f(specular1)
        cons = [f(c) for c in constants] + [(0.0, 0.0, 0.0, 1.0)] * (6 - len(constants))
        m.constant = tuple(v for c in cons for v in c)
        if depth_write is None: depth_write = not blend
        m.depthFlags = 1 | (2 if depth_write else 0)
        if blend:
            m.blendMode = 1
            m.blendCmd = (0x00E40100, 0x803F0100, 0x76760000, 0, 0xFF000000, 0)     # source alpha, 1 - source alpha
        if double_sided:
            m.cullMode, m.rasterCmd = 3, (0, 0x00010040)
        m.usedTexCoords = len(textures)
        for i, (tex, opt) in enumerate((t if isinstance(t, tuple) else (t, {})) for t in textures):
            c = m.texCoords[i]
            c.scale = (1.0, 1.0)
            c.matrix = (1.0, -0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
            c.source = opt.get('uv', 0)
            if opt.get('mapping') == 'sphere': c.mapping, c.refCamera = 2, -1
            wu, wv = opt.get('wrap', (REPEAT, REPEAT))
            smooth = opt.get('filter', True)
            cmd = [0, [0x0001008E, 0x00010096, 0x0001009E][i], 0xFF000000, [0x809F0081, 0x809F0091, 0x809F0099][i],
                   0, (6 if smooth else 0) | wv << 8 | wu << 12] + [0] * 8
            mp = Obj('TexMapper', cmd=tuple(cmd))
            mp.texture = Obj('TexRef', link=tex)
            mp.sampler = Obj('Sampler', owner=mp, minFilter=1 if smooth else 0)
            setattr(m, f'mapper{i}', mp)
        m.shader = Obj('ShaderRef')
        fs = m.fragShader = Obj('FragShader')
        fs.lutTable = Obj('LutTable')
        if lighting:
            fs.lightFlags, fs.layerConfig = lighting.get('flags', 0), lighting.get('layer', 0)
            for slot, (table, source, scale) in lighting.get('tables', {}).items():
                ref = Obj('LutRef', path=self.TABLES, table=table)
                setattr(fs.lutTable, slot, Obj('LutSampler', input=source, scale=scale, sampler=ref))
        for i in range(6):
            fs.stages[i] = stages[i] if i < len(stages) else stage(i)
        if alpha_test is not None:
            fs.alphaTest = (1 | 6 << 4 | int(alpha_test * 255 + 0.5) << 8, 0x000F0104)        # keep if alpha > reference
            self.alpha_refs[name] = float(alpha_test)
        self.model.materials.add(name, m)
        return m

    def material_flat(self, name, texture, **kw):
        """Texture x vertex colour, alpha blended: logos, text, soft shadows."""
        kw.setdefault('blend', True)
        return self.material(name, [texture] if not isinstance(texture, list) else texture,
                             [stage(0, (MODULATE, (VERTEX, TEX0)), (MODULATE, (VERTEX, TEX0)))], **kw)

    def material_shaded(self, name, colour_texture, shade_texture, base=0.5, shine=0.25, **kw):
        """A solid surface shaded without lights, the way the Activity Log banner shades its pencils: the
        shade texture (alpha only) is looked up by the direction each point faces as seen from the camera,
        and the result is (shade + base) x colour + shade x shine."""
        return self.material(name, [colour_texture, (shade_texture, {'mapping': 'sphere', 'wrap': (CLAMP, CLAMP)})],
                             [stage(0, (ADD_MULT, ((TEX1, ALPHA), (CONST, ALPHA), TEX0)), (REPLACE, (VERTEX,)), constant=0),
                              stage(1, (MULT_ADD, ((TEX1, ALPHA), (CONST, ALPHA), PREV)), (REPLACE, (PREV,)), constant=1)],
                             constants=[(0, 0, 0, base), (0, 0, 0, shine)], **kw)

    # ---- lights (only materials made with lit=True use them)
    # LIGHTING. The GPU computes two colours per pixel. LIGHT = emission + sum over lights of (light ambient x
    # material ambient + light diffuse x material diffuse x max(0, N.L)). SPECULAR = sum over lights of
    # (light specular0 x material specular0 x dist0 + light specular1 x (material specular1, or the reflect
    # tables with flag 0x20) x dist1), where dist0, dist1 and the reflect tables are curves (table()) looked up
    # by an angle: input 0 = N.H (between the surface and halfway to the light: a classic highlight),
    # 1 = V.H, 2 = N.V (facing the viewer or edge-on: rim light), 3 = L.N. flags: 2 = use dist0, 4 = use dist1,
    # 0x20 = use the reflect tables. layer says which tables the GPU evaluates: 0 = dist0 and reflect red;
    # 2 = dist0, dist1, reflect red; 4 = all of them.
    TABLES = 'COMMON.MaterialLutset'

    def light(self, name, direction, diffuse=(1, 1, 1), ambient=(0, 0, 0), specular0=(1, 1, 1), specular1=(1, 1, 1)):
        """A light shining along `direction` (towards where the light goes), like the system banners' lights."""
        n = math.sqrt(sum(d * d for d in direction))
        col = lambda c: tuple(float(x) for x in c) + (1.0,)
        l = Obj('FragLight', name=name, direction=tuple(d / n for d in direction), ambient=col(ambient), diffuse=col(diffuse),
                specular0=col(specular0), specular1=col(specular1), attenuationEnd=1.0, attenuationScale=0x3F000,
                attenuationBias=0x80000, lightFlags=1)
        l.colorBytes = tuple(max(0, min(255, int(c * 255 + 0.5))) for c4 in (l.ambient, l.diffuse, l.specular0, l.specular1) for c in c4)
        g = Obj('AnimGroup', name='LightAnimation', flags=0, memberType=4, evalTiming=0); g.blendOps = [8, 3, 6, 2, 0]
        g.members.add('Transform', Obj('MemberTransform', path='Transform', valueOffset=48, valueSize=36))
        for mname, off, size, blend, obj, idx in (('Ambient', 188, 16, 1, 13, 0), ('Diffuse', 204, 16, 1, 13, 1), ('Specular0', 220, 16, 1, 13, 2),
                                                 ('Specular1', 236, 16, 1, 13, 3), ('Direction', 268, 12, 2, 13, 4),
                                                 ('DistanceAttenuationStart', 288, 4, 3, 13, 5), ('DistanceAttenuationEnd', 292, 4, 3, 13, 6),
                                                 ('IsLightEnabled', 180, 1, 4, 12, 0)):
            g.members.add(mname, Obj('MemberOther', path=mname, valueOffset=off, valueSize=size, blendIndex=blend, objType=obj, memberIndex=idx))
        l.animGroups.add('LightAnimation', g)
        self.scene.lights.add(name, l)
        env = self.scene.scenes.get('SceneEnvironment1')
        if env is None:
            env = self.scene.scenes.add('SceneEnvironment1', Obj('Scene', name='SceneEnvironment1', revision=0x01000000))
            env.lightSets = [Obj('LightSet', id=0)]
        refs = env.lightSets[0].lights
        refs.append(Obj('SceneRef', id=len(refs), name=name))
        return l

    def table(self, name, values):
        """A lighting curve: 256 values 0..1, for inputs 0..1 (see LIGHTING)."""
        if len(values) != 256: raise ValueError('a lighting table has 256 values')
        fixed = []
        for i, v in enumerate(values):
            nxt = values[i + 1] if i < 255 else v
            fixed.append(min(0xFFF, max(0, int(v * 0xFFF))) | (int((nxt - v) * 0x7FF) & 0xFFF) << 12)
        words = [fixed[0], 0x07FF01C8] + fixed[1:128] + [0, fixed[128], 0x07FF01C8] + fixed[129:] + [0]
        luts = self.scene.luts.get(self.TABLES)
        if luts is None: luts = self.scene.luts.add(self.TABLES, Obj('LUTS', name=self.TABLES))
        luts.tables.add(name, Obj('Lut', name=name, commands=struct.pack(f'<{len(words)}I', *words)))

    # ---- bones
    def bone(self, name, parent=None, translation=(0, 0, 0), rotation=(0, 0, 0), scale=(1, 1, 1), billboard=0):
        b = Obj('Bone', name=name, index=len(self.bones), billboard=billboard)
        b.scale, b.rotation, b.translation = (tuple(float(v) for v in x) for x in (scale, rotation, translation))
        if parent is not None:
            b.parent, b.parentIndex = parent, parent.index
            if parent.child is None: parent.child = b
            else:
                last = parent.child
                while last.next is not None: last = last.next
                last.next, b.prev = b, last
        self.bones.append(b)
        self.model.skeleton.bones.add(name, b)
        return b

    # ---- meshes
    def mesh(self, bone, material, positions, triangles, uvs=None, normals=None, colors=None, priority=0):
        """One rigid mesh that moves with a bone. colors are (r, g, b, a) 0..1 per vertex; without them the
        mesh gets one fixed colour, white."""
        mdl = self.model
        n = len(positions)
        attrs, fmt, rows, offset = [], '<', [[] for _ in range(n)], 0
        def add(usage, comps, data, kind='f', scale=1.0):
            nonlocal fmt, offset
            if data is None: return
            attrs.append(Obj('Attr', usage=usage, format=0x1406 if kind == 'f' else 0x1401, components=comps, scale=scale, offset=offset))
            fmt += f'{comps}{kind}'; offset += comps * (4 if kind == 'f' else 1)
            for i in range(n): rows[i] += list(data[i])
        add(0, 3, positions)
        add(1, 3, normals)
        if colors is not None:
            add(3, 4, [[max(0, min(255, int(c * 255 + 0.5))) for c in col] for col in colors], 'B', 1 / 255)
        add(4, 2, uvs)
        inter = Obj('Interleaved', stride=offset, data=b''.join(struct.pack(fmt, *r) for r in rows))
        inter.attrs = attrs
        wide = n > 256
        idx = [i for t in triangles for i in t]
        stream = Obj('IndexStream', format=0x1403 if wide else 0x1401, data=struct.pack(f'<{len(idx)}{"H" if wide else "B"}', *idx))
        prim = Obj('Primitive'); prim.streams, prim.bufferObjs = [stream], [0]
        pset = Obj('PrimSet'); pset.bones, pset.prims = [bone.index], [prim]
        lo = [min(p[k] for p in positions) for k in range(3)]; hi = [max(p[k] for p in positions) for k in range(3)]
        shape = Obj('Shape')
        shape.bbox = Obj('BBox', center=tuple((lo[k] + hi[k]) / 2 for k in range(3)), size=tuple(float(hi[k] - lo[k]) for k in range(3)))
        shape.primSets, shape.attrs = [pset], [inter]
        if colors is None:
            fixed = Obj('FixedAttr'); fixed.values = [1.0, 1.0, 1.0, 1.0]
            shape.attrs.append(fixed)
        mesh = Obj('Mesh', shapeIndex=len(mdl.shapes), materialIndex=mdl.materials.index(material), owner=mdl, priority=priority,
                   meshNodeName=bone.name)
        mdl.meshes.append(mesh); mdl.shapes.append(shape)
        return mesh

    # ---- animation
    def animate(self, bone, channel, keys):
        """channel: sx sy sz rx ry rz tx ty tz (rotations in radians). keys: (frame, value) or (frame, value,
        slope); slopes left out are chosen so the curve passes smoothly through the keys (and, when the first
        and last values match, through the loop)."""
        self.tracks.setdefault(bone.name, {})[channel] = sorted(tuple(float(v) for v in k) for k in keys)

    def _curve(self, keys):
        n = len(keys)
        loop = abs(keys[0][1] - keys[-1][1]) < 1e-9
        out = []
        for i, k in enumerate(keys):
            if len(k) > 2: out.append(k); continue
            if 0 < i < n - 1: a, b = keys[i - 1], keys[i + 1]
            elif loop and n > 2:
                # the neighbour across the loop
                a = keys[-2] if i == 0 else keys[i - 1]
                b = keys[1] if i == n - 1 else keys[i + 1]
                span = (keys[-1][0] - keys[-2][0]) + (keys[1][0] - keys[0][0])
                out.append((k[0], k[1], (b[1] - a[1]) / span)); continue
            else: a, b = (keys[0], keys[1]) if i == 0 else (keys[-2], keys[-1])
            out.append((k[0], k[1], (b[1] - a[1]) / (b[0] - a[0])))
        seg = Segment(0.0, self.frames, interpolation=2, quant=3, keys=out)
        c = Obj('Curve', start=0.0, end=self.frames, flags=4)
        c.segments = [seg]
        return c

    # ---- finishing
    def _groups(self):
        mdl = self.model
        g = Obj('AnimGroup', name='SkeletalAnimation', flags=1, memberType=1, evalTiming=1); g.blendOps = [8]
        for b in self.bones:
            g.members.add(b.name, Obj('MemberBone', path=b.name, owner=b.name, bone=b.name))
        mdl.animGroups.add('SkeletalAnimation', g)
        g = Obj('AnimGroup', name='VisibilityAnimation', flags=0, memberType=3, evalTiming=0); g.blendOps = [0]
        g.members.add('IsVisible', Obj('MemberModel', path='IsVisible', valueOffset=0xD4, valueSize=1, memberIndex=1))
        for i in range(len(mdl.meshes)):
            p = f'Meshes[{i}].IsVisible'
            g.members.add(p, Obj('MemberMesh', path=p, owner=str(i), valueOffset=0x24, valueSize=1, index=i))
        mdl.animGroups.add('VisibilityAnimation', g)
        g = Obj('AnimGroup', name='MaterialAnimation', flags=0, memberType=2, evalTiming=1); g.blendOps = [3, 7, 5, 2]
        mats = [(n, m) for n, m in mdl.materials]
        colours = ['Emission', 'Ambient', 'Diffuse', 'Specular0', 'Specular1'] + [f'Constant{i}' for i in range(6)]
        for ci, cn in enumerate(colours):
            for n, m in mats:
                p = f'Materials["{n}"].MaterialColor.{cn}'
                g.members.add(p, Obj('MemberMatColor', path=p, owner=n, sub='MaterialColor', valueOffset=16 * ci, valueSize=16,
                                     memberIndex=ci, material=n))
        maps = [(n, i) for n, m in mats for i in range(3) if getattr(m, f'mapper{i}') is not None]
        for n, i in maps:
            p = f'Materials["{n}"].TextureMappers[{i}].Sampler.BorderColor'
            g.members.add(p, Obj('MemberSampler', path=p, owner=n, sub=f'TextureMappers[{i}].Sampler', valueOffset=12, valueSize=16,
                                 material=n, index=i))
        for n, i in maps:
            p = f'Materials["{n}"].TextureMappers[{i}].Texture'
            g.members.add(p, Obj('MemberMapper', path=p, owner=n, sub=f'TextureMappers[{i}]', valueOffset=8, valueSize=4, blendIndex=1,
                                 material=n, index=i))
        for n, m in mats:
            p = f'Materials["{n}"].FragmentOperation.BlendOperation.BlendColor'
            g.members.add(p, Obj('MemberBlendOp', path=p, owner=n, sub='FragmentOperation.BlendOperation', valueOffset=4, valueSize=16,
                                 material=n))
        coords = [(n, i) for n, m in mats for i in range(m.usedTexCoords)]
        for what, off, size, blend, idx in (('Scale', 16, 8, 2, 0), ('Rotate', 24, 4, 3, 1), ('Translate', 28, 8, 2, 2)):
            for n, i in coords:
                p = f'Materials["{n}"].TextureCoordinators[{i}].{what}'
                g.members.add(p, Obj('MemberCoord', path=p, owner=n, sub=f'TextureCoordinators[{i}]', valueOffset=off, valueSize=size,
                                     blendIndex=blend, memberIndex=idx, material=n, index=i))
        mdl.animGroups.add('MaterialAnimation', g)

    def build(self):
        mdl = self.model
        # bones: matrices and flags
        world = {}
        for b in self.bones:
            local = srt_matrix(b.scale, b.rotation, b.translation)
            world[b.index] = mat_mul(world[b.parentIndex], local) if b.parentIndex >= 0 else local
            b.local = flat(local)
            b.invBase = flat(mat_inverse(world[b.index]))
            t0, r0, s1 = all(v == 0 for v in b.translation), all(v == 0 for v in b.rotation), all(v == 1 for v in b.scale)
            b.flags = (0x1C0 | (2 if t0 else 0) | (4 if r0 else 0) | (8 if s1 else 0) | (0x10 if b.scale[0] == b.scale[1] == b.scale[2] else 0)
                       | (1 if t0 and r0 and s1 else 0))
        mdl.skeleton.rootBone = self.bones[0]
        # materials: colours as bytes, hashes
        for name, m in mdl.materials:
            cols = [m.emission, m.ambient, m.diffuse, m.specular0, m.specular1] + [m.constant[i * 4:i * 4 + 4] for i in range(6)]
            by = [max(0, min(255, int(c * 255 + 0.5))) for col in cols for c in col]
            by[7] = 0          # the ambient colour's fourth number is a scale, not an alpha
            m.colorBytes = tuple(by)
            m.hashes = cgfx_hash.hashes(m, self.alpha_refs.get(name))
        self._groups()
        # the animation: one entry per animated bone, only the animated channels present
        if self.tracks:
            anim = Obj('CANM', frames=self.frames)
            for b in self.bones:
                tr = self.tracks.get(b.name)
                if not tr: continue
                mem = Obj('AnimTransform', path=b.name, unk1=b.name, primType=5)
                flags = 0
                for f in cgfx.KINDS['AnimTransform']:
                    if not f.typ.startswith('slot'): continue
                    const, ignore = [int(x) for x in f.typ.split(':')[1:]]
                    keys = tr.get(f.name)
                    if keys is None: flags |= 1 << ignore
                    elif len(keys) == 1: flags |= 1 << const; setattr(mem, f.name, keys[0][1])
                    else: setattr(mem, f.name, self._curve(keys))
                mem.flags = flags | 1 << 22          # the unused word between the rotations and the translations
                anim.members.add(b.name, mem)
            self.scene.skeletalAnims.add('COMMON', anim)
        data = cgfx.write(self.scene)
        if len(data) > 0x80000: raise ValueError(f'the scene is {len(data)} bytes; the HOME Menu takes at most {0x80000}')
        return data
