#!/usr/bin/env python3
"""Checks tools/banner_scene.py against a banner the console is known to accept: takes the plain contents
of the Activity Log's banner (vertices, textures, bone placements, animation keys, material settings as
this project's builder expresses them), builds a scene from them, and compares the result with the
original, value by value. Only the shapes' bounding boxes are expected to differ (the original tool fits
tilted boxes; nothing reads them).

  banner_selftest.py     needs app/build/banner/samples (tools/banner_samples.py); Nintendo's file is only read
"""
import os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cgfx
from banner_scene import *

SAMPLE = os.path.join(os.path.dirname(HERE), 'app', 'build', 'banner', 'samples', '00040010_00022200.cgfx')


def differences(a, b, path, out, skip=('hint', 'off')):
    if isinstance(a, cgfx.Dict):
        if [n for n, _ in a] != [n for n, _ in b]: out.append(f'{path}: names {[n for n, _ in a][:4]} / {[n for n, _ in b][:4]}'); return
        for (n, x), (_, y) in zip(a, b): differences(x, y, f'{path}[{n}]', out)
    elif isinstance(a, (list, tuple)) and a and hasattr(a[0], 'kind'):
        if len(a) != len(b): out.append(f'{path}: {len(a)} / {len(b)} entries'); return
        for i, (x, y) in enumerate(zip(a, b)): differences(x, y, f'{path}[{i}]', out)
    elif hasattr(a, 'kind'):
        if not hasattr(b, 'kind') or a.kind != b.kind: out.append(f'{path}: {a!r} / {b!r}'); return
        if a.kind == 'BBox': return
        for k, v in vars(a).items():
            if k in skip or k in ('owner', 'parent', 'child', 'prev', 'next', 'rootBone'):
                if k not in skip and (getattr(v, 'name', None) != getattr(getattr(b, k), 'name', None)): out.append(f'{path}.{k}: points elsewhere')
                continue
            differences(v, getattr(b, k), f'{path}.{k}', out)
    elif isinstance(a, (list, tuple)) and len(a) == len(b if isinstance(b, (list, tuple)) else ()) and all(isinstance(x, float) for x in a):
        if any(abs(x - y) > 1e-5 * max(1, abs(x)) for x, y in zip(a, b)) or any(str(x)[0] != str(y)[0] for x, y in zip(a, b) if x == 0):
            out.append(f'{path}: {a} / {b}')
    elif a != b:
        out.append(f'{path}: {a!r:.120} / {b!r:.120}')


def main():
    original = cgfx.read(open(SAMPLE, 'rb').read())
    om = original.models.get('COMMON')
    b = Banner(600)
    for name, t in original.textures:
        b.texture_raw(name, t.width, t.height, cgfx.FORMATS[t.format], t.image.data)
    flat_mat = b.material_flat('c_mt_01', ('COMMON1', {'wrap': (MIRROR, MIRROR)}))
    flat_mat.stencilCmd = (0xFF000000, 0x000D0105, 0, 0x000F0106)
    b.material_shaded('c_mt_02', ('COMMON2', {'wrap': (CLAMP, CLAMP)}), 'COMMON3', base=0.5, shine=0.25)
    bones = {}
    for name, ob in om.skeleton.bones:
        parent = bones[ob.parent.name] if ob.parent is not None else None
        bones[name] = b.bone(name, parent, ob.translation, ob.rotation, ob.scale, ob.billboard)
    for mesh in om.meshes:
        shape = om.shapes[mesh.shapeIndex]
        inter = shape.attrs[0]
        n = len(inter.data) // inter.stride
        cols = {}
        for a in inter.attrs:
            fmt = f'<{a.components}{"f" if a.format == 0x1406 else "B"}'
            vals = [struct.unpack_from(fmt, inter.data, i * inter.stride + a.offset) for i in range(n)]
            cols[a.usage] = [[v / 255 for v in x] for x in vals] if a.format == 0x1401 else vals
        st = shape.primSets[0].prims[0].streams[0]
        idx = struct.unpack(f'<{len(st.data) // (1 if st.format == 0x1401 else 2)}{"B" if st.format == 0x1401 else "H"}', st.data)
        tris = [idx[i:i + 3] for i in range(0, len(idx), 3)]
        bone = b.bones[shape.primSets[0].bones[0]]
        new = b.mesh(bone, om.materials.items[mesh.materialIndex][0], cols[0], tris, cols.get(4), cols.get(1), cols.get(3), mesh.priority)
        if len(shape.attrs) == 1 and 3 not in cols: b.model.shapes[-1].attrs.pop()     # the pencils carry no colour at all
    anim = original.skeletalAnims.get('COMMON')
    for name, mem in anim.members:
        for f in cgfx.fields(mem):
            v = getattr(mem, f.name)
            if isinstance(v, cgfx.Obj) and v.kind == 'Curve': b.animate(bones[name], f.name, v.segments[0].keys)
    data = b.build()
    rebuilt = cgfx.read(data)
    out = []
    for slot in cgfx.SLOT_NAMES: differences(getattr(original, slot), getattr(rebuilt, slot), slot, out)
    print(f'{len(out)} values differ between the Activity Log banner and our rebuild of it')
    for line in out[:40]: print('  ' + line)
    same = data == open(SAMPLE, 'rb').read()
    if same: print('the two files are identical')
    elif not out: print(f'the files differ only in the bounding boxes ({len(data)} bytes against {os.path.getsize(SAMPLE)})')
    else: print('the files differ')
    return 1 if out else 0


if __name__ == '__main__':
    sys.exit(main())
