# 3DS HOME Menu themes: file formats and install mechanics

Reference for the theme side of the app: the `body_LZ.bin` format, the theme extdata the
HOME Menu reads, the fields the app patches to install a theme, BCSTM playback, SMDH and
the layout of community theme zips. All multi-byte fields are little-endian unless stated.
Offsets are hex. "u32" means unsigned 32-bit LE.

Where a fact is not on 3dbrew or in libctru it comes from the independent projects listed
under [References](#references), described here in our own words. Statements that no source
confirms are marked "unconfirmed".

Contents: [1 body_LZ.bin](#1-body_lzbin), [2 Theme extdata](#2-theme-extdata),
[3 SaveData.dat](#3-home-menu-extdata-savedatadat), [4 libctru access](#4-accessing-extdata-with-libctru),
[5 BCSTM](#5-bcstm-theme-bgm), [6 SMDH](#6-smdh-infosmdh), [7 Theme zips](#7-community-theme-zip-contents),
[Install recipes](#install-recipes), [References](#references)

## 1. body_LZ.bin

### 1.1 Compression (LZ11, type 0x11)

`body_LZ.bin` is one LZ11 stream holding the header, colour blocks, textures and optional
sound effects.

| Offset | Size | Meaning |
|---|---|---|
| 0x0 | 1 | Type byte, must be `0x11` |
| 0x1 | 3 | Decompressed size, 24-bit LE |
| 0x4 | 4 | Only present if the 24-bit size is 0: 32-bit LE decompressed size (DSDecmp convention) |

Body: repeated groups of one flag byte followed by up to 8 items. Flag bits are consumed
MSB first; bit 0 = literal byte, bit 1 = back-reference. Back-reference encodings are selected
by the high nibble of the first byte `b0`:

| `b0 >> 4` | Bytes | Length | Displacement |
|---|---|---|---|
| 0 | 3 (`b0 b1 b2`) | `((b0 & 0xF) << 4 \| b1 >> 4) + 0x11` | `((b1 & 0xF) << 8 \| b2) + 1` |
| 1 | 4 (`b0 b1 b2 b3`) | `((b0 & 0xF) << 12 \| b1 << 4 \| b2 >> 4) + 0x111` | `((b2 & 0xF) << 8 \| b3) + 1` |
| 2..15 | 2 (`b0 b1`) | `(b0 >> 4) + 1` | `((b0 & 0xF) << 8 \| b1) + 1` |

Copy `length` bytes from `out_pos - displacement`, byte by byte (overlap is allowed).
Maximum displacement is 0x1000, maximum length 0x10110.

libctru ships a decompressor: `#include <3ds/util/decompress.h>`,
`decompress_LZ11(out, outSize, NULL, in + 4, inSize - 4)` for raw data after the header, or
`decompress()` which parses the header itself. `decompressHeader()` treats bit 7 of the
type byte as "8-byte header" (tex3ds convention), not the "size == 0" convention above; for
theme bodies the 24-bit size is always enough, so either works.

Size limits:

| Limit | Value |
|---|---|
| Compressed size | <= 0x150000 (1,376,256) bytes. Must fit `BodyCache.bin`, which is exactly that size |
| Decompressed size | <= 0x150000 bytes. HOME Menu decompresses into a 0x2A0000 heap buffer: output at +0 (0x150000), compressed input at +0x150000 (0x150000) |

HOME Menu before 10.2.0 did not check the output size (the "themehax" overflow); 10.2.0
fixed it. A manager should reject any body whose header size exceeds 0x150000 or is below
0xC4.

### 1.2 Header (decompressed), 0xC4 bytes

File size is aligned to 0x10. The first data block normally starts at 0xD0. All offsets are
from the start of the decompressed data.

| Offset | Size | Field | Values |
|---|---|---|---|
| 0x00 | u32 | Version | Must be 1 |
| 0x04 | u8 | Unknown. usagirei's editor reads it as "preferred row count" 0..6 and always writes 0 | |
| 0x05 | u8 | BGM flag | 0 = ignore bgm.bcstm, non-zero = play it |
| 0x06 | 2 | Padding | 0 |
| 0x08 | u32 | Unused | Normally 0 |
| 0x0C | u32 | Top draw type | 0 none, 1 solid colour, 2 solid colour + textured squares, 3 texture. Must be < 4 |
| 0x10 | u32 | Top frame type (draw type 3) | 0 scroll (normal speed), 1 static, 3 slow scroll. 2 is invalid |
| 0x14 | u32 | Offset: top solid-colour block | 5 bytes (draw type 1) or 7 bytes (draw type 2) |
| 0x18 | u32 | Offset: top texture | Draw type 3: wallpaper. Draw type 2: 64x64 A8 pattern (moving) |
| 0x1C | u32 | Offset: top extra texture | Draw type 2 only, optional (0 = absent): 64x64 A8 pattern (static) |
| 0x20 | u32 | Bottom draw type | 0 none, 1 solid colour, 2 invalid, 3 texture. Must be < 4 |
| 0x24 | u32 | Bottom frame type (draw type 3) | 0 scroll, 1 static, 2 flipbook 0>1>2>0, 3 slow scroll, 4 flipbook 0>1>2>1>0 |
| 0x28 | u32 | Offset: bottom texture | |
| 0x2C | u32 | Flag: cursor colours | 0/1 |
| 0x30 | u32 | Offset: cursor block | 0xC bytes |
| 0x34 | u32 | Flag: 3D folder colours | 0/1 |
| 0x38 | u32 | Offset: 3D folder block | 0xC bytes |
| 0x3C | u32 | Flag: folder textures | 0/1, enables 0x40 and 0x44 |
| 0x40 | u32 | Offset: closed-folder texture | 128x64 BGR888 |
| 0x44 | u32 | Offset: open-folder texture | 128x64 BGR888 |
| 0x48 | u32 | Flag: file colours | 0/1 |
| 0x4C | u32 | Offset: file block | 0xD bytes |
| 0x50 | u32 | Flag: file (icon border) textures | 0/1, enables 0x54 and 0x58 |
| 0x54 | u32 | Offset: large file texture | 64x128 BGR888 |
| 0x58 | u32 | Offset: small file texture | 32x64 BGR888 |
| 0x5C | u32 | Flag: arrow-button colours | 0/1 |
| 0x60 | u32 | Offset: arrow-button block | 0xD bytes |
| 0x64 | u32 | Flag: arrow colours | 0/1 |
| 0x68 | u32 | Offset: arrow block | 0x9 bytes |
| 0x6C | u32 | Flag: open/close button colours | 0/1, enables 0x70 and 0x74 |
| 0x70 | u32 | Offset: open-button block | 0x20 bytes |
| 0x74 | u32 | Offset: close-button block | 0x20 bytes |
| 0x78 | u32 | Game-text draw type | 0 default, 1 coloured, 2 hidden. Must be < 3 |
| 0x7C | u32 | Offset: game-text block | 0xD bytes |
| 0x80 | u32 | Flag: bottom solid (inner) colours | 0/1 |
| 0x84 | u32 | Offset: bottom solid block | 0xD bytes. Used when bottom draw type = 1 |
| 0x88 | u32 | Flag: bottom outer colours | 0/1 |
| 0x8C | u32 | Offset: bottom outer block | 0x9 bytes. Used when bottom draw type = 1 |
| 0x90 | u32 | Flag: folder-background colours | 0/1 |
| 0x94 | u32 | Offset: folder-background block | 0xD bytes |
| 0x98 | u32 | Flag: folder back-arrow colours | 0/1 |
| 0x9C | u32 | Offset: folder-arrow block | 0x20 bytes |
| 0xA0 | u32 | Flag: bottom corner buttons | 0/1 |
| 0xA4 | u32 | Offset: bottom-corner block | 0x15 bytes |
| 0xA8 | u32 | Flag: top corner buttons | 0/1 |
| 0xAC | u32 | Offset: top-corner block | 0xC bytes |
| 0xB0 | u32 | Flag: demo text | 0/1 |
| 0xB4 | u32 | Offset: demo-text block | 0x6 bytes |
| 0xB8 | u32 | Flag: sound effects | 0/1 |
| 0xBC | u32 | SFX section size | Must be <= 0x2DC00 (187,392 bytes) |
| 0xC0 | u32 | Offset: SFX section | |

Reader rule: only dereference an offset when its flag (or draw type) enables it, the offset
is non-zero, and `offset + block size <= decompressed size`. Editors may write offsets for
disabled blocks, and may leave 0 for absent ones.

### 1.3 Textures

| ID | Selected by | Format | Stored size | Visible size | Bytes |
|---|---|---|---|---|---|
| 0 | top draw 3, frame 1 | RGB565 | 512x256 | 412x240 | 0x40000 |
| 1 | top draw 3, frame 0 or 3 | RGB565 | 1024x256 | 1008x240 | 0x80000 |
| 2 | bottom draw 3, frame 1 | RGB565 | 512x256 | 320x240 | 0x40000 |
| 3 | bottom draw 3, frame 2 or 4 | RGB565 | 1024x256 | 960x240 (three 320x240 frames side by side) | 0x80000 |
| 4 | bottom draw 3, frame 0 or 3 | RGB565 | 1024x256 | 1008x240 | 0x80000 |
| 5 | top draw 2, offset 0x18 | A8 | 64x64 | 64x64, rotated 90 degrees, moving pattern | 0x1000 |
| 6 | top draw 2, offset 0x1C | A8 | 64x64 | 64x64, rotated 90 degrees, static pattern | 0x1000 |
| 7 | folder flag, 0x40 | BGR888 | 128x64 | 74x64 | 0x6000 |
| 8 | folder flag, 0x44 | BGR888 | 128x64 | 82x64 | 0x6000 |
| 9 | file flag, 0x54 | BGR888 | 64x128 | 36x72 | 0x6000 |
| 10 | file flag, 0x58 | BGR888 | 32x64 | 25x50 | 0x1800 |

Pixel layout: all textures are in the 3DS native tiled order.

- The image is split into 8x8 tiles. Tiles are stored left to right, then top to bottom.
  The first tile is the top-left of the picture.
- Within a tile, pixel index `i` (0..63) is Morton (Z-order) coded: `x` = bits 0,2,4 of `i`,
  `y` = bits 1,3,5 of `i`. So indices 0,1,2,3 are (0,0),(1,0),(0,1),(1,1).
- Linear pixel number `n`: `tile = n / 64`, `tx = tile % (W/8)`, `ty = tile / (W/8)`,
  then `(x, y) = (tx*8 + mx, ty*8 + my)`.
- RGB565: u16 LE, `R = v >> 11`, `G = (v >> 5) & 0x3F`, `B = v & 0x1F`. This is `GPU_RGB565`.
- BGR888: three bytes in order B, G, R. This is the memory layout of `GPU_RGB8`.
- A8: one byte per pixel, `GPU_A8`.

GPU upload: the data is already in PICA layout with power-of-two sizes, so it can be
passed straight to `C3D_TexInit(&tex, W, H, GPU_RGB565)` + `C3D_TexUpload(&tex, data)`
with no conversion. tex3ds stores images in the same order (top rows first, no vertical
flip) and gives the top edge v = 1.0, so the visible region maps to:

`left = 0`, `right = visW / W`, `top = 1.0`, `bottom = 1.0 - visH / H`

For example the static top wallpaper: `right = 412/512`, `bottom = 1 - 240/256 = 0.0625`.
These four values are exactly a `Tex3DS_SubTexture` for citro2d.

What to show in a preview:

| Case | Preview |
|---|---|
| Top frame 1 | 412x240 from top-left. The screen is 400 wide; the extra 12 px are for the 3D parallax (Kame Editor wiki). Cropping 6 px each side is unconfirmed |
| Top frame 0/3 | 1008x240 scrolls with the bottom screen. Show the first 412 (or 400) columns, or animate |
| Bottom frame 1 | 320x240 from top-left |
| Bottom frame 0/3 | 1008x240 scrolling. Kame Editor notes the bottom image is offset left by 45 px so it lines up with a top texture of the same frame type |
| Bottom frame 2/4 | Three frames at x = 0, 320, 640. Show frame 0 |
| Top draw 1/2 | Fill with the solid colour block (1.4). Draw type 2 overlays the A8 patterns |
| Bottom draw 1 | Fill with the bottom outer/inner colours (1.4) |
| Draw type 0 | HOME Menu default look; nothing in the file |

### 1.4 Colour blocks

"RGB" = 3 bytes R, G, B. "RGBA" = 4 bytes R, G, B, A. Byte and field order follow
usagirei's editor; sizes and meanings follow 3dbrew. Where the two describe a field
differently both readings are given.

Top solid colour (offset at 0x14; 5 bytes for draw type 1, 7 bytes for draw type 2):

| Byte | Field |
|---|---|
| 0-2 | Background colour RGB |
| 3 | Gradient strength (0 = none, max = fade to white at the top) |
| 4 | Opacity of the floating squares / pattern texture |
| 5 | Draw type 2 only: second pattern opacity |
| 6 | Draw type 2 only: gradient colour |

Value range is unconfirmed: 3dbrew describes the first 4 bytes as RGBA8888 with 0..255
ranges; usagirei's editor treats all seven bytes as percentages 0..100 and rescales by
255/100 on load.

Cursor (flag 0x2C, offset 0x30, 0xC bytes):

| Bytes | Field |
|---|---|
| 0-2 | Border / dark |
| 3-5 | Main |
| 6-8 | Unknown ("light", possibly unused) |
| 9-11 | Expanded glow |

3D folder (flag 0x34, offset 0x38, 0xC bytes):

| Bytes | Field |
|---|---|
| 0-2 | Shadowed (dark) colour |
| 3-5 | Main colour |
| 6-11 | Unknown (light, shadow; possibly unused) |

Files (flag 0x48, offset 0x4C, 0xD bytes). Colours the DSiWare cart icon and the file
graphic inside folders.

| Bytes | Field |
|---|---|
| 0-2 | Bottom shadow (dark) |
| 3-5 | Main |
| 6-8 | Top highlight (light) |
| 9-12 | Unknown (shadow RGBA) |

Arrow buttons (flag 0x5C, offset 0x60, 0xD bytes). The bottom-screen buttons that carry
the scroll arrows.

| Bytes | Field |
|---|---|
| 0-2 | Downward sheen (dark) |
| 3-5 | Main |
| 6-8 | Leftward sheen (light) |
| 9-12 | Unknown (shadow RGBA) |

Arrows (flag 0x64, offset 0x68, 0x9 bytes):

| Bytes | Field |
|---|---|
| 0-2 | Edge / border |
| 3-5 | Unpressed |
| 6-8 | Pressed |

Open button / close button (flag 0x6C; open at 0x70, close at 0x74; 0x20 bytes each)
and folder back arrow (flag 0x98, offset 0x9C, 0x20 bytes) share one layout:

| Bytes | Field |
|---|---|
| 0-3 | float32 LE, text (or arrow) shadow position ("unknown" on 3dbrew) |
| 4-6 | Button background, pressed (dark) |
| 7-9 | Button background, unpressed (main) |
| 10-12 | Border (light) |
| 13-16 | Shadow RGBA (unknown on 3dbrew) |
| 17-19 | Glow (unknown on 3dbrew) |
| 20-22 | Text / arrow shadow |
| 23-25 | Text / arrow, unpressed |
| 26-28 | Text / arrow, pressed |
| 29-31 | Padding |

Game text (draw type at 0x78, offset 0x7C, 0xD bytes). The title bubble above icons at
maximum zoom.

| Bytes | Field |
|---|---|
| 0-2 | Background |
| 3-5 | Unknown (light) |
| 6-9 | Unknown (shadow RGBA) |
| 10-12 | Text colour |

Bottom solid / inner (flag 0x80, offset 0x84, 0xD bytes) and folder background
(flag 0x90, offset 0x94, 0xD bytes) share one layout:

| Bytes | Field |
|---|---|
| 0-2 | Shadow at the top of an empty slot (dark) |
| 3-5 | Background (main) |
| 6-8 | Border / shadow for the rest of an empty slot (light) |
| 9-12 | Shadow/glow around the folder area, RGBA |

Bottom outer (flag 0x88, offset 0x8C, 0x9 bytes):

| Bytes | Field |
|---|---|
| 0-2 | Bottom stripes (dark) |
| 3-5 | Main background |
| 6-8 | Subtle edge glow (light) |

Bottom corner buttons (flag 0xA0, offset 0xA4, 0x15 bytes). Icon-resize and settings buttons.

| Bytes | 3dbrew reading | usagirei reading |
|---|---|---|
| 0-2 | Left box shadow (subtle) | Base dark |
| 3-5 | Background | Base main |
| 6-8 | Border | Base light |
| 9-11 | Icon gradient colour 1 | Base shadow |
| 12-14 | Icon gradient colour 2 | Icon main |
| 15-17 | Pressed colour | Icon light |
| 18-20 | Right box shadow (subtle) | Icon text |

The first three positions agree. The last four are unconfirmed.

Top corner buttons (flag 0xA8, offset 0xAC, 0xC bytes). The "press to activate camera"
overlay.

| Bytes | Field |
|---|---|
| 0-2 | Background |
| 3-5 | Unknown (light) |
| 6-8 | Unknown (shadow) |
| 9-11 | Text colour |

Demo text (flag 0xB0, offset 0xB4, 0x6 bytes):

| Bytes | Field |
|---|---|
| 0-2 | Background of the "demo uses remaining" message |
| 3-5 | Text colour |

### 1.5 Sound effects section

Enabled by the u32 at 0xB8; size at 0xBC (<= 0x2DC00); offset at 0xC0.

| Offset in section | Size | Meaning |
|---|---|---|
| 0x0 | 4 | Unknown |
| 0x4 | 4 | Unknown |
| 0x8 | ... | Audio entries |

Each entry is an 8-byte prefix followed by a BCWAV (magic `CWAV`): u32 CWAV size (also the
distance to the next entry), then a volume byte 0..100 at prefix byte 4, other bytes zero.
Some entries carry extra unknown data (0x10 bytes before one entry, 0x2C bytes inside
another). Slot meanings per 3dbrew: 0 cursor move, 1 launch, 2 certain buttons, 3 cancel,
4 cursor at screen edge, 5 page scroll, 6 folder buttons. Kame Editor exposes eight slots
(cursor, launch, folder, close, frame 0/1/2, open lid). The entry layout is only partly
documented; a manager does not need to parse it.

### 1.6 Built-in themes

HOME Menu's own themes live at RomFS `/theme/<Color>_LZ.bin` in the same format.

## 2. Theme extdata

### 2.1 Extdata IDs

SD extdata, extdata-ID high word = 0.

| Region | `CFG_Region` | Theme extdata ID | HOME Menu extdata ID |
|---|---|---|---|
| JPN | 0 | 0x000002CC | 0x00000082 |
| USA | 1 | 0x000002CD | 0x0000008F |
| EUR (and AUS consoles) | 2 | 0x000002CE | 0x00000098 |
| CHN | 4 | none | not documented |
| KOR | 5 | none (see below) | not documented |
| TWN | 6 | none | not documented |

Themes exist only on JPN/USA/EUR. KOR HOME Menu contains the theme code but uses extdata ID
0 for it, which is invalid, and has no theme-settings button. CHN HOME Menu stopped at v7.0
and TWN has no settings menu.

### 2.2 Files

The archive root seen by `ARCHIVE_EXTDATA` is the extdata's `/user` directory, so the paths
below are used as written.

| Path | Fixed size | Introduced | Contents |
|---|---|---|---|
| `/ThemeManage.bin` | 0x800 | 9.0.0 | Management info (2.3). Wrong size makes HOME Menu error out |
| `/BodyCache.bin` | 0x150000 | 9.0.0 | The compressed `body_LZ.bin` of the single active theme, zero padded |
| `/BgmCache.bin` | 0x337000 (3,371,008) | 9.0.0 | The `bgm.bcstm` of the single active theme, zero padded |
| `/BodyCache_rd.bin` | 0xD20000 (10 x 0x150000) | 9.3.0 | Shuffle: slot `i` holds a compressed body at offset `i * 0x150000` |
| `/BgmCache_00.bin` .. `/BgmCache_09.bin` | 0x337000 each | 9.3.0 | Shuffle: BGM for slot `i` |
| `nsalist` | - | 9.0.0 | Theme Shop list from SpotPass. Not used by a manager |

The cache files hold the raw file bytes starting at offset 0, with the true length recorded
in ThemeManage.bin. The body is stored still LZ11-compressed. `BodyCache.bin` and
`BgmCache.bin` are all-zero when no theme is selected. Extdata files cannot be resized after
creation, so a shorter write leaves the old tail in place; HOME Menu reads only the recorded
length, but writing zero padding to the full fixed size keeps the files clean. When
installing without BGM, write 0x337000 zero bytes and record BGM size 0.

The shuffle files may not exist if the user never used shuffle. Themely handles a failed
open by `FSUSER_DeleteFile` then `FSUSER_CreateFile` with the fixed size, then reopening.

Whether HOME Menu enforces the size of `BodyCache_rd.bin` and `BgmCache_NN.bin` the way it
does for the three base files is unconfirmed.

### 2.3 ThemeManage.bin (0x800 bytes)

| Offset | Size | 3dbrew description | Value written for a custom theme |
|---|---|---|---|
| 0x00 | u32 | Unknown, normally 1 | 1 |
| 0x04 | u32 | Unknown, normally 0 | 0 |
| 0x08 | u32 | Size of cached body_LZ.bin | Single: body size. Shuffle: 0 |
| 0x0C | u32 | Size of cached bgm.bcstm | Single: BGM size, or 0 for none. Shuffle: 0 |
| 0x10 | u32 | Unknown (1 when no theme is in use) | 0xFF |
| 0x14 | u32 | Unknown | 1 |
| 0x18 | u32 | DLC content index of the selected theme | 0xFF |
| 0x1C | u32 | Unknown, "usually 0x200 when theme-cache is used" | 0x200 |
| 0x20-0x337 | | Normally zero | leave as read |
| 0x338 | u32 x 10 | not on 3dbrew | Shuffle: body size of slot 0..9 (0 for unused). Single: zero |
| 0x360 | u32 x 10 | not on 3dbrew | Shuffle: BGM size of slot 0..9 (0 for none). Single: zero |
| 0x388-0x7FF | | Normally zero | leave as read |

The single-theme values are what yellows8's extdata tool, CHMM2 and Themely all write. The
shuffle arrays at 0x338 and 0x360 come from CHMM2 and Themely; yellows8's shufflehax note
confirms that HOME Menu reads shuffle body sizes from ThemeManage. With no theme in use,
only 0x00 = 1 and 0x10 = 1 are non-zero.

Procedure: read the existing 0x800 bytes, patch the fields above, write all 0x800 bytes
back. Clear all ten entries of each array on a single install. The meaning of 0x10, 0x14
and 0x1C, and whether anything else in 0x20-0x7FF matters, is unconfirmed.

## 3. HOME Menu extdata SaveData.dat

Extdata IDs are in the table in 2.1. Path `/SaveData.dat`. Size 0x2DA0 (0x2CB0 if the
extdata was created before 4.0.0 and never recreated; the theme fields still fall inside).
Format version byte at 0x0 is 4 as of 9.3.0+.

### 3.1 Theme fields

| Offset | Size | Field |
|---|---|---|
| 0x13B8 | 8 | Theme entry for the regular (single) theme |
| 0x13C0 | 8 x 10 | Theme entries for shuffle slots 0..9 |
| 0x141B | u8 | 0 = single theme, 1 = shuffle |

Theme entry (8 bytes):

| Offset | Size | Field |
|---|---|---|
| 0x0 | u32 | Theme index. DLC: content index. Built-in: index into HOME Menu's table |
| 0x4 | u8 | Low 8 bits of the DLC title ID the theme came from. Normally 0. Must be < 10 |
| 0x5 | u8 | Theme type, 0..5: 0 none, 1 built-in (index must be < 9), 2 loaded from SD cache / DLC (index must be non-zero), 3..5 undocumented |
| 0x6 | u8 | Normally 0 |
| 0x7 | u8 | Normally 0 |

The loader treats every type >= 2 the same, except that the "is this DLC installed" checks
run only for type 2. With type 2 and no matching DLC, HOME Menu resets the entry to "no
theme" on sleep, shutdown screen and app launch. Type 3 skips that check and persists.
What types 4 and 5 do is unconfirmed.

### 3.2 Values to write

Single custom theme:

| Offset | Bytes |
|---|---|
| 0x141B | `00` |
| 0x13B8..0x13BF | `FF 00 00 00 00 03 00 00` (clear all 8, then index = 0xFF, type = 3) |

Shuffle with N themes (2 <= N <= 10):

| Offset | Bytes |
|---|---|
| 0x141B | `01` |
| 0x13C0 + 8*i, i < N | `i 00 00 00 00 03 00 00` (index = slot number, type = 3) |
| 0x13C0 + 8*i, i >= N | eight zero bytes |
| 0x13B8..0x13BF | CHMM2 writes eight zeros. Themely writes `FF 00 00 00 00 03 00 00`. Both shipped |

Read the whole file, patch, write the whole file back with the size reported by
`FSFILE_GetSize`. Do not try to resize it: `FSFILE_SetSize` does not work on extdata.

### 3.3 If the theme extdata does not exist

`FSUSER_OpenArchive` on the theme extdata fails when the user has never used the theme
menu. The generic FS "object does not exist" result is 0xC8804478; which exact code comes
back here is unconfirmed.

HOME Menu creates the extdata itself: HOME Menu settings > Change Theme > pick any theme
other than the default and let the console create the data. A manager should detect the
failure and show that instruction.

Creating it from homebrew with `FSUSER_CreateExtSaveData` is possible in principle, but the
directory/file limits, size limit and icon HOME Menu uses are not documented, so this is
not recommended without measuring a real console's extdata first.

HOME Menu extdata (`SaveData.dat`) exists on any console that has booted with the SD card.

### 3.4 File-in-use error

`FSFILE_Write`/`OpenFile` returns 0xC92044E6 ("operation not allowed with the current open
flags / file already in use") when HOME Menu still has `BgmCache.bin` open, which is the
case when the currently active theme has BGM. yellows8's workaround was to select the
default theme in HOME Menu first. Whether this still occurs for an app launched as a
normal title on current firmware is unconfirmed.

## 4. Accessing extdata with libctru

From `3ds/services/fs.h` and `3ds/services/cfgu.h`, with IPC details from 3dbrew.

### 4.1 Region

```c
Result cfguInit(void);
Result CFGU_SecureInfoGetRegion(u8* region);   // CFG_Region
void   cfguExit(void);
```

Values: 0 JPN, 1 USA, 2 EUR, 3 AUS (unused; Australian consoles report EUR), 4 CHN, 5 KOR,
6 TWN.

### 4.2 Archive

`ARCHIVE_EXTDATA = 0x00000006`. It requires a binary low path of 12 bytes:

| Word | Offset | Value |
|---|---|---|
| 0 | 0x0 | Media type in the low byte: `MEDIATYPE_SD` = 1 (bytes 1-3 zero) |
| 1 | 0x4 | Extdata ID low (for example 0x000002CD) |
| 2 | 0x8 | Extdata ID high = 0 for SD |

```c
u32 low[3] = { MEDIATYPE_SD, extdataId, 0 };
FS_Path path = { PATH_BINARY, sizeof(low), low };   // type, size 0xC, data
FS_Archive arc;
Result rc = FSUSER_OpenArchive(&arc, ARCHIVE_EXTDATA, path);
...
FSUSER_CloseArchive(arc);
```

File paths inside are ASCII or UTF-16 text: `fsMakePath(PATH_ASCII, "/ThemeManage.bin")`.

### 4.3 Constants and rules

The calls used are `FSUSER_OpenArchive`, `FSUSER_OpenFile`, `FSUSER_CreateFile`,
`FSUSER_DeleteFile`, `FSFILE_Read`, `FSFILE_Write`, `FSFILE_GetSize` and `FSFILE_Close`,
as declared in libctru `3ds/services/fs.h`.

| Constant | Value |
|---|---|
| `FS_OPEN_READ` / `FS_OPEN_WRITE` / `FS_OPEN_CREATE` | bit 0 / bit 1 / bit 2 |
| `FS_WRITE_FLUSH` / `FS_WRITE_UPDATE_TIME` | bit 0 / bit 8 |
| `PATH_BINARY` / `PATH_ASCII` / `PATH_UTF16` | 2 / 3 / 4 |
| `MEDIATYPE_NAND` / `MEDIATYPE_SD` / `MEDIATYPE_GAME_CARD` | 0 / 1 / 2 |

Rules that follow from the extdata design:

- Extdata files are fixed-size containers. `FSFILE_SetSize` fails. `FS_OPEN_CREATE` on
  open is not how files get made here; use `FSUSER_CreateFile(arc, path, 0, size)`, which
  zero-fills. To change a size: `FSUSER_DeleteFile` then `FSUSER_CreateFile`.
- Open existing files with `FS_OPEN_READ`, `FS_OPEN_WRITE`, or both; attributes 0.
- Write with `FS_WRITE_FLUSH`. Writing past the file size fails (0xE0E046C1 / 0xE0E046D1).
- No commit step is needed for extdata (`ARCHIVE_ACTION_COMMIT_SAVE_DATA` is for savedata).
- `FSUSER_CreateFile` returns 0xC82044BE if the path already exists.

### 4.4 Permissions

Opening an extdata ID that is not the one in the app's own exheader requires FS access
rights in the exheader (3dbrew lists access-info mask 0x100D for `ARCHIVE_EXTDATA`;
yellows8's tool asks for `FileSystemAccess: CategorySystemApplication` in the RSF for
CIA builds). Without it the call fails with 0xD9004676 or 0xE0E046BE. A .3dsx run from the
Homebrew Launcher inherits whatever the launcher grants. The minimum RSF access flags for
a CIA build are unconfirmed; the app's RSF is `app/meta/app.rsf`.

## 5. BCSTM (theme BGM)

Little-endian on 3DS. Layout from 3dbrew, cross-checked against vgmstream and VGAudio.

### 5.1 File header (0x40 bytes)

| Offset | Size | Field |
|---|---|---|
| 0x00 | 4 | Magic `CSTM` |
| 0x04 | u16 | Byte-order mark, reads 0xFEFF for little-endian (bytes `FF FE`) |
| 0x06 | u16 | Header size (0x40) |
| 0x08 | u32 | Version (0x02000000 typical; newer files differ) |
| 0x0C | u32 | File size |
| 0x10 | u16 | Number of blocks (3) |
| 0x12 | u16 | Reserved |
| 0x14 | 12 x n | Block references: u16 type, u16 pad, u32 offset from file start, u32 size |

Block reference types: 0x4000 INFO, 0x4001 SEEK, 0x4002 DATA. Find blocks by type rather
than by position. Every block starts with a 4-byte magic and a u32 size and is aligned to 0x20.

### 5.2 INFO block

| Offset in block | Size | Field |
|---|---|---|
| 0x00 | 8 | `INFO`, size |
| 0x08 | 8 | Reference to stream info (u16 type, u16 pad, u32 offset relative to 0x08) |
| 0x10 | 8 | Reference to track-info table (offset relative to 0x08; 0xFFFFFFFF = none) |
| 0x18 | 8 | Reference to channel-info table (offset relative to 0x08) |
| 0x20 | ... | Stream info (normally here) |

Stream info:

| Offset | Size | Field |
|---|---|---|
| 0x00 | u8 | Encoding: 0 PCM8, 1 PCM16, 2 DSP-ADPCM, 3 IMA-ADPCM |
| 0x01 | u8 | Loop flag |
| 0x02 | u8 | Channel count |
| 0x03 | u8 | Padding |
| 0x04 | u32 | Sample rate |
| 0x08 | u32 | Loop start, in samples |
| 0x0C | u32 | Loop end = total sample count |
| 0x10 | u32 | Number of blocks |
| 0x14 | u32 | Block size in bytes, per channel |
| 0x18 | u32 | Samples per block |
| 0x1C | u32 | Last block size in bytes, without padding |
| 0x20 | u32 | Last block sample count |
| 0x24 | u32 | Last block size in bytes, padded |
| 0x28 | u32 | Bytes per seek entry per channel (4) |
| 0x2C | u32 | Samples per seek entry |
| 0x30 | 8 | Reference to sample data: offset relative to DATA + 0x08 (normally 0x18, so samples start at DATA + 0x20) |

Later versions append more fields; do not assume a fixed stream-info size.

Channel-info table (at INFO + 0x08 + offset from 0x1C): u32 count, then `count` references
whose offsets are relative to the table start. Each channel info is one reference (offset
relative to that channel info) to the codec info.

DSP-ADPCM info, 0x2E bytes per channel:

| Offset | Size | Field |
|---|---|---|
| 0x00 | s16 x 16 | Coefficients (8 pairs) |
| 0x20 | u8 | Initial predictor (high nibble) and scale (low nibble) |
| 0x21 | u8 | Reserved |
| 0x22 | s16 | Initial history 1 (previous sample) |
| 0x24 | s16 | Initial history 2 |
| 0x26 | u8, u8, s16, s16 | Loop context: predictor/scale, reserved, history 1, history 2 at the loop start |
| 0x2C | u16 | Padding |

### 5.3 SEEK block

After the 8-byte header: one entry per seek interval; each entry holds, for every channel
in order, `s16 history1, s16 history2` valid at the start of that interval. Only needed to
start decoding mid-stream.

### 5.4 DATA block

Samples are stored in blocks, interleaved by channel: block 0 channel 0, block 0 channel 1,
block 1 channel 0, and so on. Each piece is "block size" bytes, except the final block, where
each channel piece is "last block padded size" bytes. Typical values: block size 0x2000
bytes = 0x3800 samples.

DSP-ADPCM frame: 8 bytes = 1 header byte (predictor << 4 | scale shift) + 7 data bytes =
14 samples, high nibble first. Software decode:

`sample = clamp16(((nibble_signed << scale_shift) << 11) + 1024 + c1*h1 + c2*h2) >> 11)`
with `c1 = coef[2*pred]`, `c2 = coef[2*pred + 1]`, then `h2 = h1; h1 = sample`.

### 5.5 Playback

The app decodes BCSTM in software to PCM16 (`app/source/core/bcstm.h`) and plays it with
`NDSP_FORMAT_STEREO_PCM16`. Decoding at 32 kHz stereo is cheap, and it avoids two ndsp
limitations: `NDSP_FORMAT_ADPCM` is mono, so a stereo file needs two ndsp channels kept in
sync, and the ADPCM loop context passed through `ndspWaveBuf.adpcm_data` is only exact
when loop start is a multiple of the block sample count. `ndspInit()` needs the DSP
firmware dump at `sdmc:/3ds/dspfirm.cdc`; wave buffers must be in linear memory
(`linearAlloc`) and flushed with `DSP_FlushDataCache` before queueing.

### 5.6 HOME Menu constraints on theme BGM

| Constraint | Source |
|---|---|
| File size <= 3,371,008 bytes (0x337000). Check bytes, not "3.3 MB" | 3dbrew, hacks.guide |
| Body header byte 0x05 must be non-zero or the BGM is ignored | 3dbrew |
| Do not use mono; it can break the BGM | hacks.guide; Kame Editor recommends stereo |
| Lower sample rates (22050 or 32000 Hz) are used only to fit the size limit | community guides |
| Format: tools produce DSP-ADPCM, looped | Kame Editor |

Whether HOME Menu accepts PCM16 or mono BGM, and any sample-rate ceiling, is unconfirmed.

## 6. SMDH (info.smdh)

Total size 0x36C0 bytes.

| Offset | Size | Field |
|---|---|---|
| 0x0000 | 4 | Magic `SMDH` |
| 0x0004 | u16 | Version |
| 0x0006 | u16 | Reserved |
| 0x0008 | 0x200 x 16 | Title entries, one per language |
| 0x2008 | 0x30 | Application settings (ratings, region lock, flags). Irrelevant for themes |
| 0x2038 | 8 | Reserved |
| 0x2040 | 0x480 | Small icon, 24x24 RGB565 tiled |
| 0x24C0 | 0x1200 | Large icon, 48x48 RGB565 tiled |

Title entry (0x200 bytes), UTF-16LE, zero-terminated/padded:

| Offset | Size | Field | Theme usage |
|---|---|---|---|
| 0x000 | 0x80 (64 code units) | Short description | Theme name |
| 0x080 | 0x100 (128 code units) | Long description | Theme description |
| 0x180 | 0x80 (64 code units) | Publisher | Author |

Language order: 0 Japanese, 1 English, 2 French, 3 German, 4 Italian, 5 Spanish,
6 Simplified Chinese, 7 Korean, 8 Dutch, 9 Portuguese, 10 Russian, 11 Traditional Chinese,
12-15 unused. Entry `n` starts at `0x8 + n * 0x200`.

Theme managers read name/description/author from the first entry at 0x8. Which language
slots theme editors fill is unconfirmed; using English (index 1) and falling back to the
first non-empty entry covers both.

Icons: same tiled layout as section 1.3: 8x8 tiles left to right then top to bottom,
Morton order inside each tile, u16 LE RGB565 with red in the top 5 bits. The 48x48 icon is
6x6 tiles; the 24x24 icon is 3x3 tiles.

GPU upload: PICA textures need power-of-two sizes, so use a 64x64 `GPU_RGB565` texture
(8 tiles per row) for the large icon. Tiles themselves need no conversion; only the row
stride differs. For each of the 6 tile rows, copy 6 tiles (6 x 64 x 2 = 768 bytes) from
the SMDH into the texture's tile row, whose stride is 8 x 64 x 2 = 1024 bytes. With the
icon in tile rows 0..5 the sub-texture is `left 0, right 0.75, top 1.0, bottom 0.25`. For
the 24x24 icon use a 32x32 texture: 3 tiles of 4 per row, `right 0.75`, `bottom 0.25`. To
decode on the CPU instead, use the Morton mapping from 1.3.

Themely skips icons whose first two bytes are `9D 04` or `BF 0D` (two known editor default
icons) and shows its own placeholder instead; optional.

## 7. Community theme zip contents

Files sit at the zip root (or in a plain folder with the same names). Names are matched
with inconsistent case in the wild, so match case-insensitively.

| File | Required | Role |
|---|---|---|
| `body_LZ.bin` | yes | Theme data (section 1). Also seen as `body_lz.bin` |
| `bgm.bcstm` | no | HOME Menu BGM (section 5). Also seen as `BGM.bcstm`. Used only if body byte 0x05 is set |
| `info.smdh` | no | Name, description, author, icon (section 6) |
| `preview.png` | no | Screenshot shown by managers. Also `Preview.png`; CHMM2 also accepted `.jpg` / `.bmp` |
| `bgm.ogg` | no | Ogg Vorbis copy of the BGM for in-manager preview, since older managers could not decode BCSTM. Also `BGM.ogg` |
| `ThemeManage.bin` | rare | A pre-made management file. CHMM2 copied it if present; safe to ignore and generate |

Preview image sizes seen by CHMM2: 400 wide x >= 480 tall (YATA), 412 x >= 480 (usagirei
editor), 432 x 528 (console screenshot composite). In each the top screen is the upper
half and the bottom screen is centred below it. A theme with no `preview.png` needs a
preview generated from the textures (section 1.3). Theme Plaza's zips hold `body_LZ.bin`,
`info.smdh`, the preview PNGs and, when the theme has music, `bgm.bcstm` and an Ogg copy of it.

Splash zips (`splash.bin`, `splashbottom.bin`) share the same distribution channel but are
a different feature; see [badges-splashes.md](badges-splashes.md).

## Install recipes

Single theme:

1. Region -> extdata IDs. Open both archives (4.2). If the theme archive fails to open,
   show the "open Change Theme in HOME Menu settings once" message.
2. Validate: body <= 0x150000 compressed, header size <= 0x150000, first decompressed u32
   is 1; BGM <= 0x337000 and starts with `CSTM`.
3. `SaveData.dat`: 0x141B = 0; 0x13B8.. = `FF 00 00 00 00 03 00 00`.
4. `BodyCache.bin`: body bytes, then zeros to 0x150000.
5. `BgmCache.bin`: BGM bytes then zeros to 0x337000, or all zeros for "no BGM".
6. `ThemeManage.bin`: 0x00 = 1, 0x04 = 0, 0x08 = body size, 0x0C = BGM size or 0,
   0x10 = 0xFF, 0x14 = 1, 0x18 = 0xFF, 0x1C = 0x200; zero the arrays at 0x338 and 0x360.

Shuffle (2..10 themes):

1. Steps 1-2 for every theme.
2. `SaveData.dat`: 0x141B = 1; entries at 0x13C0 + 8*i = `i 00 00 00 00 03 00 00` for
   used slots, zeros for the rest; regular entry per 3.2.
3. `BodyCache_rd.bin` (create at 0xD20000 if missing): body `i` at `i * 0x150000`, zero
   the remainder of each slot.
4. `BgmCache_0i.bin` (create at 0x337000 if missing): BGM or zeros.
5. `ThemeManage.bin`: as above but 0x08 = 0, 0x0C = 0, and sizes in the 0x338 / 0x360 arrays.

Removing a theme is not documented by any independent source. 3dbrew's "no theme" state
is: SaveData regular entry all zero (type 0), 0x141B = 0; ThemeManage 0x00 = 1, 0x10 = 1,
everything else zero; cache files zeroed.

The theme takes effect when HOME Menu next loads its theme (HOME Menu process restart).

## References

- 3dbrew: [Home_Menu/Themes](https://www.3dbrew.org/wiki/Home_Menu/Themes),
  [Home_Menu](https://www.3dbrew.org/wiki/Home_Menu) (SaveData.dat, theme extdata, ThemeManage.bin),
  [Extdata](https://www.3dbrew.org/wiki/Extdata),
  [Filesystem services](https://www.3dbrew.org/wiki/Filesystem_services) (archives, path types, error codes),
  [BCSTM](https://www.3dbrew.org/wiki/BCSTM), [BCWAV](https://www.3dbrew.org/wiki/BCWAV),
  [SMDH](https://www.3dbrew.org/wiki/SMDH)
- libctru (github.com/devkitPro/libctru): `services/fs.h`, `services/cfgu.h`, `util/decompress.h`,
  `ndsp/channel.h`, `ndsp-channel.c`; citro3d `tex3ds.h`; tex3ds (texture orientation and tiling)
- yellows8: [3ds_homemenu_extdatatool](https://github.com/yellows8/3ds_homemenu_extdatatool)
  (single-theme ThemeManage values, SaveData patch, low path, file-in-use error),
  [3ds_homemenuhax](https://github.com/yellows8/3ds_homemenuhax) (decompression buffer sizes)
- Rinnegatamante/CHMM2 `source/themes.lua` (shuffle layout, zip file names, preview sizes)
- Ann0ying/Themely `source/theme.cpp` (shuffle layout, create/delete handling, SMDH reading, icon upload)
- usagirei/3DS-Theme-Editor `ThemeEditor.Common` (colour block field order, texture decoding, LZ11 header)
- vgmstream `src/meta/bcstm.c`, `src/coding/ngc_dsp_decoder.c`; Thealexbarney/VGAudio
  `BCFstmReader.cs`, `BCFstmWriter.cs` (BCSTM offsets, DSP-ADPCM decode, SEEK table)
- Kame Editor wiki (gitlab.com/beelzy/kame-editor); wiki.hacks.guide "3DS: Custom themes"
