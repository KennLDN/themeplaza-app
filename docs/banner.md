# HOME Menu banners: CBMD, BCWAV and CGFX

Reference for the app's animated 3D banner: the container, the sound, what the HOME Menu
does with the scene, the CGFX structures our writer emits, and what was verified. The
format facts come from 3dbrew, GBATEK, bannertool and the open-source CGFX tools listed
under [References](#references); the layout rules were checked byte for byte against the
system's own banners, and the HOME Menu behaviour was measured with test banners (section 5).
Statements no source or test confirms are marked "unconfirmed".

The app's banner is built by `tools/make_banner.py` on `tools/banner_scene.py` (scene
description) and `tools/cgfx.py` (file writer). `tools/cgfx_hash.py` computes the material
hashes, `tools/banner_selftest.py` checks the builder against a system banner, and
`tools/banner_samples.py` extracts the system banners it is checked against.

## Summary

- A banner (`ExeFS:/banner`, `.bnr`) is a 0x88-byte CBMD header, then up to 14
  LZ11-compressed CGFX files (one common plus 13 region/language slots), then one BCWAV
  aligned to 16 bytes.
- The decompressed CGFX must be at most 0x80000 bytes (512 KiB). The sound must be at
  most 3 seconds and have 2 channels.
- The HOME Menu draws the CGFX model named `COMMON` with its own camera (30 degree
  vertical FOV, at (0, 1, 44.786) looking down -Z; 1 unit is 10 pixels at z = 0) and
  turns the model about the vertical axis once per 600 frames. Parts that must keep
  facing the viewer sit on a billboard bone.
- Animation is a CANM named `COMMON` in the skeletal-animation dictionary, targeting the
  model's `SkeletalAnimation` group, with one Transform track per bone, 60 frames per
  second, loop flag set. Rigid per-bone animation works; soft skinning (bone index and
  weight streams) is reported to freeze the HOME Menu on hardware.
- Official banners, bannertool's template and pycgfx all use the same object revisions:
  CGFX 0x05000000 with 15 dictionary slots, CMDL 0x07000000, MTOB 0x06000000,
  CANM 0x05000000. Our writer emits that family.

## 1. CBMD / BNR container

Sources: [3dbrew CBMD](https://www.3dbrew.org/wiki/CBMD),
[GBATEK CBMD](http://problemkaputt.de/gbatek-3ds-files-video-banner-cbmd.htm), bannertool
`cbmd.cpp` and `lz11.cpp`. All three agree.

### Header (0x88 bytes, little endian)

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Magic `CBMD` |
| 0x04 | 4 | Zero |
| 0x08 | 4 | Offset of the common CGFX (0x88 when it comes first) |
| 0x0C | 4 x 13 | Offsets of region/language CGFX, 0 = absent |
| 0x40 | 0x44 | Padding, zero (GBATEK guesses CHN/KOR/TWN slots could live here; unconfirmed) |
| 0x84 | 4 | Offset of the BCWAV, 0 = no sound |

Slot order after the common one: EUR-English, EUR-French, EUR-German, EUR-Italian,
EUR-Spanish, EUR-Dutch, EUR-Portuguese, EUR-Russian, JPN-Japanese, USA-English,
USA-French, USA-Spanish, USA-Portuguese. The common CGFX is used when the slot for the
console's region/language is zero.

The header holds offsets only. A section's size is the distance to the next larger offset
(or file end).

Language slots hold smaller CGFX files, usually with one or two textures (GBATEK). How a
language CGFX is merged with the common one is unconfirmed; one report names textures
`commonXX` and language-specific ones `EN01`, `ES01`, `JP01`.

### Layout written by bannertool (`bnr_build`)

1. Header.
2. Each present CGFX, LZ11-compressed, in slot order, back to back. Each compressed stream
   is padded with zeros to a multiple of 4 bytes.
3. Zero padding to a multiple of 0x10.
4. The BCWAV, uncompressed.

"CBMD" strictly means header plus CGFX; "BNR" is the same thing with the CWAV appended.
`ExeFS:/banner` is the BNR form.

### LZ11

- Byte 0: 0x11. Bytes 1..3: decompressed size, 24-bit little endian.
- Then groups of one flag byte followed by 8 items, flags read from bit 7 down. Flag 0:
  one literal byte. Flag 1: a back-reference.
- Back-reference encodings, by the top nibble of the first byte:
  - nibble 0: 3 bytes, length = ((b0 & 0xF) << 4 | b1 >> 4) + 0x11, displacement = ((b1 & 0xF) << 8 | b2) + 1
  - nibble 1: 4 bytes, length = ((b0 & 0xF) << 12 | b1 << 4 | b2 >> 4) + 0x111, displacement = ((b2 & 0xF) << 8 | b3) + 1
  - otherwise: 2 bytes, length = (b0 >> 4) + 1, displacement = ((b0 & 0xF) << 8 | b1) + 1
- Window 0x1000 bytes, minimum match 3.
- A stream of literals only (every flag byte zero) is valid LZ11.

### Size limits

| Limit | Value | Source |
|---|---|---|
| Decompressed CGFX (ExeFS banner) | at most 0x80000 bytes; the HOME Menu checks this | 3dbrew CBMD and 3DS_Userland_Flaws; GBATEK |
| Whole banner file | none documented; makerom copies the file into ExeFS with no size check | makerom `ncch.c`, `exefs.c` |
| SpotPass exbanner CBMD buffer | 0x100000 bytes (a different code path) | 3dbrew Userland Flaws |
| Texture dimensions | power of two, 8 to 1024 per side (PICA200 rule) | citro3d `checkTexSize`; a HOME Menu-specific limit is unconfirmed |
| Texture format RGBA8 | broken on the HOME Menu, do not use | pycgfx `txob.py` |
| Vertex / polygon count | no limit documented; indices are u8 or u16, so at most 65536 vertices per shape | unconfirmed |
| Working sizes | 331 KiB CGFX with three animated meshes runs on hardware (community report); the largest system banner is 206 KB, 2,300 triangles, 12 meshes | |

## 2. Banner sound (BCWAV)

Sources: [3dbrew BCWAV](https://www.3dbrew.org/wiki/BCWAV), 3dbrew CBMD, bannertool `cwav.cpp`.

### What the HOME Menu requires

- "The included BCWAV total channels must be 2, and the length of the audio must be 3
  seconds or less, otherwise the sound will play incorrectly (beeping/clicking) or the
  model may fail to load" (3dbrew CBMD).
- All 13 system banners have 2 channels, 32,000 Hz (three at 32,728), DSP-ADPCM
  (encoding 2), 2.0 to 3.0 s, no loop.
- Sample rate limit: none documented. Whether the 3-second rule depends on sample rate is
  unconfirmed.
- Encodings the format allows: 0 PCM8, 1 PCM16, 2 DSP ADPCM, 3 IMA ADPCM. bannertool only
  writes PCM8/PCM16; PCM16 banners are known to work. ADPCM and loop flags in custom
  banners are unconfirmed.
- bannertool does not check length, channel count or sample rate, and writes as many
  channels as the WAV has. The app's `app/meta/audio.wav` (made by `tools/make_music.py`)
  is stereo, 32 kHz and under 3 s for this reason.

### File layout as bannertool writes it (PCM)

- Header, 0x40 bytes: `CWAV`, BOM 0xFEFF, header size 0x40, version 0x02010000, file
  size, block count 2, reserved, sized reference to INFO (type 0x7000, offset 0x40, size),
  sized reference to DATA (type 0x7001, offset, size).
- INFO block, padded to 0x20: `INFO`, size, encoding u8, loop u8, pad u16, sample rate
  u32, loop start frame, loop end frame (total frames when not given), reserved, channel
  reference table (count, then per channel type 0x7100 + offset relative to the count
  field), then per channel a 0x14-byte channel info: samples reference (type 0x1F00,
  offset relative to the DATA block's data field), ADPCM reference (type 0, offset
  0xFFFFFFFF for PCM), reserved.
- DATA block: `DATA`, size, padding so the samples start on a 0x20 boundary, then the
  samples per channel, not interleaved (all of channel 0, then all of channel 1).

## 3. What the HOME Menu does with the banner CGFX

### Names

| Item | Value |
|---|---|
| Model (CMDL) name | `COMMON`. Every working example uses it; whether it is enforced is unconfirmed |
| Skeletal CANM name | `COMMON` |
| Material CANM name | `COMMON` |
| Number of models | 1 in every example seen; more is unconfirmed |
| Texture names | free (`COMMON1`, `COMMON3`, `commonXX` all seen) |
| Material, bone, mesh names | free |
| Shader | materials reference a shader by name `DefaultShader`; no SHDR object is in the file |

### Camera

The CGFX needs no camera. The HOME Menu's camera (from pycgfx's `banner-camera.gltf`,
confirmed by measurement, section 5):

- perspective, aspect 400:240, vertical FOV 0.523599 rad (30 degrees), near 26.5, far 1000
- position (0, 1, 44.786), no rotation, so it looks down -Z with +Y up and +X right

The plane z = 0 shows 40 x 24 units, so 1 unit = 10 pixels at z = 0, and the screen centre
is at y = 1. bannertool's template quad is 26 x 13 units on a bone translated +1 in y,
about 260 x 130 pixels for its 256 x 128 texture. Near plane 26.5 at camera z 44.786 means
geometry with z above about 18.3 is clipped.

### Rotation, billboards and lights

See the measurements in section 5. In short: the HOME Menu turns the model about the
vertical axis through the origin, one turn per 600 frames, in step with the animation;
a bone with a billboard mode holds its own mesh still (children do not inherit it); lights
in the file work and stay fixed relative to the viewer; unlit materials need no lights.

Coordinate system: right-handed, +Y up, camera on +Z looking at the origin (the same as
glTF). The material's cull mode field is ignored by the HOME Menu; pycgfx duplicates
geometry with reversed winding for double-sided materials.

### Animation

| Question | Answer |
|---|---|
| How the animation is found | CANM named `COMMON` in DATA slot 9 (skeletal) or 10 (material); its `TargetAnimGroupName` (`SkeletalAnimation`, `MaterialAnimation`) selects the model's animation group; each CANM member path equals a member path of that group (a bone name, or `Materials["m"].MaterialColor.Diffuse` etc.) |
| Frame rate | 60 frames per second. System banners' skeletal CANMs have 600 frames (one has 149) |
| Looping | `LoopMode = 1` in official files; curves use repeat methods (0 none, 1 repeat, 2 mirrored, 3 relative repeat). Whether the HOME Menu loops a CANM whose flag is 0 is unconfirmed |
| Rigid bone animation (Transform tracks, skinning mode 0, one bone per primitive set) | works; it is all the system banners use |
| Soft skinning (bone index + weight vertex streams, skinning mode 2) | reported to freeze or crash the HOME Menu on hardware while an emulator is fine; not used |
| Material colour animation | supported by pycgfx via a material CANM (RGBA tracks); reported to work |
| Texture SRT / pattern animation, visibility animation | exist in official banners; no custom example known; unconfirmed |
| `Sampler.BorderColor` member | reported to crash sometimes; pycgfx leaves it out |
| Mesh `MeshNodeName` | the system banners set it to the bone name for each rigid mesh; our builder does the same |

SMDH has a float "optimal banner frame" (3dbrew SMDH): the most representative frame of
the banner animation. bannertool exposes it as `-obf`. Whether it affects playback is
unconfirmed.

The SMDH `extendedbanner` flag (0x0010) is described on 3dbrew as "Uses an Extended
Banner", which is a different thing (text and a texture in extdata). One tutorial claims
it is needed for a 3D banner; unconfirmed.

### Other behaviour

- The HOME Menu caches banners; reinstalling may show the old one until a reboot.
- Draw order follows bone order; put translucent parts last. The template's material uses
  render layer 1 (translucent), depth test on, depth write off, blend src-alpha /
  one-minus-src-alpha.

## 4. CGFX (BCRES) format, as our writer emits it

All integers little endian. Every pointer is self-relative: stored value = target offset -
offset of the pointer field; 0 = null. Counted lists are stored as (count u32, pointer u32).
`tools/cgfx.py` describes every structure once (its `KINDS` table) and reads and writes
from that description; its `check` command reads a file, confirms every byte is accounted
for, and writes it back byte for byte.

### 4.1 Revisions to emit

| Object | Revision |
|---|---|
| CGFX header | 0x05000000 |
| DATA dictionary slots | 15 |
| CMDL | 0x07000000 |
| MTOB | 0x06000000 |
| TXOB (image and reference) | 0x05000000 |
| SHDR reference | 0x05000000 |
| Mesh / Shape / Skeleton SOBJ | 0 |
| CANM | 0x05000000 |
| CFLT, CENV | 0x06000000 |
| LUTS | 0x04000000 |

EFE and SPICA write a newer family (CMDL 0x09000000, MTOB 0x06000003, SHDR 0x06000000,
16 slots; SPICA also CANM 0x07000001). The two families differ in animation-group members,
CANM members and the mesh tail, so do not mix layouts from different sources. Whether the
newer family is accepted for animated banners is unconfirmed; only static EFE output is
known to work.

### 4.2 File layout

```
0x00  CGFX header (0x14)
0x14  DATA block: "DATA", size, 15 x (count, ptr)
      dictionaries and objects
      string table (NUL-terminated ASCII, deduplicated)
      zero padding
      IMAG block: "IMAG", size, raw buffers (index data, vertex data, texture data)
```

CGFX header: `CGFX`, BOM u16 0xFEFF (bytes FF FE), header size u16 0x14, revision u32,
file size u32, block count u32 (1 = DATA only, 2 = DATA + IMAG).

DATA: `DATA`, block size u32 (from the `D` to the start of IMAG), then slots in this
order: 0 models, 1 textures, 2 LUTs, 3 materials, 4 shaders, 5 cameras, 6 lights, 7 fogs,
8 scenes (CENV), 9 skeletal animations, 10 material animations, 11 visibility animations,
12 camera animations, 13 light animations, 14 emitters. Empty slot = (0, 0).

IMAG: `IMAG`, size u32 (including the 8-byte header), data.

Layout rules, as the system's banners follow them (our writer reproduces all 154 system
CGFX files byte for byte with these rules): the file's dictionaries first; then per
object: the structure, the rows its lists point to, the tables of its own dictionaries,
then what it points to, in field order. Texts are stored once, in order of first mention.
A model's rows start on a multiple of 8, and so does each mesh. The IMAG block's contents
start on a multiple of 0x80, and so does each texture; other buffers start on a multiple
of 4.

### 4.3 DICT and the patricia tree

```
0x00 "DICT"
0x04 size = 0x0C + (N + 1) * 0x10
0x08 N
0x0C root node, then N nodes, 0x10 bytes each:
       u32 refbit, u16 left, u16 right, ptr name, ptr data
```

Root node: refbit 0xFFFFFFFF, `left` = index of the first node to visit, right 0, name 0,
data 0. Node i (1-based) describes entry i - 1.

Tree rules (the same in EFE, SPICA and pycgfx; they reproduce every dictionary in
bannertool's template and the system banners):

- Keys are compared as byte strings zero-padded to L = longest name length in this
  dictionary.
- Bit b means byte b >> 3, bit b & 7 (least significant bit first).
- Insert a name:
  1. Walk from the root: `cur = root.left`; while `parent.refbit > cur.refbit`:
     `parent = cur; cur = bit(name, cur.refbit) ? cur.right : cur.left`. The node reached
     is the closest existing key.
  2. Starting at b = L * 8 - 1 and counting down, find the first bit where the new name
     and that key differ. That is the new node's refbit.
  3. Walk again from the root with the extra stop condition `cur.refbit > b`. Call the
     last node passed `parent` and the node reached `child`.
  4. New node: if bit(name, b) is 1 then left = child, right = itself; else left = itself,
     right = child.
  5. Link from `parent`: for the root always `left`; otherwise right if
     bit(name, parent.refbit) is 1, else left.
- Lookup is the walk of step 1 followed by one string comparison.

### 4.4 Model (CMDL), revision 0x07000000

| Offset | Field |
|---|---|
| 0x00 | type 0x40000012; with skeleton 0x40000092 (bit 7) |
| 0x04 | `CMDL` |
| 0x08 | revision |
| 0x0C | name ptr |
| 0x10 | user data (count, DICT ptr) |
| 0x18 | flags (1) |
| 0x1C | is branch visible (1) |
| 0x20 | child count; 0x24 unused/children ptr |
| 0x28 | animation groups (count, DICT ptr) |
| 0x30 | scale (3 f32); 0x3C rotation; 0x48 translation |
| 0x54 | local matrix 3x4; 0x84 world matrix 3x4 (row-major, translation in the 4th column) |
| 0xB4 | meshes (count, ptr to array of pointers) |
| 0xBC | materials (count, DICT ptr) |
| 0xC4 | shapes (count, ptr to array of pointers) |
| 0xCC | mesh node visibilities (count, DICT ptr) |
| 0xD4 | visible (1); 0xD8 cull mode (0); 0xDC layer id (0) |
| 0xE0 | skeleton ptr (only when bit 7 of type is set) |

A banner model always has a skeleton: bones are the scene nodes (billboards, animation
targets).

### 4.5 Mesh (SOBJ, type 0x01000000), 0x80 bytes

| Offset | Field |
|---|---|
| 0x00 | type, `SOBJ`, revision 0, name ptr, user data (count, ptr) |
| 0x18 | shape index; 0x1C material index |
| 0x20 | owner ptr (signed, back to the CMDL) |
| 0x24 | visible u8; 0x25 render priority u8; 0x26 mesh node visibility index i16 (-1 when unused) |
| 0x28 | 18 words zero, filled at run time |
| 0x70 | mesh node name ptr (the bone name) |
| 0x74 | 3 words zero |

### 4.6 Shape (SOBJ, type 0x10000001)

| Offset | Field |
|---|---|
| 0x00 | type, `SOBJ`, revision 0, name ptr, user data (count, ptr) |
| 0x18 | flags (0) |
| 0x1C | bounding box ptr: type 0x80000000, centre 3 f32, orientation 3x3, size 3 f32 (nothing reads it) |
| 0x20 | position offset 3 f32 |
| 0x2C | primitive sets (count, ptr to pointer array) |
| 0x34 | base address (0) |
| 0x38 | vertex attributes (count, ptr to pointer array) |
| 0x40 | blend shape ptr (0) |

Primitive set (0x14): related bones (count, ptr to u32 array of bone indices), skinning
mode (0 none, 1 rigid, 2 smooth), primitives (count, ptr array). With skinning mode 0 the
set lists exactly one bone, and the shape is drawn with that bone's transform.

Primitive (0x10): index streams (count, ptr array), buffer objects (count, ptr to u32
array, one zero per index stream). Nothing follows the two lists.

Index stream (0x2C): format u32 (0x1401 u8, 0x1403 u16), primitive mode u8 (0 = triangle
list), visible u8 (1), pad u16, data size u32, data ptr (into IMAG), buffer object,
location flag, command cache, command cache size, location address, memory area, bounding
box ptr; all zero in a file.

Vertex attributes, three kinds:

- Interleaved buffer (type 0x40000002), 0x30 bytes: type, usage 21, flags 2, buffer object
  0, location flag 0, data size, data ptr (IMAG), location address 0, memory area 0,
  stride, attributes (count, ptr array). Each attribute (type 0x40000001, 0x34 bytes):
  type, usage, flags 0, buffer object, location flag, size 0, ptr 0, location address,
  memory area, format, component count, scale f32, offset within the vertex. This is what
  the system banners and our writer use: float positions, normals and texture coordinates
  in one interleaved buffer, colours as 4 bytes scaled by 1/255.
- Separate stream (type 0x40000001 used standalone, with its own data size and data ptr):
  what pycgfx writes.
- Fixed value (type 0x80000000): type, usage, flags 1, format 0x1406, component count,
  scale, (count, ptr) of f32. EFE uses it for a constant vertex colour.

Usage: 0 position, 1 normal, 2 tangent, 3 colour, 4 texcoord0, 5 texcoord1, 6 texcoord2,
7 bone index, 8 bone weight.
Format: 0x1400 s8, 0x1401 u8, 0x1402 s16, 0x1406 f32. u16 (0x1403) is not valid for
vertex data. Stored value x scale = real value.

### 4.7 Skeleton and bones

Skeleton (SOBJ, type 0x02000000, 0x2C bytes): type, `SOBJ`, revision 0, name ptr, user
data, bones (count, DICT ptr), root bone ptr, scaling rule (0 standard, 1 Maya,
2 Softimage), flags (bit 0 model coordinate, bit 1 translation animation enabled).

Bone (0xE0 bytes): name ptr, flags, index, parent index (-1 for none), parent ptr
(signed), child ptr, previous sibling ptr, next sibling ptr, scale 3 f32, rotation 3 f32
(Euler XYZ radians), translation 3 f32, local matrix 3x4, world matrix 3x4, inverse base
matrix 3x4, billboard mode u32, user data (count, ptr).

Bone flags: 1 identity, 2 translation zero, 4 rotation zero, 8 scale one, 0x10 uniform
scale, 0x20 segment scale compensate, 0x40 needs rendering, 0x80 local matrix calculate,
0x100 world matrix calculate, 0x200 has skinning matrix.

Billboard modes: 0 off, 1 World, 2 WorldViewpoint, 3 Screen, 4 ScreenViewpoint, 5 YAxial,
6 YAxialViewpoint.

### 4.8 Material (MTOB, type 0x08000000), revision 0x06000000, 0x2D8 bytes

| Offset | Field |
|---|---|
| 0x00 | type, `MTOB`, revision, name ptr, user data (count, ptr) |
| 0x18 | flags: 1 fragment light, 2 vertex light, 4 hemisphere light, 8 hemisphere occlusion, 0x10 fog, 0x20 polygon offset |
| 0x1C | texture coordinate config (0) |
| 0x20 | render layer / translucency kind (0 opaque, 1 translucent, ...) |
| 0x24 | 11 colours as 4 f32: emission, ambient, diffuse, specular0, specular1, constant0..5 |
| 0xD4 | the same 11 colours as RGBA bytes |
| 0x100 | command cache (0) |
| 0x104 | rasterisation: flags, cull mode, polygon offset unit f32, PICA command (param, header 0x00010040) |
| 0x118 | depth: flags (1 test, 2 write), commands `0x41, 0x00010107`, `0x03000000, 0x00080126` |
| 0x12C | blend: mode, colour 4 f32, commands `0x00E40100, 0x803F0100`, `blend function word, 0`, `blend colour RGBA8, 0` |
| 0x158 | stencil commands `0, 0x000D0105`, `0, 0x000F0106` |
| 0x168 | used texture coordinator count |
| 0x16C | 3 texture coordinators, 0x58 each |
| 0x274 | texture mapper ptrs x 3, procedural mapper ptr |
| 0x284 | shader reference ptr; 0x288 fragment shader ptr |
| 0x28C | shader program description index, shader parameters (count, ptr), light set index, fog index |
| 0x2A0 | 13 hashes; 0x2D4 material id |

Blend function word: `colour eq | alpha eq << 8 | src colour << 16 | dst colour << 20 |
src alpha << 24 | dst alpha << 28`; factors 0 zero, 1 one, 2 src colour, 3 inv src colour,
4 dst colour, 5 inv dst colour, 6 src alpha, 7 inv src alpha, 8 dst alpha, 9 inv dst
alpha, 10 to 13 constant colour/alpha and inverses, 14 src alpha saturate. Alpha blending
is 0x76760000.

Texture coordinator (0x58): source coordinate, mapping method (0 UV, 1 camera cube,
2 camera sphere, 3 projection), reference camera, matrix mode, scale 2 f32, rotation f32,
translation 2 f32, flag u32 (0), matrix 3x4.

Texture mapper (0x4C): type 0x80000000, dynamic allocator 0, texture reference ptr,
sampler ptr, 14 command words, command size 0x38. Command words for unit 0: `0,
0x0001008E`, `0xFF000000, 0x809F0081`, `0, 0x00002206`, zeros. 0x2206 = linear mag and
min filter, repeat on both axes; width, height and address words are filled at run time.
For mapper 1 and 2 the register numbers differ by 8 per unit.

Sampler (0x20): type 0x80000000, owner ptr (signed, back to the mapper), min filter,
border colour 4 f32, LOD bias f32.

Texture reference (TXOB type 0x20000004, 0x20): type, `TXOB`, revision, name ptr (empty),
user data, linked texture name ptr, linked texture ptr (0; resolved by name at load).

Shader reference (SHDR type 0x80000001, 0x20): type, `SHDR`, revision, name ptr (empty),
user data, shader name ptr (`DefaultShader`), 0.

Fragment shader (0xF4): buffer colour 4 f32; fragment lighting 6 words (flags, layer
config, fresnel config, bump texture, bump mode, bump renormalise); LUT table ptr (to 6
pointers: reflectance R, G, B, distribution 0, distribution 1, fresnel; all 0 when unlit);
6 texture combiner stages of 0x1C; alpha test command (param, header 0x000F0104); buffer
commands `0xFF000000, 0x000F00FD`, `0, 0x000200E0`, `0x400, 0x000201C3`.

Combiner stage (0x1C): constant source u32 (0..5 = material constant colour); source RGB
u16, source alpha u16 (three nibbles: source 0, 1, 2); header u32 = 0x804F0000 | register
(0xC0, 0xC8, 0xD0, 0xD8, 0xF0, 0xF8); operands u32; combine RGB u16, combine alpha u16;
constant colour RGBA8; scale RGB u16, scale alpha u16.
Sources: 0 primary colour, 1 fragment primary (diffuse light), 2 fragment secondary
(specular), 3 texture0, 4 texture1, 5 texture2, 0xE constant, 0xF previous. Combine:
0 replace, 1 modulate, 2 add, ..., 8 mult-add.
A flat textured material has stage 0 with sources 0x0E30 for RGB and alpha and combine
modulate (output = primary colour x texture0), and stages 1..5 with source 0x0E1F and
combine replace (pass through).
Alpha test param: `enabled | function << 4 | reference << 8`; functions 0 never, 1 always,
2 equal, 3 not equal, 4 less, 5 less-equal, 6 greater, 7 greater-equal.

Hashes: the console compares the 13 hashes between materials to decide which settings it
can skip sending again, so two materials may only share a hash if they share the settings.
Each is the MD5 of a description of one group of settings, folded to 32 bits (0 becomes
1). `tools/cgfx_hash.py` reproduces all 13 for all 96 materials in the system banners. The
descriptions follow SPICA with three corrections: the lighting hash has no "enabled" byte,
a blend's alpha part is hashed as one/zero/add, and most banners leave the buffer colour
out of the combiner hash. The alpha-test hash uses the reference as authored (0.5), not
the byte / 255. Two hashes (texture mappers, lighting tables) are left 0 in files; the
console fills them in. pycgfx writes 0 for most hashes and its banners are reported to
work, so zeros appear to be accepted; our writer computes them anyway.

### 4.9 Texture (TXOB, type 0x20000011), 0x5C bytes

type, `TXOB`, revision, name ptr, user data; height u32, width u32, GL format, GL type,
mip level count, texture object 0, location flag 0, hardware format; image ptr (4,
pointing at the struct that follows); image struct: height, width, data size, data ptr
(IMAG), dynamic allocator 0, bits per pixel, location address 0, memory area 0.

Hardware formats: 0 RGBA8, 1 RGB8, 2 RGBA5551, 3 RGB565, 4 RGBA4, 5 LA8, 6 HILO8, 7 L8,
8 A8, 9 LA4, 10 L4, 11 A4, 12 ETC1, 13 ETC1A4. Bits per pixel: 32, 24, 16, 16, 16, 16,
16, 8, 8, 8, 4, 4, 4, 8.

GL format / GL type per hardware format: formats `6752 6754 6752 6754 6752 6758 6759 6757
6756 6758 6757 6756 675A 675B`, types `1401 1401 8034 8363 8033 1401 1401 1401 1401 6760
6761 6761 0 0` (hex).

Pixel data: 8 x 8 tiles in row order, pixels inside a tile in Morton (Z) order:
`((y >> 3) * (width >> 3) + (x >> 3)) << 6` plus the bits of x and y interleaved (x in the
even positions). Rows are written top row first, and the first row in memory is v = 1
(verified: text on a texture reads correctly). glTF has v = 0 at the top, which is why
pycgfx flips images vertically. RGBA4 pixel: `R << 12 | G << 8 | B << 4 | A`, stored
little endian. Mipmaps follow the base level, each half the size. ETC1: 4 x 4 blocks
stored as little-endian u64, grouped 2 x 2 per 8 x 8 tile; ETC1A4 puts 8 bytes of 4-bit
alpha before each block. The system banners use ETC1 for larger pictures. RGBA4, RGB565 and A8
from our writer have been tried on the HOME Menu (the app's banner uses RGB565 and A8);
ETC1 output has not.

### 4.10 Animation groups (inside CMDL), revision-7 layout

Group (0x24): type 0x80000000, flags (1 = transform), name ptr, member type, members
(count, DICT ptr), blend operations (count, ptr to u32 array), evaluation timing (0 before
world update, 1 after scene cull).

| Group | flags | member type | blend operations | timing |
|---|---|---|---|---|
| `SkeletalAnimation` | 1 | 1 | [8] | 1 |
| `VisibilityAnimation` | 0 | 3 | [0] | 0 |
| `MaterialAnimation` | 0 | 2 | [3, 7, 5, 2] | 1 |

Blend operation meaning, inferred from which members use them: 0 bool, 2 float, 3 RGBA
colour, 5 vector2, 7 texture, 8 transform.

Member, common part (0x28 bytes):

| Offset | Field |
|---|---|
| 0x00 | type flag: 0x00080000 mesh node visibility, 0x01000000 mesh, 0x02000000 texture sampler, 0x04000000 blend operation, 0x08000000 material colour, 0x10000000 model, 0x20000000 texture mapper, 0x40000000 bone, 0x80000000 texture coordinator |
| 0x04 | path ptr (also the DICT key) |
| 0x08 | owner name ptr: material name, bone name, mesh index as decimal text; 0 for the model |
| 0x0C | sub-object path ptr: `MaterialColor`, `TextureMappers[0]`, `TextureMappers[0].Sampler`, `TextureCoordinators[0]`, `FragmentOperation.BlendOperation`; 0 for bone, model, mesh |
| 0x10 | offset of the value inside its struct |
| 0x14 | size of the value in bytes |
| 0x18 | index into the group's blend operations |
| 0x1C | object type: 0 bone, 1 material colour, 2 texture sampler, 3 texture mapper, 4 blend operation, 5 texture coordinator, 6 model, 7 mesh, 8 mesh node visibility |
| 0x20 | member index within the object (emission 0, ambient 1, diffuse 2, specular0 3, specular1 4, constant0..5 5..10; scale 0, rotate 1, translate 2) |
| 0x24 | 0 (run-time pointer) |

Type-specific tail: every member ends with the object type repeated. Bone, material
colour, blend operation: name ptr, object type. Texture sampler, mapper, coordinator:
material name ptr, index, object type. Mesh: mesh index, object type. Model: object type.
A light's `Transform` member has the repeated type; its other members do not. (pycgfx
writes these members shorter.)

Example values: bone: offset 0, size 0, blend 0. `MaterialColor.Ambient`: offset 0x10,
size 0x10, blend 0. `TextureCoordinators[0].Scale`: offset 0x10, size 8, blend 2.
`.Rotate`: 0x18, 4, blend 3. `.Translate`: 0x1C, 8, blend 2. `TextureMappers[0].Texture`:
8, 4, blend 1. `IsVisible` (model): 0xD4, 1. `Meshes[0].IsVisible`: 0x24, 1.

The 0x08, 0x0C and 0x14 words do not exist in EFE's newer-revision layout.

The system banners, bannertool's template and our builder create all three groups for
every model, with one skeletal member per bone, one visibility member per mesh plus the
model's, and the full list of material members per material, whether or not they are
animated.

### 4.11 CANM, revision 0x05000000

Header (0x28): `CANM` (no type word before the magic), revision, name ptr, target
animation group name ptr, loop mode u32 (0 once, 1 loop), frame count f32, members (count,
DICT ptr), user data (count, ptr).

Member: flags u32, path ptr (equals an animation group member path), two more string ptrs
(for a skeletal member the second equals its path), primitive type u32, then the payload.
Primitive types: 0 float, 1 int, 2 bool, 3 vector2, 4 vector3, 5 transform, 6 RGBA,
7 texture, 8 baked (quaternion) transform, 9 matrix transform.

Payload slots are one word each: a pointer to a float curve, or an f32 constant if the
slot's "constant" flag is set, or 0 if its "not present" flag is set.

| Type | Slots | Constant flags | Not-present flags |
|---|---|---|---|
| Float | 1 | bit 0 | bit 1 |
| Vector2 | x, y | bits 0, 1 | bits 2, 3 |
| RGBA | r, g, b, a | bits 0..3 | bits 4..7 |
| Transform | scale x y z, rotation x y z, one unused word, translation x y z (10 words) | bits 6, 7, 8 / 9, 10, 11 / 13, 14, 15 | bits 16, 17, 18 / 19, 20, 21 / 23, 24, 25 |

Transform flag bits 0..5: identity, rotation-and-translation zero, scale one, scale
uniform, rotation zero, translation zero. Bit 22 (the unused word between rotations and
translations) is set in the system banners. Rotation is Euler XYZ in radians; unwrap
successive keys to avoid 2 pi jumps.

Float curve: start frame f32, end frame f32, pre-repeat u8, post-repeat u8, pad u16,
flags u32 (4 for a curve with one segment), segment count u32, segment ptrs.

Segment: start frame f32, end frame f32, flags u32 = `single value (bit 0) | interpolation
<< 2 | quantisation << 5`, then either one f32 (single value) or: key count u32, speed f32
= 1 / (end - start), then for quantised formats the scales (see the table), then the keys.

Interpolation: 0 step, 1 linear, 2 cubic Hermite.

| Quantisation | Key layout | Scales present |
|---|---|---|
| 0 Hermite128 | frame f32, value f32, in slope f32, out slope f32 | none |
| 1 Hermite64 | u32: frame in bits 0..11, value in bits 12..31; in slope s16, out slope s16 (8 fractional bits) | value scale, value offset, frame scale |
| 2 Hermite48 | frame u8, value u16, 24 bits: in slope 12 bits, out slope 12 bits (5 fractional bits) | value scale, value offset, frame scale |
| 3 UnifiedHermite96 | frame f32, value f32, slope f32 | none |
| 4 UnifiedHermite48 | frame u16 (5 fractional bits), value u16, slope s16 (8 fractional bits) | value scale, value offset (two, not three) |
| 5 UnifiedHermite32 | frame u8, 24 bits: value 12 bits, slope 12 bits (5 fractional bits) | value scale, value offset, frame scale |
| 6 StepLinear64 | frame f32, value f32 | none |
| 7 StepLinear32 | u32: frame bits 0..11, value bits 12..31 | value scale, value offset, frame scale |

Decoded value = stored x value scale + value offset; decoded frame = stored x frame scale.
The system banners use hermite keys (quantisation 3 or 0, interpolation 2), and so does our
builder.

### 4.12 Lights and scene (only for lit materials)

Fragment light (type 0x400000A2, `CFLT`, revision 0x06000000): node header like CMDL up to
the world matrix, then enabled, light type, ambient/diffuse/specular0/specular1 as f32 and
as bytes, direction, LUT pointers, attenuation values. Scene environment (type 0x00800000,
`CENV`): cameras, light sets, fogs as counted lists of name references. Lookup tables
(type 0x04000000, `LUTS`): named 256-entry tables stored as PICA command words. GBATEK's
CFLT and CENV pages describe the same structures from official banners.

## 5. Verified behaviour

### Against the system banners

`tools/cgfx.py check` reads all 154 CGFX files from the 13 system banners (with their
language variants) with every byte of the structure block accounted for, and writes each
one again byte for byte, both with blocks at their original places and laid out afresh
from the object tree. So the layout rules in 4.2 are the ones Nintendo's tool follows, not
only ones the console tolerates.

`tools/banner_selftest.py` rebuilds the Activity Log banner from plain contents (vertices,
textures, bone placements, keys, material settings as our builder expresses them): every
value equals the original except the shapes' bounding boxes, which nothing reads.

What every system banner has in common, and what the builder therefore emits: one model
`COMMON` with a skeleton; one rigid mesh per bone (one primitive set, one bone, skinning
0; no system banner uses skinning); float positions, normals and texture coordinates in
one interleaved buffer; u8 or u16 indices; mesh node name = bone name; the three animation
groups listing every bone, mesh and material value; skeletal animation `COMMON`, 600
frames (one banner 149), loop on, hermite keys. Largest: 2,300 triangles, 12 meshes,
206 KB. The eShop banner is 504 triangles and three small textures.

### On the HOME Menu

Measured with test scenes built by `tools/banner_scene.py` and installed into the HOME
Menu running in an emulator (`emulator/try_banner.py`). Not yet tested on a console; the
files follow Nintendo's own structure value for value and the HOME Menu code is the real
one, so the remaining risk is in the GPU and timing, not in parsing.

| Question | Result |
|---|---|
| Does a scene built from nothing load and animate | yes (a title card and two boxes spinning and bobbing with soft shadows), no error |
| Camera | as documented: 1 unit = 10 pixels at z = 0, screen centre at y = 1 |
| What the HOME Menu does to the model | turns it about the vertical axis through the origin, one turn per 600 frames, in step with the animation: a bone animated `ry` 0 to +2 pi over 600 frames stands still on screen, across loops and after moving the cursor away and back. The turn takes 600 frames whatever the animation's length, so animations should be 600 frames or a multiple |
| Holding things still | either a billboard bone, or a bone turning +2 pi per 600 frames with everything that must not orbit under it. The second method assumes the console advances the turning and the animation together, as the emulator does; a billboard bone does not depend on that |
| Billboard bones | Y-axial, World and Screen all cancel the turning for their own mesh. Children of a billboard bone do not inherit it. The bone's own `ry` is ignored in all three; `rx`/`rz` tilt the object but, for Y-axial and World, about axes that turn with the scene |
| Texture orientation | the image's top row first in memory is v = 1: text reads correctly |
| Shading without lights | works: an alpha texture looked up by the direction the surface faces (the Activity Log's method) |
| Lights in the file | work: a directional light, scene environment and lighting table give diffuse shading and a highlight; the light stays fixed relative to the viewer while the scene turns |
| Stereoscopic depth | at full slider, an object at z = 0 is 4 pixels behind the screen, z = +10 is 4 in front, z = -10 is 10 behind, z = -25 is 15 behind, z = +16 is 12 in front. The screen plane is near z = +5; the Activity Log's title card sits at z = 5, the eShop's logo at z = 9 |

## 6. bannertool's built-in template

bannertool's `makebanner -i image.png` copies a 0x1580-byte CGFX header
(`source/3ds/data.h`, `BANNER_CGFX_HEADER`) and appends the 256 x 128 image converted to
RGBA4 tiles (0x10000 bytes); the header already states file size 0x11580 and IMAG size
0x10088. It is the smallest banner known to work: CGFX revision 0x05000000, 15 slots,
1 model and 1 texture, no camera, light, scene or CANM. The model `COMMON` (revision
0x07000000, with skeleton) has the three animation groups, one mesh, one material and one
shape. The skeleton is one bone `banner`, flags 0x1DC, translation (0, 1, 0), billboard
mode 5 (YAxial). The shape is one quad, 4 vertices with stride 20 (position f32 x 3,
texcoord0 f32 x 2) at (-13, -7.5), (13, -7.5), (-13, 5.5), (13, 5.5) with UVs (0, 0),
(1, 0), (0, 1), (1, 1), indices `0 1 2 1 3 2` as u8. The material `mt_banner` is unlit
(flags 0), render layer 1, depth test on and depth write off, alpha blending, one texture
mapper referencing `COMMON1` with linear filtering and repeat, shader `DefaultShader`,
combiner stage 0 = primary colour x texture0. The texture is 256 x 128 RGBA4, 1 mip level.

The array is part of bannertool's MIT-licensed source, but the bytes look like the output
of Nintendo's tool chain for a one-quad scene. It is a reference to read; our generator
builds its own file.

## 7. Pitfalls reported by banner authors

| Symptom | Cause |
|---|---|
| HOME Menu freezes when the title is selected; emulator is fine | soft skinning left in the CGFX (bone index/weight streams, skinning mode not 0) |
| Sound beeps or clicks, or the model fails to load | BCWAV longer than 3 s or not 2 channels |
| Model does not load | decompressed CGFX over 0x80000 |
| Texture wrong | RGBA8 texture |
| Crash, sometimes | `TextureMappers[i].Sampler.BorderColor` animation member |
| Vertex data unusable | u16 vertex components; use s16 |
| Back faces missing although culling is off | material cull mode ignored by the HOME Menu |
| Model nearly white when facing the camera | strong specular stage from a glTF conversion |
| Billboard plane becomes a thin line | billboard bone has its own rotation; bake the transform into the vertices |
| Billboard part rotates with the model | it is a child of the rotating bone |
| Long pause between loops | exported animation longer than the keyed range |
| Old banner still shown after reinstall | HOME Menu banner cache; reboot |
| Transparent layers sorted wrongly | draw order follows bone order; put translucent parts last |

## References

- 3dbrew: [CBMD](https://www.3dbrew.org/wiki/CBMD), [CGFX](https://www.3dbrew.org/wiki/CGFX),
  [BCWAV](https://www.3dbrew.org/wiki/BCWAV), [SMDH](https://www.3dbrew.org/wiki/SMDH),
  [3DS Userland Flaws](https://www.3dbrew.org/wiki/3DS_Userland_Flaws) (banner size checks)
- GBATEK, [3DS files: video banner CBMD](http://problemkaputt.de/gbatek-3ds-files-video-banner-cbmd.htm)
  and the CGFX sub-pages (header/DICT, CMDL, TXOB, LUTS, SHDR, CCAM, CFLT, CENV, CANM)
- bannertool (MIT): forks [epicpkmn11/bannertool](https://github.com/epicpkmn11/bannertool) and
  [carstene1ns/bannertool](https://github.com/carstene1ns/bannertool); `cbmd.cpp`, `cwav.cpp`,
  `lz11.cpp`, `cmd.cpp`, `data.h`
- [skyfloogle/pycgfx](https://github.com/skyfloogle/pycgfx): glTF to CGFX writer made for
  banners, with the HOME Menu camera as `banner-camera.gltf`; no licence file, so read only
- [KillzXGaming/SPICA](https://github.com/KillzXGaming/SPICA) (Unlicense): full CGFX object
  model, animation and material writers, hash recipes, key-frame quantisation
- [Gericom/EveryFileExplorer](https://github.com/Gericom/EveryFileExplorer): CGFX reader and
  writer (newer revisions), patricia tree generator, hash computation
- makerom (Project_CTR, MIT): `ncch.c`, `exefs.c`
- Community reports: GBAtemp threads 683412 (pycgfx banner tutorial), 671763 (pycgfx
  release), 433783 (EFE tutorial), 569962, 683356
