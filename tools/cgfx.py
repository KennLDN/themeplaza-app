#!/usr/bin/env python3
"""Reads and writes CGFX files: the 3D scene inside a HOME Menu banner.

Our own implementation of the file format, written from its public descriptions (3dbrew, GBATEK, the
public-domain SPICA library) and checked against working banners: every structure is described once, in
KINDS below, and both the reader and the writer work from that description. The check command reads a
file, confirms that every byte of it is accounted for, writes it again and compares the result.

  cgfx.py check FILE.cgfx...     reads each file; reports bytes not accounted for; writes it again twice
                                 (blocks at their original places, then laid out afresh) and compares
  cgfx.py info FILE.cgfx         describes the scene: bones, materials, meshes, lights, animations

All numbers little endian. A pointer holds the distance from its own position to its target; 0 = none.
"""
import struct, sys

I12 = (1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0)
SCALAR = {'u32': '<I', 's32': '<i', 'f32': '<f', 'u16': '<H', 's16': '<h', 'u8': '<B'}


class F:
    """One field of a structure: its name, type, the value a new object starts with, and (optionally) the
    condition for the field to exist at all."""
    def __init__(self, name, typ, default=None, cond=None):
        self.name, self.typ, self.default, self.cond = name, typ, default, cond


# Field types:
#   u32 s32 f32 u16 s16 u8     a number                T[n]       n numbers
#   magic                      4 characters            str        pointer to a text (None = no pointer)
#   *Kind                      pointer to a structure this one owns
#   ^                          pointer to a structure owned elsewhere (parent, owner, sibling)
#   [*Kind]                    count, pointer to a row of pointers        [u32] [f32]   count, pointer to numbers
#   (*Kind)                    count, then the pointers themselves
#   {Kind}                     count, pointer to a dictionary of named structures
#   data                       size, pointer to bytes kept in the file's second block (IMAG)
#   bytes                      size, pointer to bytes kept among the structures
#   =Kind  =Kind[n]            the structure itself, in place
#   slot:C:I                   animation value: None if flag bit I is set, a number if bit C is set, else a curve
HEAD = lambda typ, magic, rev: [F('type', 'u32', typ), F('magic', 'magic', magic), F('revision', 'u32', rev),
                                F('name', 'str', ''), F('userData', '{UserData}')]
NODE = [F('flags', 'u32', 1), F('branchVisible', 'u32', 1), F('childCount', 'u32', 0), F('children', 'u32', 0),
        F('animGroups', '{AnimGroup}'), F('scale', 'f32[3]', (1.0, 1.0, 1.0)), F('rotation', 'f32[3]', (0.0, 0.0, 0.0)),
        F('translation', 'f32[3]', (0.0, 0.0, 0.0)), F('local', 'f32[12]', I12), F('world', 'f32[12]', I12)]
MEMBER = lambda typ, obj: [F('type', 'u32', typ), F('path', 'str', ''), F('owner', 'str'), F('sub', 'str'),
                           F('valueOffset', 'u32', 0), F('valueSize', 'u32', 0), F('blendIndex', 'u32', 0),
                           F('objType', 'u32', obj), F('memberIndex', 'u32', 0), F('runtime', 'u32', 0)]
ANIM = [F('flags', 'u32', 0), F('path', 'str', ''), F('unk1', 'str'), F('unk2', 'str'), F('primType', 'u32', 0)]

KINDS = {
    'CMDL': HEAD(0x40000092, b'CMDL', 0x07000000) + NODE + [
        F('meshes', '[*Mesh]'), F('materials', '{MTOB}'), F('shapes', '[*Shape]'), F('meshNodes', '{MeshNode}'),
        F('visible', 'u32', 1), F('cullMode', 'u32', 0), F('layerId', 'u32', 0),
        F('skeleton', '*Skeleton', None, lambda o: o.type & 0x80)],
    'Mesh': HEAD(0x01000000, b'SOBJ', 0) + [
        F('shapeIndex', 'u32', 0), F('materialIndex', 'u32', 0), F('owner', '^'), F('visible', 'u8', 1),
        F('priority', 'u8', 0), F('meshNodeIndex', 's16', -1), F('runtime', 'u32[18]', (0,) * 18),
        F('meshNodeName', 'str', ''), F('tail', 'u32[3]', (0, 0, 0))],
    'Shape': HEAD(0x10000001, b'SOBJ', 0) + [
        F('flags', 'u32', 0), F('bbox', '*BBox'), F('positionOffset', 'f32[3]', (0.0, 0.0, 0.0)),
        F('primSets', '[*PrimSet]'), F('baseAddress', 'u32', 0), F('attrs', '[*VertexAttr]'), F('blendShape', 'u32', 0)],
    'BBox': [F('type', 'u32', 0x80000000), F('center', 'f32[3]', (0.0, 0.0, 0.0)),
             F('orientation', 'f32[9]', (1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0)), F('size', 'f32[3]', (1.0, 1.0, 1.0))],
    'PrimSet': [F('bones', '[u32]'), F('skinning', 'u32', 0), F('prims', '[*Primitive]')],
    'Primitive': [F('streams', '[*IndexStream]'), F('bufferObjs', '[u32]')],
    'IndexStream': [F('format', 'u32', 0x1401), F('mode', 'u8', 0), F('visible', 'u8', 1), F('pad', 'u16', 0),
                    F('data', 'data'), F('bufferObj', 'u32', 0), F('locFlag', 'u32', 0), F('cmdCache', 'u32', 0),
                    F('cmdCacheSize', 'u32', 0), F('locAddr', 'u32', 0), F('memArea', 'u32', 0), F('bbox', 'u32', 0)],
    'Interleaved': [F('type', 'u32', 0x40000002), F('usage', 'u32', 21), F('flags', 'u32', 2), F('bufferObj', 'u32', 0),
                    F('locFlag', 'u32', 0), F('data', 'data'), F('locAddr', 'u32', 0), F('memArea', 'u32', 0),
                    F('stride', 'u32', 0), F('attrs', '[*VertexAttr]')],
    'Attr': [F('type', 'u32', 0x40000001), F('usage', 'u32', 0), F('flags', 'u32', 0), F('bufferObj', 'u32', 0),
             F('locFlag', 'u32', 0), F('data', 'data'), F('locAddr', 'u32', 0), F('memArea', 'u32', 0),
             F('format', 'u32', 0x1406), F('components', 'u32', 3), F('scale', 'f32', 1.0), F('offset', 'u32', 0)],
    'FixedAttr': [F('type', 'u32', 0x80000000), F('usage', 'u32', 3), F('flags', 'u32', 1), F('format', 'u32', 0x1406),
                  F('components', 'u32', 4), F('scale', 'f32', 0.0), F('values', '[f32]')],
    'Skeleton': HEAD(0x02000000, b'SOBJ', 0) + [
        F('bones', '{Bone}'), F('rootBone', '^'), F('scalingRule', 'u32', 0), F('flags', 'u32', 2)],
    'Bone': [F('name', 'str', ''), F('flags', 'u32', 0x1DC), F('index', 'u32', 0), F('parentIndex', 's32', -1),
             F('parent', '^'), F('child', '^'), F('prev', '^'), F('next', '^'),
             F('scale', 'f32[3]', (1.0, 1.0, 1.0)), F('rotation', 'f32[3]', (0.0, 0.0, 0.0)),
             F('translation', 'f32[3]', (0.0, 0.0, 0.0)), F('local', 'f32[12]', I12), F('world', 'f32[12]', I12),
             F('invBase', 'f32[12]', I12), F('billboard', 'u32', 0), F('userData', '{UserData}')],
    'MTOB': HEAD(0x08000000, b'MTOB', 0x06000000) + [
        F('flags', 'u32', 0), F('texCoordConfig', 'u32', 0), F('renderLayer', 'u32', 0),
        F('emission', 'f32[4]', (0.0, 0.0, 0.0, 0.0)), F('ambient', 'f32[4]', (1.0, 1.0, 1.0, 1.0)),
        F('diffuse', 'f32[4]', (1.0, 1.0, 1.0, 1.0)), F('specular0', 'f32[4]', (1.0, 1.0, 1.0, 0.0)),
        F('specular1', 'f32[4]', (0.0, 0.0, 0.0, 0.0)), F('constant', 'f32[24]', (0.0, 0.0, 0.0, 1.0) * 6),
        F('colorBytes', 'u8[44]', None), F('commandCache', 'u32', 0),
        F('rasterFlags', 'u32', 0), F('cullMode', 'u32', 1), F('polygonOffset', 'f32', 0.0), F('rasterCmd', 'u32[2]', (2, 0x00010040)),
        F('depthFlags', 'u32', 3), F('depthCmd', 'u32[4]', (0x41, 0x00010107, 0x03000000, 0x00080126)),
        F('blendMode', 'u32', 0), F('blendColor', 'f32[4]', (0.0, 0.0, 0.0, 1.0)),
        F('blendCmd', 'u32[6]', (0x00E40100, 0x803F0100, 0x01010000, 0, 0xFF000000, 0)),
        F('stencilCmd', 'u32[4]', (0, 0x000D0105, 0, 0x000F0106)),
        F('usedTexCoords', 'u32', 0), F('texCoords', '=TexCoord[3]'),
        F('mapper0', '*TexMapper'), F('mapper1', '*TexMapper'), F('mapper2', '*TexMapper'), F('procMapper', 'u32', 0),
        F('shader', '*ShaderRef'), F('fragShader', '*FragShader'),
        F('shaderProgram', 'u32', 0), F('shaderParamCount', 'u32', 0), F('shaderParams', 'u32', 0),
        F('lightSet', 'u32', 0), F('fog', 'u32', 0), F('hashes', 'u32[13]', (0,) * 13), F('materialId', 'u32', 0)],
    'TexCoord': [F('source', 'u32', 0), F('mapping', 'u32', 0), F('refCamera', 's32', 0), F('matrixMode', 'u32', 0),
                 F('scale', 'f32[2]', (0.0, 0.0)), F('rotation', 'f32', 0.0), F('translation', 'f32[2]', (0.0, 0.0)),
                 F('flags', 'u32', 0), F('matrix', 'f32[12]', I12)],
    'TexMapper': [F('type', 'u32', 0x80000000), F('dynAlloc', 'u32', 0), F('texture', '*Texture'), F('sampler', '*Sampler'),
                  F('cmd', 'u32[14]', (0, 0x0001008E, 0xFF000000, 0x809F0081, 0, 0x2206) + (0,) * 8), F('cmdSize', 'u32', 0x38)],
    'TexRef': HEAD(0x20000004, b'TXOB', 0x05000000) + [F('link', 'str', ''), F('linkPtr', 'u32', 0)],
    'Sampler': [F('type', 'u32', 0x80000000), F('owner', '^'), F('minFilter', 'u32', 1),
                F('border', 'f32[4]', (0.0, 0.0, 0.0, 1.0)), F('lodBias', 'f32', 0.0)],
    'ShaderRef': HEAD(0x80000001, b'SHDR', 0x05000000) + [F('path', 'str', 'DefaultShader'), F('ptr', 'u32', 0)],
    'FragShader': [F('bufferColor', 'f32[4]', (0.0, 0.0, 0.0, 1.0)), F('lightFlags', 'u32', 0), F('layerConfig', 'u32', 0),
                   F('fresnel', 'u32', 0), F('bumpTexture', 'u32', 0), F('bumpMode', 'u32', 0), F('bumpRenorm', 'u32', 0),
                   F('lutTable', '*LutTable'), F('stages', '=TexEnv[6]'),
                   F('alphaTest', 'u32[2]', (0x10, 0x000F0104)),
                   F('bufferCmd', 'u32[6]', (0xFF000000, 0x000F00FD, 0, 0x000200E0, 0x400, 0x000201C3))],
    'TexEnv': [F('constant', 'u32', 0), F('source', 'u32', 0x0E1F0E1F), F('header', 'u32', 0), F('operand', 'u32', 0),
               F('combine', 'u32', 0), F('color', 'u32', 0xFF000000), F('scale', 'u32', 0)],
    'LutTable': [F('reflectR', '*LutSampler'), F('reflectG', '*LutSampler'), F('reflectB', '*LutSampler'),
                 F('dist0', '*LutSampler'), F('dist1', '*LutSampler'), F('fresnel', '*LutSampler')],
    'LutSampler': [F('input', 'u32', 0), F('scale', 'u32', 0), F('sampler', '*LutRef')],
    'LutRef': [F('type', 'u32', 0x40000000), F('path', 'str', ''), F('table', 'str', ''), F('runtime', 'u32', 0)],
    'TexImage': HEAD(0x20000011, b'TXOB', 0x05000000) + [
        F('height', 'u32', 0), F('width', 'u32', 0), F('glFormat', 'u32', 0), F('glType', 'u32', 0), F('mipLevels', 'u32', 1),
        F('texObj', 'u32', 0), F('locFlag', 'u32', 0), F('format', 'u32', 4), F('image', '*Image')],
    'Image': [F('height', 'u32', 0), F('width', 'u32', 0), F('data', 'data'), F('dynAlloc', 'u32', 0), F('bpp', 'u32', 16),
              F('locAddr', 'u32', 0), F('memArea', 'u32', 0)],
    'AnimGroup': [F('type', 'u32', 0x80000000), F('flags', 'u32', 0), F('name', 'str', ''), F('memberType', 'u32', 0),
                  F('members', '{Member}'), F('blendOps', '[u32]'), F('evalTiming', 'u32', 0)],
    'MemberBone': MEMBER(0x40000000, 0) + [F('bone', 'str', ''), F('objType2', 'u32', 0)],
    'MemberMatColor': MEMBER(0x08000000, 1) + [F('material', 'str', ''), F('objType2', 'u32', 1)],
    'MemberSampler': MEMBER(0x02000000, 2) + [F('material', 'str', ''), F('index', 'u32', 0), F('objType2', 'u32', 2)],
    'MemberMapper': MEMBER(0x20000000, 3) + [F('material', 'str', ''), F('index', 'u32', 0), F('objType2', 'u32', 3)],
    'MemberBlendOp': MEMBER(0x04000000, 4) + [F('material', 'str', ''), F('objType2', 'u32', 4)],
    'MemberCoord': MEMBER(0x80000000, 5) + [F('material', 'str', ''), F('index', 'u32', 0), F('objType2', 'u32', 5)],
    'MemberModel': MEMBER(0x10000000, 6) + [F('objType2', 'u32', 6)],
    'MemberMesh': MEMBER(0x01000000, 7) + [F('index', 'u32', 0), F('objType2', 'u32', 7)],
    'MemberTransform': MEMBER(0x00800000, 9) + [F('objType2', 'u32', 9)],      # a light's or camera's placement
    'MemberOther': MEMBER(0x00100000, 13),                                     # a light's other values
    'CANM': [F('magic', 'magic', b'CANM'), F('revision', 'u32', 0x05000000), F('name', 'str', 'COMMON'),
             F('targetGroup', 'str', 'SkeletalAnimation'), F('loopMode', 'u32', 1), F('frames', 'f32', 0.0),
             F('members', '{CanmMember}'), F('userData', '{UserData}')],
    'AnimTransform': ANIM + [F('sx', 'slot:6:16'), F('sy', 'slot:7:17'), F('sz', 'slot:8:18'),
                             F('rx', 'slot:9:19'), F('ry', 'slot:10:20'), F('rz', 'slot:11:21'), F('pad', 'u32', 0),
                             F('tx', 'slot:13:23'), F('ty', 'slot:14:24'), F('tz', 'slot:15:25')],
    'AnimFloat': ANIM + [F('value', 'slot:0:1')],
    'AnimVec2': ANIM + [F('x', 'slot:0:2'), F('y', 'slot:1:3')],
    'AnimVec3': ANIM + [F('x', 'slot:0:3'), F('y', 'slot:1:4'), F('z', 'slot:2:5')],
    'AnimRgba': ANIM + [F('r', 'slot:0:4'), F('g', 'slot:1:5'), F('b', 'slot:2:6'), F('a', 'slot:3:7')],
    'AnimTexture': ANIM + [F('index', 'slot:0:1'), F('textures', '[*Texture]')],
    'AnimBool': ANIM + [F('curve', '*BoolCurve')],
    'LUTS': HEAD(0x04000000, b'LUTS', 0x04000000) + [F('tables', '{Lut}')],
    'Lut': [F('type', 'u32', 0x80000000), F('name', 'str', ''), F('absolute', 'u32', 1), F('commands', 'bytes')],
    'FragLight': HEAD(0x400000A2, b'CFLT', 0x06000000) + NODE + [
        F('enabled', 'u32', 1), F('lightType', 'u32', 0), F('ambient', 'f32[4]', (0.0, 0.0, 0.0, 1.0)),
        F('diffuse', 'f32[4]', (1.0, 1.0, 1.0, 1.0)), F('specular0', 'f32[4]', (1.0, 1.0, 1.0, 1.0)),
        F('specular1', 'f32[4]', (1.0, 1.0, 1.0, 1.0)), F('colorBytes', 'u8[16]', None),
        F('direction', 'f32[3]', (0.0, 0.0, -1.0)), F('distanceLut', '*LutRef'), F('angleLut', '*LutSampler'),
        F('attenuationStart', 'f32', 0.0), F('attenuationEnd', 'f32', 0.0), F('attenuationScale', 'u32', 0),
        F('attenuationBias', 'u32', 0), F('lightFlags', 'u32', 0)],
    'AmbientLight': HEAD(0x40000422, b'CFLT', 0x06000000) + NODE + [
        F('enabled', 'u32', 1), F('color', 'f32[4]', (0.0, 0.0, 0.0, 1.0)), F('colorBytes', 'u8[4]', None), F('dirty', 'u32', 0)],
    'Scene': HEAD(0x00800000, b'CENV', 0x06000000) + [
        F('cameras', '[*SceneRef]'), F('lightSets', '[*LightSet]'), F('fogs', '[*SceneRef]')],
    'SceneRef': [F('id', 'u32', 0), F('name', 'str', ''), F('runtime', 'u32', 0)],
    'LightSet': [F('id', 'u32', 0), F('lights', '[*SceneRef]')],
    'Curve': [F('start', 'f32', 0.0), F('end', 'f32', 0.0), F('preRepeat', 'u8', 0), F('postRepeat', 'u8', 0),
              F('pad', 'u16', 0), F('flags', 'u32', 0), F('segments', '(*Segment)')],
}
# the kind of structure in each of the file's 15 dictionaries
SLOTS = ['CMDL', 'Texture', 'LUTS', 'MTOB', 'Shader', 'Camera', 'Light', 'Fog', 'Scene', 'CANM', 'CANM', 'CANM', 'CANM',
         'CANM', 'Emitter']
SLOT_NAMES = ['models', 'textures', 'luts', 'materials', 'shaders', 'cameras', 'lights', 'fogs', 'scenes',
              'skeletalAnims', 'materialAnims', 'visibilityAnims', 'cameraAnims', 'lightAnims', 'emitters']
MEMBERS = {0x40000000: 'MemberBone', 0x08000000: 'MemberMatColor', 0x02000000: 'MemberSampler', 0x20000000: 'MemberMapper',
           0x04000000: 'MemberBlendOp', 0x80000000: 'MemberCoord', 0x10000000: 'MemberModel', 0x01000000: 'MemberMesh',
           0x00800000: 'MemberTransform'}


def family(kind, word, word4):
    """Some pointers lead to one of several structures; the structure itself says which (word = its first
    number, word4 = its fifth)."""
    if kind == 'VertexAttr': return {0x40000002: 'Interleaved', 0x40000001: 'Attr', 0x80000000: 'FixedAttr'}[word]
    if kind == 'Texture': return {0x20000011: 'TexImage', 0x20000004: 'TexRef'}[word]
    if kind == 'Member': return MEMBERS.get(word, 'MemberOther')
    if kind == 'CanmMember':
        return {0: 'AnimFloat', 2: 'AnimBool', 3: 'AnimVec2', 4: 'AnimVec3', 5: 'AnimTransform', 6: 'AnimRgba', 7: 'AnimTexture'}[word4]
    if kind == 'Light': return {0x400000A2: 'FragLight', 0x40000422: 'AmbientLight'}[word]
    return kind


class Obj:
    """One structure. Its fields are attributes; hint remembers where the file it was read from kept it."""
    def __init__(self, kind, **values):
        self.kind, self.hint = kind, {}
        for f in KINDS[kind]:
            setattr(self, f.name, initial(f))
        for k, v in values.items():
            if not hasattr(self, k): raise AttributeError(f'{kind} has no field {k}')
            setattr(self, k, v)

    def __repr__(self):
        return f'<{self.kind} {getattr(self, "name", "") or ""}>'


class Dict:
    """A dictionary of named structures, in file order."""
    def __init__(self, items=()):
        self.items, self.hint, self.name_at = list(items), None, []

    def __len__(self): return len(self.items)
    def __iter__(self): return iter(self.items)
    def add(self, name, obj): self.items.append((name, obj)); return obj
    def get(self, name):
        for n, o in self.items:
            if n == name: return o
    def index(self, name): return [n for n, _ in self.items].index(name)


class Segment:
    """One stretch of an animation curve: either a single value or keys.
    quant 0: keys (frame, value, slope in, slope out); 3: (frame, value, slope); 6: (frame, value).
    Other quantisations keep their packed keys as bytes, after scales (value scale, value offset, and for all
    but quantisation 4 a frame scale; only 4 has been seen in a real file)."""
    KEY = {0: 4, 3: 3, 6: 2}
    PACKED = {1: 8, 2: 6, 4: 6, 5: 4, 7: 4}

    def __init__(self, start=0.0, end=0.0, interpolation=1, quant=6, keys=None, single=None):
        self.kind, self.hint = 'Segment', {}
        self.start, self.end, self.interpolation, self.quant, self.keys, self.single = start, end, interpolation, quant, keys or [], single
        self.speed, self.scales, self.extraFlags = None, None, 0

    @staticmethod
    def read(b, off):
        s = Segment()
        s.start, s.end, flags = struct.unpack_from('<ffI', b, off)
        s.interpolation, s.quant, s.extraFlags = flags >> 2 & 7, flags >> 5 & 7, flags & ~0xFD
        if flags & 1:
            s.single = struct.unpack_from('<f', b, off + 12)[0]
            return s, 16
        n, s.speed = struct.unpack_from('<If', b, off + 12)
        pos = off + 20
        if s.quant in Segment.PACKED:
            ns = 2 if s.quant == 4 else 3
            s.scales = struct.unpack_from(f'<{ns}f', b, pos); pos += 4 * ns
            size = Segment.PACKED[s.quant]
            s.keys = [bytes(b[pos + i * size:pos + (i + 1) * size]) for i in range(n)]
            pos += -(-n * size // 4) * 4
        else:
            k = Segment.KEY[s.quant]
            s.keys = [struct.unpack_from(f'<{k}f', b, pos + i * k * 4) for i in range(n)]
            pos += n * k * 4
        return s, pos - off

    def pack(self):
        flags = self.extraFlags | self.interpolation << 2 | self.quant << 5
        if self.single is not None:
            return struct.pack('<ffIf', self.start, self.end, flags | 1, self.single)
        # speed: the reciprocal of the segment's length, which the console uses to guess a key's position
        speed = self.speed if self.speed is not None else 1.0 / (self.end - self.start)
        out = struct.pack('<ffIIf', self.start, self.end, flags, len(self.keys), speed)
        if self.quant in Segment.PACKED:
            out += struct.pack(f'<{len(self.scales)}f', *self.scales) + b''.join(self.keys)
            out += b'\0' * (-len(out) % 4)
        else:
            out += b''.join(struct.pack(f'<{len(k)}f', *k) for k in self.keys)
        return out


class BoolCurve:
    """An on/off animation: one bit per frame, after a first bit that says whether the value never changes."""
    def __init__(self, start=0.0, end=0.0, bits=b''):
        self.kind, self.hint = 'BoolCurve', {}
        self.start, self.end, self.preRepeat, self.postRepeat, self.bits = start, end, 0, 0, bits

    @staticmethod
    def read(b, off):
        c = BoolCurve()
        c.start, c.end, c.preRepeat, c.postRepeat, pad, ptr = struct.unpack_from('<ffBBHI', b, off)
        if ptr != 4: raise ValueError(f'on/off curve at 0x{off:x} keeps its bits elsewhere')
        n = -(-(int(c.end - c.start) + 1) // 32) * 4
        c.bits = bytes(b[off + 16:off + 16 + n])
        return c, 16 + n

    def pack(self):
        return struct.pack('<ffBBHI', self.start, self.end, self.preRepeat, self.postRepeat, 0, 4) + self.bits


LEAVES = {'Segment': Segment, 'BoolCurve': BoolCurve}


def initial(f):
    t = f.typ
    if t[0] == '{': return Dict()
    if t[0] in '[(': return []
    if t[0] == '=':
        kind, n = (t[1:].split('[') + [''])[:2]
        return [Obj(kind) for _ in range(int(n[:-1]))] if n else Obj(kind)
    return f.default


def fields(o):
    return [f for f in KINDS[o.kind] if not f.cond or f.cond(o)]


def field_size(f):
    t = f.typ
    if t in SCALAR: return struct.calcsize(SCALAR[t])
    if t in ('magic', 'str', '^') or t[0] in '*(' or t.startswith('slot'): return 4
    if t[0] in '[{' or t in ('data', 'bytes'): return 8
    if t[0] == '=':
        kind, n = (t[1:].split('[') + [''])[:2]
        return sum(field_size(g) for g in KINDS[kind]) * (int(n[:-1]) if n else 1)
    if '[' in t:
        base, n = t.split('['); return struct.calcsize(SCALAR[base]) * int(n[:-1])
    raise ValueError(t)


def size_of(o):
    if o.kind in LEAVES: return len(o.pack())
    return sum(field_size(f) + (4 * len(getattr(o, f.name)) if f.typ[0] == '(' else 0) for f in fields(o))


# ---------------------------------------------------------------- dictionary search tree

def tree(names):
    """The search tree stored with a dictionary: for the root and each name, (bit to test, left, right).
    A lookup starts at the root's left and, at each node, goes right if the name has that bit set, until
    the bit numbers stop decreasing; the node reached is the only candidate."""
    keys = [n.encode() for n in names]
    width = max([len(k) for k in keys] + [0])
    bit = lambda k, b: (k[b >> 3] >> (b & 7)) & 1 if (b >> 3) < len(k) else 0
    nodes = [[0xFFFFFFFF, 0, 0, b'']]

    def walk(key, stop=-1):
        parent, cur = 0, nodes[0][1]
        while nodes[parent][0] > nodes[cur][0] and nodes[cur][0] > stop:
            parent, cur = cur, nodes[cur][2 if bit(key, nodes[cur][0]) else 1]
        return cur, parent

    for i, key in enumerate(keys):
        nodes.append([0, 0, 0, key])
    for i in sorted(range(len(keys)), key=lambda k: -len(keys[k])):   # longest names first
        key, idx = keys[i], i + 1
        near, _ = walk(key)
        b = width * 8 - 1
        while bit(nodes[near][3], b) == bit(key, b):
            b -= 1
            if b < 0: raise ValueError(f'the name {names[i]!r} is in the dictionary twice')
        child, parent = walk(key, b)
        nodes[idx][0] = b
        nodes[idx][1], nodes[idx][2] = (child, idx) if bit(key, b) else (idx, child)
        if parent == 0: nodes[0][1] = idx
        else: nodes[parent][2 if bit(key, nodes[parent][0]) else 1] = idx
    return [(n[0], n[1], n[2]) for n in nodes]


# ---------------------------------------------------------------- reading

class Scene:
    """A whole file: one Dict per slot (models, textures, ...)."""
    def __init__(self):
        for n in SLOT_NAMES: setattr(self, n, Dict())
        self.hint = {}


class Reader:
    def __init__(self, b):
        self.b, self.objs, self.claims, self.refs, self.notes, self.texts = b, {}, [], [], [], {}

    def u32(self, o): return struct.unpack_from('<I', self.b, o)[0]
    def s32(self, o): return struct.unpack_from('<i', self.b, o)[0]

    def claim(self, start, end, what):
        self.claims.append((start, end, what))

    def text(self, pos):
        p = self.u32(pos)
        if not p: return None, None
        at = pos + p; end = self.b.index(b'\0', at)
        self.texts[at] = self.b[at:end].decode('ascii')
        return self.texts[at], at

    def dictionary(self, kind, pos):
        d = Dict()
        n, p = self.u32(pos), self.u32(pos + 4)
        if not p:
            if n: raise ValueError(f'dictionary of {n} without a table at 0x{pos:x}')
            return d
        at = pos + 4 + p
        if self.b[at:at + 4] != b'DICT': raise ValueError(f'no DICT at 0x{at:x}')
        size, count = self.u32(at + 4), self.u32(at + 8)
        if count != n or size != 12 + 16 * (n + 1): raise ValueError(f'odd DICT at 0x{at:x}')
        d.hint = at
        self.claim(at, at + size, 'DICT')
        stored = []
        for i in range(n + 1):
            e = at + 12 + 16 * i
            stored.append((self.u32(e), *struct.unpack_from('<HH', self.b, e + 4)))
            if i:
                name, name_at = self.text(e + 8)
                d.name_at.append(name_at)
                d.add(name, self.obj(kind, e + 12 + self.s32(e + 12)))
        if tree([nm for nm, _ in d.items]) != stored:
            self.notes.append(f'DICT at 0x{at:x}: its search tree is not the one we would build')
        return d

    def obj(self, kind, off):
        kind = family(kind, self.u32(off), self.u32(off + 16) if off + 20 <= len(self.b) else 0)
        if off in self.objs:
            if self.objs[off].kind != kind: raise ValueError(f'0x{off:x} is read as {self.objs[off].kind} and as {kind}')
            return self.objs[off]
        if kind in LEAVES:
            o, size = LEAVES[kind].read(self.b, off)
            o.hint[''] = off; self.objs[off] = o
            self.claim(off, off + size, kind)
            return o
        if kind not in KINDS: raise ValueError(f'no description of {kind} (at 0x{off:x})')
        o = Obj.__new__(Obj); o.kind, o.hint = kind, {'': off}
        self.objs[off] = o
        end = self.fields(o, KINDS[kind], off)
        self.claim(off, end, kind)
        return o

    def fields(self, o, flist, pos):
        b = self.b
        for f in flist:
            t = f.typ
            if f.cond and not f.cond(o): setattr(o, f.name, None); continue
            if t in SCALAR:
                v = struct.unpack_from(SCALAR[t], b, pos)[0]
            elif t == 'magic':
                v = bytes(b[pos:pos + 4])
                if v != f.default: raise ValueError(f'{o.kind} at 0x{o.hint[""]:x}: magic {v} instead of {f.default}')
            elif t == 'str':
                v, o.hint['s:' + f.name] = self.text(pos)
            elif t == '^':
                v = None
                if self.s32(pos): self.refs.append((o, f.name, pos + self.s32(pos)))
            elif t[0] == '*':
                v = self.obj(t[1:], pos + self.s32(pos)) if self.u32(pos) else None
            elif t.startswith('slot'):
                const, ignore = [int(x) for x in t.split(':')[1:]]
                if o.flags >> ignore & 1: v = None
                elif o.flags >> const & 1: v = struct.unpack_from('<f', b, pos)[0]
                else: v = self.obj('Curve', pos + self.s32(pos))
            elif t[0] == '(':
                n = self.u32(pos)
                v = [self.obj(t[2:-1], pos + 4 + 4 * i + self.s32(pos + 4 + 4 * i)) for i in range(n)]
                pos += 4 * n
            elif t[0] == '[' :
                n, p = self.u32(pos), self.u32(pos + 4)
                at = pos + 4 + p if p else None
                o.hint['a:' + f.name] = at
                if n and not p: raise ValueError(f'{o.kind}.{f.name}: {n} entries without a table')
                if t[1] == '*': v = [self.obj(t[2:-1], at + 4 * i + self.s32(at + 4 * i)) for i in range(n)]
                else: v = list(struct.unpack_from(f'<{n}{SCALAR[t[1:-1]][1]}', b, at)) if n else []
                if n: self.claim(at, at + 4 * n, 'row')
            elif t[0] == '{':
                v = self.dictionary(t[1:-1], pos)
            elif t in ('data', 'bytes'):
                n, p = self.u32(pos), self.u32(pos + 4)
                v = bytes(b[pos + 4 + p:pos + 4 + p + n]) if p else None
                o.hint['d:' + f.name] = pos + 4 + p if p else None
                if p: self.claim(pos + 4 + p, pos + 4 + p + n, 'data')
            elif t[0] == '=':
                kind, n = (t[1:].split('[') + [''])[:2]
                v = []
                for _ in range(int(n[:-1]) if n else 1):
                    sub = Obj.__new__(Obj); sub.kind, sub.hint = kind, {}
                    self.fields(sub, KINDS[kind], pos)
                    pos += sum(field_size(g) for g in KINDS[kind])
                    v.append(sub)
                if not n: v = v[0]
                setattr(o, f.name, v); continue
            else:
                base, n = t.split('[')
                v = struct.unpack_from(f'<{n[:-1]}{SCALAR[base][1]}', b, pos)
            setattr(o, f.name, v)
            pos += field_size(f)
        return pos


def read(b):
    """Reads a file into a Scene. scene.unread lists stretches of the first block that nothing pointed to,
    scene.notes anything else that looked odd."""
    magic, bom, hsize, rev, fsize, blocks = struct.unpack_from('<4sHHIII', b, 0)
    if magic != b'CGFX' or bom != 0xFEFF: raise ValueError('not a CGFX file')
    if b[0x14:0x18] != b'DATA': raise ValueError('no DATA block')
    r = Reader(b)
    scene = Scene()
    dsize = r.u32(0x18)
    first = min([0x1C + 8 * i + 4 + r.u32(0x1C + 8 * i + 4) for i in range(16) if 0x1C + 8 * i + 8 <= len(b) and r.u32(0x1C + 8 * i + 4)
                 and b[0x1C + 8 * i + 4 + r.u32(0x1C + 8 * i + 4):][:4] == b'DICT'] + [0x1C + 16 * 8])
    nslots = (first - 0x1C) // 8
    scene.hint = {'revision': rev, 'slots': nslots, 'dataSize': dsize, 'fileSize': fsize, 'blocks': blocks}
    r.claim(0, 0x1C + 8 * nslots, 'header')
    for i in range(nslots):
        kind = SLOTS[i]
        if r.u32(0x1C + 8 * i) and kind not in KINDS and kind not in ('Texture', 'Light'):
            r.notes.append(f'{SLOT_NAMES[i]}: {r.u32(0x1C + 8 * i)} entries not read (no description of {kind})')
            continue
        setattr(scene, SLOT_NAMES[i], r.dictionary(kind, 0x1C + 8 * i))
    for o, name, target in r.refs:
        if target not in r.objs: raise ValueError(f'{o.kind}.{name} points at 0x{target:x}, where nothing was read')
        setattr(o, name, r.objs[target])
    end = 0x14 + dsize
    scene.hint['imag'] = end if blocks > 1 else None
    if blocks > 1 and b[end:end + 4] != b'IMAG': raise ValueError('no IMAG block after DATA')
    scene.hint['texts'] = r.texts
    covered = bytearray(len(b))
    for s, e, what in r.claims:
        for i in range(s, e): covered[i] += 1
    for s, t in r.texts.items():
        for i in range(s, s + len(t) + 1): covered[i] += 1
    scene.unread, i = [], 0
    while i < end:
        if not covered[i] and any(b[i:min(end, (i | 3) + 1)]):
            j = i
            while j < end and not covered[j]: j += 1
            scene.unread.append((i, j)); i = j
        else: i += 1
    scene.notes = r.notes
    once = bytearray(len(b))
    scene.overlaps = []
    for s, e, what in sorted(r.claims):
        if any(once[s:e]) and e <= end: scene.overlaps.append((s, e, what))
        for i in range(s, e): once[i] = 1
    scene.objs = r.objs
    return scene



# ---------------------------------------------------------------- writing

class Writer:
    """Lays a Scene out and produces the file. With exact=True every block goes where the file the scene
    was read from had it (to prove that reading and writing lose nothing); otherwise the blocks are laid
    out afresh, in the order the working banners use."""
    def __init__(self, scene, exact=False):
        self.scene, self.exact = scene, exact
        self.pos = 0x1C + 8 * scene.hint.get('slots', 15)
        self.placed, self.order = set(), []          # structures placed; (offset, kind of block, owner, field) in order
        self.texts, self.blobs = {}, []              # text -> offset; (owner, field) of data kept in IMAG

    def take(self, size, hint, align=4):
        if self.exact:
            if hint is None: raise ValueError('exact layout needs a scene read from a file')
            return hint
        self.pos = -(-self.pos // align) * align
        at = self.pos; self.pos += size
        return at

    # -- pass 1: decide where everything goes
    def place(self, o):
        if o is None or id(o) in self.placed: return
        self.placed.add(id(o))
        o.off = self.take(size_of(o), o.hint.get(''), 8 if o.kind == 'Mesh' else 4)
        self.order.append((o.off, 'obj', o, None))
        if o.kind in LEAVES: return
        if o.kind == 'CMDL' and not self.exact: self.pos = -(-self.pos // 8) * 8
        self.rows(o, fields(o))
        self.dicts(o, fields(o))
        self.children(o, fields(o))

    def dicts(self, o, flist):
        """...then the tables of its dictionaries, before anything those hold."""
        for f in flist:
            v = getattr(o, f.name)
            if f.typ[0] == '{' and len(v):
                v.off = self.take(12 + 16 * (len(v) + 1), v.hint)
                self.order.append((v.off, 'dict', v, f.typ[1:-1]))
            elif f.typ[0] == '=':
                for sub in (v if isinstance(v, list) else [v]): self.dicts(sub, KINDS[sub.kind])

    def rows(self, o, flist):
        """The rows of pointers and numbers a structure's lists point to come straight after it."""
        for f in flist:
            t, v = f.typ, getattr(o, f.name)
            if t[0] == '[' and v:
                o.hint['A:' + f.name] = self.take(4 * len(v), o.hint.get('a:' + f.name))
            elif t == 'bytes' and v is not None:
                o.hint['D:' + f.name] = self.take(len(v), o.hint.get('d:' + f.name))
            elif t[0] == '=':
                for sub in (v if isinstance(v, list) else [v]): self.rows(sub, KINDS[sub.kind])

    def children(self, o, flist):
        for f in flist:
            t, v = f.typ, getattr(o, f.name)
            if t[0] == '*' or t.startswith('slot'):
                if isinstance(v, (Obj, Segment, BoolCurve)): self.place(v)
            elif t[0] == '(' or t[:2] == '[*':
                for c in v: self.place(c)
            elif t[0] == '{':
                for _, c in v: self.place(c)
            elif t == 'data' and v is not None:
                self.blobs.append((o, f.name))
            elif t[0] == '=':
                for sub in (v if isinstance(v, list) else [v]): self.children(sub, KINDS[sub.kind])

    # -- pass 2: produce the bytes
    def text(self, o, name, v, at):
        if v is None: return 0
        if self.exact: return o.hint['s:' + name] - at
        return self.texts[v] - at

    def collect_texts(self, o, flist, out):
        for f in flist:
            v = getattr(o, f.name)
            if f.typ == 'str' and v is not None and v not in out: out[v] = None
            elif f.typ[0] == '=':
                for sub in (v if isinstance(v, list) else [v]): self.collect_texts(sub, KINDS[sub.kind], out)

    def emit(self, out, o, flist, pos):
        for f in flist:
            t, v = f.typ, getattr(o, f.name)
            if t in SCALAR:
                struct.pack_into(SCALAR[t], out, pos, v)
            elif t == 'magic':
                out[pos:pos + 4] = v
            elif t == 'str':
                struct.pack_into('<i', out, pos, self.text(o, f.name, v, pos))
            elif t == '^' or t[0] == '*':
                struct.pack_into('<i', out, pos, v.off - pos if v is not None else 0)
            elif t.startswith('slot'):
                if v is None: struct.pack_into('<I', out, pos, 0)
                elif isinstance(v, float): struct.pack_into('<f', out, pos, v)
                else: struct.pack_into('<i', out, pos, v.off - pos)
            elif t[0] == '(':
                struct.pack_into('<I', out, pos, len(v))
                for i, c in enumerate(v): struct.pack_into('<i', out, pos + 4 + 4 * i, c.off - (pos + 4 + 4 * i))
                pos += 4 * len(v)
            elif t[0] == '[':
                at = o.hint.get('A:' + f.name) if v else None
                struct.pack_into('<Ii', out, pos, len(v), at - (pos + 4) if v else 0)
                for i, c in enumerate(v):
                    if t[1] == '*': struct.pack_into('<i', out, at + 4 * i, c.off - (at + 4 * i))
                    else: struct.pack_into(SCALAR[t[1:-1]], out, at + 4 * i, c)
            elif t[0] == '{':
                struct.pack_into('<Ii', out, pos, len(v), v.off - (pos + 4) if len(v) else 0)
            elif t == 'bytes':
                at = o.hint.get('D:' + f.name)
                struct.pack_into('<Ii', out, pos, len(v) if v is not None else 0, at - (pos + 4) if v is not None else 0)
                if v is not None: out[at:at + len(v)] = v
            elif t == 'data':
                at = o.hint.get('D:' + f.name)
                struct.pack_into('<Ii', out, pos, len(v) if v is not None else 0, at - (pos + 4) if v is not None else 0)
                if v is not None: out[at:at + len(v)] = v
            elif t[0] == '=':
                for sub in (v if isinstance(v, list) else [v]):
                    self.emit(out, sub, KINDS[sub.kind], pos)
                    pos += sum(field_size(g) for g in KINDS[sub.kind])
                continue
            else:
                base, n = t.split('[')
                struct.pack_into(f'<{n[:-1]}{SCALAR[base][1]}', out, pos, *v)
            pos += field_size(f)

    def build(self):
        sc = self.scene
        nslots = sc.hint.get('slots', 15)
        dicts = [getattr(sc, n) for n in SLOT_NAMES[:nslots]]
        # the file's own dictionaries come first, then the contents of each
        for d, kind in zip(dicts, SLOTS):
            if len(d):
                d.off = self.take(12 + 16 * (len(d) + 1), d.hint)
                self.order.append((d.off, 'dict', d, kind))
        for d in dicts:
            for _, o in d: self.place(o)
        # texts, each kept once
        if self.exact:
            data_end = 0x14 + sc.hint['dataSize']
        else:
            # each text once, in the order the blocks first mention it
            for _, what, o, _ in self.order:
                if what == 'dict':
                    for name, _ in o: self.texts.setdefault(name, None)
                elif o.kind not in LEAVES: self.collect_texts(o, fields(o), self.texts)
            for t in self.texts:
                self.texts[t] = self.pos; self.pos += len(t) + 1
            # IMAG's contents start on a multiple of 0x80... its 8-byte header sits just before
            data_end = -(-(self.pos + 8) // 0x80) * 0x80 - 8 if self.blobs else -(-self.pos // 4) * 4
        size = data_end
        if self.blobs:
            pos = data_end + 8
            for o, name in self.blobs:
                v = getattr(o, name)
                if self.exact: at = o.hint['d:' + name]
                else:
                    pos = -(-pos // (0x80 if o.kind == 'Image' else 4)) * (0x80 if o.kind == 'Image' else 4)
                    at = pos; pos += len(v)
                o.hint['D:' + name] = at
                size = max(size, at + len(v))
            size = sc.hint['fileSize'] if self.exact else -(-pos // 4) * 4
        out = bytearray(size)
        struct.pack_into('<4sHHIII', out, 0, b'CGFX', 0xFEFF, 0x14, sc.hint.get('revision', 0x05000000), size, 2 if self.blobs else 1)
        struct.pack_into('<4sI', out, 0x14, b'DATA', data_end - 0x14)
        for i, d in enumerate(dicts):
            struct.pack_into('<Ii', out, 0x1C + 8 * i, len(d), d.off - (0x20 + 8 * i) if len(d) else 0)
        for at, what, o, kind in self.order:
            if what == 'dict':
                names = [n for n, _ in o]
                struct.pack_into('<4sII', out, at, b'DICT', 12 + 16 * (len(o) + 1), len(o))
                for i, (bit, left, right) in enumerate(tree(names)):
                    e = at + 12 + 16 * i
                    struct.pack_into('<IHH', out, e, bit, left, right)
                    if i:
                        name, child = o.items[i - 1]
                        text = o.name_at[i - 1] if self.exact else self.texts[name]
                        struct.pack_into('<ii', out, e + 8, text - (e + 8), child.off - (e + 12))
            elif o.kind in LEAVES:
                b = o.pack(); out[at:at + len(b)] = b
            else:
                self.emit(out, o, fields(o), at)
        for t, at in ([(t, at) for at, t in sc.hint['texts'].items()] if self.exact else self.texts.items()):
            out[at:at + len(t)] = t.encode()
        if self.blobs:
            struct.pack_into('<4sI', out, data_end, b'IMAG', size - data_end)
        return bytes(out)


def write(scene, exact=False):
    return Writer(scene, exact).build()



# ---------------------------------------------------------------- describing

FORMATS = ['RGBA8', 'RGB8', 'RGBA5551', 'RGB565', 'RGBA4', 'LA8', 'HILO8', 'L8', 'A8', 'LA4', 'L4', 'A4', 'ETC1', 'ETC1A4']
SOURCES = {0: 'vertex', 1: 'light', 2: 'specular', 3: 'tex0', 4: 'tex1', 5: 'tex2', 6: 'tex3', 13: 'buffer', 14: 'const', 15: 'prev'}
COMBINE = ['a', 'a*b', 'a+b', 'a+b-0.5', 'lerp(a,b,c)', 'a-b', 'dot3', 'dot3a', 'a*b+c', '(a+b)*c']
USAGE = ['pos', 'normal', 'tangent', 'color', 'uv0', 'uv1', 'uv2', 'boneIndex', 'boneWeight']
BILLBOARD = ['', 'World', 'WorldViewpoint', 'Screen', 'ScreenViewpoint', 'YAxial', 'YAxialViewpoint']


def stage_text(st):
    def side(src, op, comb, alpha):
        names = []
        for k in range(3):
            n, o = SOURCES.get(src >> 4 * k & 15, '?'), (op >> (3 if alpha else 4) * k + (0 if not alpha else 0)) & (7 if alpha else 15)
            if alpha: o = op >> [12, 16, 20][k] & 7
            suffix = (['.a', '.(1-a)', '.r', '.(1-r)', '.g', '.(1-g)', '.b', '.(1-b)'][o] if alpha else
                      {0: '', 1: '.(1-rgb)', 2: '.a', 3: '.(1-a)', 4: '.r', 5: '.(1-r)', 8: '.g', 9: '.(1-g)', 12: '.b', 13: '.(1-b)'}.get(o, '?'))
            names.append(n + suffix)
        expr = COMBINE[comb] if comb < len(COMBINE) else f'combine{comb}'
        for letter, n in zip('abc', names): expr = expr.replace(letter, '{' + letter + '}')
        return expr.format(a=names[0], b=names[1], c=names[2])
    return (f'rgb = {side(st.source & 0xFFF, st.operand, st.combine & 0xFFFF, False)}'
            + (f' x{1 << (st.scale & 3)}' if st.scale & 3 else '')
            + f';  alpha = {side(st.source >> 16 & 0xFFF, st.operand, st.combine >> 16, True)}'
            + (f' x{1 << (st.scale >> 16 & 3)}' if st.scale >> 16 & 3 else '') + f'   [const = constant{st.constant}]')


def info(scene, out=print):
    r = lambda v: '(' + ', '.join(f'{x:.3g}' for x in v) + ')'
    for name, t in scene.textures:
        if t.kind == 'TexImage': out(f'texture {name}: {t.width}x{t.height} {FORMATS[t.format]}, {t.mipLevels} level(s), {len(t.image.data)} bytes')
    for name, lut in scene.luts:
        out(f'lookup tables {name}: ' + ', '.join(n for n, _ in lut.tables))
    for name, l in scene.lights:
        if l.kind == 'FragLight':
            out(f'light {name}: type {l.lightType}, direction {r(l.direction)}, ambient {r(l.ambient[:3])}, diffuse {r(l.diffuse[:3])}, '
                f'specular {r(l.specular0[:3])} / {r(l.specular1[:3])}, position {r(l.translation)}, flags {l.lightFlags}, groups {[n for n, _ in l.animGroups]}')
        else: out(f'light {name}: {l.kind} {r(l.color)}')
    for name, sc in scene.scenes:
        out(f'scene {name}: light sets ' + '; '.join(f'{ls.id}: ' + ', '.join(x.name for x in ls.lights) for ls in sc.lightSets))
    for name, m in scene.models:
        out(f'model {name}: {len(m.meshes)} meshes, {len(m.materials)} materials, groups {[n for n, _ in m.animGroups]}')
        if m.skeleton:
            for bn, b in m.skeleton.bones:
                out(f'  bone {b.index} {bn}: parent {b.parentIndex}, scale {r(b.scale)}, rotation {r(b.rotation)}, translation {r(b.translation)}'
                    + (f', billboard {BILLBOARD[b.billboard]}' if b.billboard else '') + f', flags 0x{b.flags:x}')
        for mn, mt in m.materials:
            out(f'  material {mn}: flags 0x{mt.flags:x}, layer {mt.renderLayer}, depth flags {mt.depthFlags} cmd 0x{mt.depthCmd[0]:x}, '
                f'blend mode {mt.blendMode} 0x{mt.blendCmd[2]:08x}, cull cmd {mt.rasterCmd[0]}, alpha test 0x{mt.fragShader.alphaTest[0]:x}, light set {mt.lightSet}')
            out(f'      emission {r(mt.emission)} ambient {r(mt.ambient)} diffuse {r(mt.diffuse)} specular {r(mt.specular0)} / {r(mt.specular1)}')
            out('      constants ' + ' '.join(r(mt.constant[i * 4:i * 4 + 4]) for i in range(6)))
            for i, mp in enumerate([mt.mapper0, mt.mapper1, mt.mapper2]):
                if mp is None: continue
                c = mt.texCoords[i]
                out(f'      texture {i}: {mp.texture.link}, filter word 0x{mp.cmd[5]:x}, min filter {mp.sampler.minFilter}; coordinates from '
                    + (f'uv{c.source}' if c.mapping == 0 else ['uv', 'camera cube', 'camera sphere', 'projection'][c.mapping])
                    + f', scale {r(c.scale)}, rotation {c.rotation:.3g}, translation {r(c.translation)}, matrix mode {c.matrixMode}')
            fs = mt.fragShader
            if mt.flags & 1 or fs.lightFlags or fs.layerConfig:
                lt = fs.lutTable
                luts = {n: (f'{getattr(lt, n).sampler.table} (input {getattr(lt, n).input}, scale {getattr(lt, n).scale})') for n in
                        ('reflectR', 'reflectG', 'reflectB', 'dist0', 'dist1', 'fresnel') if getattr(lt, n) is not None}
                out(f'      lighting: flags 0x{fs.lightFlags:x}, layer config {fs.layerConfig}, fresnel {fs.fresnel}, bump {fs.bumpMode} tex {fs.bumpTexture}, tables {luts}')
            for i, st in enumerate(fs.stages):
                if (st.source, st.combine) != (0x0E1F0E1F, 0): out(f'      stage {i}: {stage_text(st)}')
        for i, mesh in enumerate(m.meshes):
            sh = m.shapes[mesh.shapeIndex]
            mat = m.materials.items[mesh.materialIndex][0]
            desc = []
            for a in sh.attrs:
                if a.kind == 'Interleaved':
                    n = len(a.data) // a.stride
                    desc.append(f'{n} vertices: ' + ' '.join(f'{USAGE[x.usage]}:{x.components}x{ {0x1400: "s8", 0x1401: "u8", 0x1402: "s16", 0x1406: "f32"}[x.format]}'
                                                             + (f'*{x.scale:.4g}' if x.scale != 1 else '') for x in a.attrs))
                elif a.kind == 'FixedAttr': desc.append(f'fixed {USAGE[a.usage]} {r(a.values)}')
                else: desc.append(f'separate {USAGE[a.usage]}')
            tris = sum(len(st.data) // (1 if st.format == 0x1401 else 2) for ps in sh.primSets for pr in ps.prims for st in pr.streams) // 3
            bones = [b for ps in sh.primSets for b in ps.bones]
            out(f'  mesh {i}: node {mesh.meshNodeName!r}, material {mat}, priority {mesh.priority}, bones {bones}, skinning {[ps.skinning for ps in sh.primSets]}, '
                f'{tris} triangles, ' + '; '.join(desc) + f'; box centre {r(sh.bbox.center)} size {r(sh.bbox.size)}')
    for slot in ('skeletalAnims', 'materialAnims', 'visibilityAnims', 'lightAnims'):
        for name, a in getattr(scene, slot):
            out(f'{slot} {name}: target {a.targetGroup}, {a.frames:g} frames, loop {a.loopMode}')
            for mn, mem in a.members:
                parts = []
                for f in fields(mem):
                    v = getattr(mem, f.name)
                    if f.typ.startswith('slot'):
                        if isinstance(v, float): parts.append(f'{f.name}={v:.4g}')
                        elif v is not None:
                            segs = v.segments
                            keys = [k for sg in segs for k in sg.keys]
                            vals = [k[1] for k in keys if isinstance(k, tuple)]
                            rng = f' {min(vals):.3g}..{max(vals):.3g}' if vals else ''
                            parts.append(f'{f.name}: {len(keys)} keys{rng} (quant {segs[0].quant}, interp {segs[0].interpolation}, repeat {v.preRepeat}/{v.postRepeat})')
                    elif f.name == 'textures' and v: parts.append('textures ' + ','.join(t.link for t in v))
                    elif f.name == 'curve' and v is not None: parts.append(f'bits {v.bits.hex()}')
                out(f'  {mn}: ' + '; '.join(parts))


def main():
    cmd, paths = sys.argv[1], sys.argv[2:]
    if cmd == 'info':
        info(read(open(paths[0], 'rb').read()))
    if cmd == 'check':
        for path in paths:
            b = open(path, 'rb').read()
            try:
                s = read(b)
            except Exception as e:
                print(f'{path.split("/")[-1]}: CANNOT READ: {type(e).__name__}: {e}'); continue
            gaps = sum(e - st for st, e in s.unread)
            print(f'{path.split("/")[-1]}: {len(s.objs)} structures, {gaps} bytes not accounted for in {len(s.unread)} places'
                  + ''.join(f'\n    {n}' for n in s.notes) + ''.join(f'\n    not read: 0x{st:x}..0x{e:x}' for st, e in s.unread[:6])
                  + ''.join(f'\n    read twice: 0x{st:x}..0x{e:x} ({what})' for st, e, what in s.overlaps[:6]))
            for exact in (True, False):
                label = 'written again, blocks in their original places' if exact else 'written again, laid out afresh'
                try:
                    again = write(s, exact)
                except Exception as e:
                    print(f'    {label}: FAILED {type(e).__name__}: {e}'); continue
                diff = [i for i in range(min(len(b), len(again))) if b[i] != again[i]]
                if not diff and len(b) == len(again): print(f'    {label}: identical')
                else: print(f'    {label}: {len(diff)} bytes differ, first at 0x{diff[0] if diff else min(len(b), len(again)):x}; sizes {len(again)} and {len(b)}')


if __name__ == '__main__':
    main()
