# HOME Menu badges and Luma3DS boot splashes

Reference for the badge and splash side of the app: the badge extdata and its two files,
how the extdata is created and written, the community PNG and zip conventions, backups,
and the Luma3DS splash files and configuration. All multi-byte values are little-endian
unless stated.

Facts come from 3dbrew, the Luma3DS source and the independent badge tools listed under
[References](#references), described here in our own words. Where no source confirms a
statement it is marked "unconfirmed". The badge file layout was also tested against the
HOME Menu (section 1.4).

## 1. Badge extdata

### 1.1 Location and access

| Item | Value |
|---|---|
| Extdata ID | `0x000014D1` (the same in JPN, USA and EUR) |
| Media type | SD (`MEDIATYPE_SD` = 1) |
| On-card path | `sdmc:/Nintendo 3DS/<ID0>/<ID1>/extdata/00000000/000014d1/`. The device files there are encrypted DIFF containers and are not usable directly |
| Archive | `ARCHIVE_EXTDATA` (0x00000006) with a binary low path of 12 bytes: u32 media type (1), u32 save ID low (0x000014D1), u32 save ID high (0) |
| Files as seen through the archive | `/BadgeData.dat`, `/BadgeMngFile.dat`. 3dbrew shows them under `user/` in the extdata tree; the archive is mounted at that directory, so the app does not include `user/` in the path |
| `BadgeData.dat` size | `0xF4DF80` = 16,048,000 bytes |
| `BadgeMngFile.dat` size | `0xD4A8` = 54,440 bytes. "The file size must match 0xD4A8, otherwise the Home Menu code returns an error" (3dbrew) |
| Firmware | Both files were introduced in 9.0.0-20; GYTB asks for 9.3 or above |

Extdata files are fixed-size and cannot be resized after creation, so each file is created
once with its final size and later writes overwrite in place.

### 1.2 `BadgeData.dat` layout (names and images)

| Offset | Size | Content |
|---|---|---|
| `0x000000` | 100 x 16 x `0x8A` = `0x35E80` | Set names. 100 set entries, each 16 strings of `0x8A` bytes |
| `0x035E80` | 1000 x 16 x `0x8A` = `0x21B100` | Badge names. 1000 badge entries, each 16 strings of `0x8A` bytes |
| `0x250F80` | 100 x `0x2000` = `0xC8000` | Set icons. 64x64 RGB565, tiled, no alpha |
| `0x318F80` | 1000 x `0x2800` = `0x9C4000` | Badge images 64x64. Per entry: `0x2000` bytes RGB565 followed by `0x800` bytes A4 |
| `0xCDCF80` | 1000 x `0xA00` = `0x271000` | Badge images 32x32. Per entry: `0x800` bytes RGB565 followed by `0x200` bytes A4 |
| `0xF4DF80` | | End of file |

Entry addressing (N is the entry index, see the "index" fields in 1.3):

- Set name, language L: `N * 0x8A0 + L * 0x8A`
- Badge name, language L: `0x35E80 + N * 0x8A0 + L * 0x8A`
- Set icon: `0x250F80 + N * 0x2000`
- Badge 64x64 colour: `0x318F80 + N * 0x2800`; alpha: `0x31AF80 + N * 0x2800`
- Badge 32x32 colour: `0xCDCF80 + N * 0xA00`; alpha: `0xCDD780 + N * 0xA00`

Names:

- Each string slot is `0x8A` bytes = 69 UTF-16LE code units, zero padded. The app uses
  68 code units plus a terminator; whether the terminator is required is unconfirmed.
- There are 16 language slots per entry. 3dbrew does not list the order for
  `BadgeData.dat`. For Badge Arcade's PRB/CAB files it gives: Japanese, English, French,
  German, Italian, Spanish, Simplified Chinese, Korean, Dutch, Portuguese, Russian,
  Traditional Chinese, then 4 unused, which is the system language order. GYTB, ABE and the
  app write the same string into all 16 slots, which removes any dependence on the order.

Pixel format, colour: RGB565 stored as a u16 LE: `(r5 << 11) | (g6 << 5) | b5`. GYTB and
ABE convert from 8-bit by truncation (`r >> 3`, `g >> 2`, `b >> 3`).

Pixel format, alpha: A4, 4 bits per pixel, in a separate block that follows the colour
block. Pixel index `i` (in the tiled order below) is in byte `i / 2`; even `i` is the low
nibble, odd `i` is the high nibble. 8-bit alpha is converted by `a >> 4`.

Pixel order: the 3DS tiled (Morton / Z-order) layout.

- The image is split into 8x8 pixel tiles. Tiles are stored row-major: left to right,
  then top to bottom, starting from the top-left tile.
- Within a tile the 64 pixels are in Morton order with x as the lowest bit:
  bit0 = x bit 0, bit1 = y bit 0, bit2 = x bit 1, bit3 = y bit 1, bit4 = x bit 2,
  bit5 = y bit 2.
- For an SxS image (S = 64 or 32) and a pixel at (x, y) measured from the top-left:
  `i = ((y >> 3) * (S / 8) + (x >> 3)) * 64 + morton(x & 7, y & 7)`.
  Colour is at byte `2 * i` of the colour block, alpha nibble as described above.
- The origin is the top-left. There is no vertical flip (unlike GPU textures loaded
  through citro3d).

32x32 image: a half-size copy of the same badge, used in the HOME Menu's badge picker.
GYTB produces it by averaging each 2x2 block per channel, alpha included, without
premultiplying. ABE uses a bicubic downscale.

Transparent pixels: ABE replaces the RGB of fully transparent pixels with the colour of
the nearest opaque pixel in the same row/column before conversion, keeping the original
alpha; GYTB keeps whatever RGB the PNG has there. The likely purpose is to avoid dark
fringes when the HOME Menu filters the texture (unconfirmed).

Set icons: 64x64 RGB565, tiled as above, `0x2000` bytes, no alpha block. ABE draws the
icon scaled to 48x48 in the top-left corner of the 64x64 canvas, fills the remainder by
extending edge colours, and on read crops the top-left 48x48. The community convention is
a 48x48 `_seticon.png` (section 3), so the visible part is the top-left 48x48. There is
no alpha channel; what a transparent PNG is composited against is unconfirmed.

### 1.3 `BadgeMngFile.dat` layout (management data)

Header:

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 4 | Must be zero | |
| `0x04` | 4 | Number of badge sets | GYTB writes 0 even though it fills one set entry. ABE and the app write the real count |
| `0x08` | 4 | Number of unique badges | Count of used badge slots |
| `0x0C` | 4 | Number of placed badges | Badges currently placed on the HOME Menu |
| `0x10` | 4 | Selected badge set in the HOME Menu picker | `0xFFFFFFFF` = "All badges" |
| `0x14` | 4 | Selected badge column in "All badges" | 0 |
| `0x18` | 4 | Number of total badges | Sum of the per-badge quantities |
| `0x1C` | 4 | "Nintendo Network ID Number" | The account's principal ID (see 2.3) |
| `0x20` | `0x338` | Unknown, zeros | |
| `0x358` | `0x80` | Used badge slots bitfield | 1 bit per slot, 1024 bits, 1000 meaningful. Slot `i` is byte `i / 8`, bit `i % 8` (LSB first) |
| `0x3D8` | `0x10` | Used badge set slots bitfield | 1 bit per slot, 128 bits, 100 meaningful. Same bit order |
| `0x3E8` | 1000 x `0x28` | BadgeInfo entries | |
| `0xA028` | 100 x `0x30` | BadgeSetInfo entries | |
| `0xB2E8` | 360 x `0x18` | BadgeLayoutSlot entries | Badges placed on the HOME Menu |
| `0xD4A8` | | End of file | |

BadgeIdentifier (`0x10` bytes):

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x0` | 4 | Unknown | Reported to be a per-badge "hash" copied from the PRB file (PRB offset 0x40). GYTB and ABE leave it 0 for custom badges and the HOME Menu accepts that |
| `0x4` | 4 | Badge ID | GYTB uses slot + 1. ABE uses a running count, or the PRB's ID for imported badges. Parts of one multi-part badge share the ID |
| `0x8` | 4 | Badge set ID | Must equal the `Badge Set ID` of the set the badge belongs to |
| `0xC` | 2 | Badge index | Index N into the `BadgeData.dat` badge name and image arrays. GYTB and ABE make it equal to the slot number |
| `0xE` | 2 | Badge sub ID | Position inside a multi-part badge, see below. 0 for a standalone badge |

Sub ID values, read as four hex nibbles `R C r c`: R = 1 if the badge is 2 rows tall, C = 1
if it is 2 columns wide, r = row of this part, c = column of this part.

| Value | Meaning |
|---|---|
| `0x0000` | standalone |
| `0x0100` / `0x0101` | left / right part of a 2x1 badge |
| `0x1000` / `0x1010` | top / bottom part of a 1x2 badge |
| `0x1100` / `0x1101` / `0x1110` / `0x1111` | top-left / top-right / bottom-left / bottom-right of a 2x2 badge |

BadgeInfo (`0x28` bytes):

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x00` | `0x10` | BadgeIdentifier | |
| `0x10` | 2 | Number placed | How many copies are on the HOME Menu |
| `0x12` | 2 | Quantity | Copies owned. GYTB writes `0xFFFF`; ABE writes the user's value, 1 for PRB imports |
| `0x14` | 4 | Unknown ("packed data?") | Reported to control where the pin is drawn; PRB files store it as two signed coordinates (PRB 0xB0, 0xB4) and the packing is unconfirmed. 0 puts the pin in the centre |
| `0x18` | 8 | Shortcut title ID, copy 1 | u64 LE. `0xFFFFFFFFFFFFFFFF` = no shortcut |
| `0x20` | 8 | Shortcut title ID, copy 2 | Same value again. Both copies must be written |

Example: Activity Log on a EUR console is stored as bytes `00 22 02 00 10 00 04 00` twice,
that is `0x0004001000022200`.

BadgeSetInfo (`0x30` bytes; the first `0x18` bytes are the BadgeSetIdentifier):

| Offset | Size | Field | Notes |
|---|---|---|---|
| `0x00` | 4 | Unknown | usually `0xFFFFFFFF` |
| `0x04` | 4 | Unknown | usually `0xFFFFFFFF` |
| `0x08` | 4 | Unknown | usually 0 |
| `0x0C` | 4 | Unknown | usually `0x2710` (10000) |
| `0x10` | 4 | Badge set ID | GYTB uses `0x0000EFBE`. ABE uses the set number, or the CAB file's ID |
| `0x14` | 4 | Badge set index | Index N into the `BadgeData.dat` set name and set icon arrays |
| `0x18` | 4 | Unknown | usually `0xFFFFFFFF` |
| `0x1C` | 4 | Number of unique badges in the set | |
| `0x20` | 4 | Number of total badges in the set | Sum of quantities of its badges |
| `0x24` | 4 | Start badge index | Slot/index of the set's first badge. ABE treats a set as the contiguous run of badges from this index up to the next set's start index |
| `0x28` | 4 | Unknown | usually 0 |
| `0x2C` | 4 | Unknown | usually 0 |

BadgeLayoutSlot (`0x18` bytes):

| Offset | Size | Field |
|---|---|---|
| `0x00` | `0x10` | BadgeIdentifier of the placed badge |
| `0x10` | 4 | Position |
| `0x14` | 4 | Folder (`0xF0FF` = icon of a folder, `0xFFFFFFFF` = no folder) |

The semantics of `Position` are unconfirmed.

Unused-entry patterns, as ABE writes them for a new, empty file (GYTB leaves unused
entries all-zero):

- Unused BadgeInfo: `00x4, FFx10, 00x10, FFx8, 00x8` (unknown 0; badge ID, set ID and
  index all FF; sub ID, counts and 0x14 zero; first shortcut FF; second shortcut zero).
- Unused BadgeSetInfo: `FFx8, 00x4, 10 27 00 00, FFx12, 00x8, FFx4, 00x8`.
- Unused BadgeLayoutSlot: `00x4, FFx10, 00x2, FFx8`.

### 1.4 Verified against the HOME Menu

Tested on a EUR HOME Menu running in an emulator:

- With unused BadgeInfo, BadgeSetInfo and layout entries left as zeros, "All Badges"
  lists every badge but each set's own tab is empty. With the fill patterns above in the
  unused entries, the set tabs list their badges.
- Set ids and badge ids can be arbitrary 32-bit values.
- The two used-slot bit fields make no visible difference set or cleared.
- Quantity `0xFFFF` shows a crown on the badge; quantity 1 does not.

The app writes the fill patterns, sets both bitfields, uses quantity `0xFFFF`, writes
the real set count, and derives each badge id from its set and file name so that badges
already placed on the HOME Menu keep pointing at the right picture after a rebuild.

### 1.5 Fields that must be kept consistent when writing

1. File sizes exactly `0xF4DF80` and `0xD4A8`.
2. `0x00` = 0.
3. `0x04` set count, `0x08` unique badge count, `0x18` total = sum of all quantities.
4. For each used badge slot: bit in `0x358`; BadgeInfo with badge ID, set ID, index,
   sub ID, quantity, both shortcut copies.
5. For each used set slot: bit in `0x3D8`; BadgeSetInfo with set ID, set index, unique
   count, total count, start badge index; the constant-looking unknowns
   (`FFFFFFFF, FFFFFFFF, 0, 0x2710, ..., FFFFFFFF, ..., 0, 0`).
6. Badges of one set occupy consecutive slots starting at the set's start index.
7. `0x1C` = the current account's principal ID (see 2.3).
8. Names and images in `BadgeData.dat` at the indices referenced by `Badge Index` and
   `Badge Set Index`.
9. Layout slots and the placed counts (`0x0C`, BadgeInfo `0x10`). If the badge list is
   replaced while badges are placed on the HOME Menu, the placed entries still point at
   old IDs/indices and may change picture (hacks.guide warns about this). GYTB keeps the
   old layout slots but zeroes the counts; the app keeps slots whose badge still exists
   and clears the rest.

Checksums: no checksum or hash over the file content is documented, and neither GYTB nor
ABE computes one. The integrity data of the extdata container (DIFF) is handled by the FS
module when writing through the archive.

### 1.6 Limits

| Limit | Value |
|---|---|
| Badge slots (unique badges) | 1000 |
| Set slots | 100 |
| Placed badges (layout slots) | 360 |
| Quantity per badge | u16, max 65535 |
| Name length | `0x8A` bytes per language slot |

## 2. Creating the extdata, and what the HOME Menu needs

### 2.1 Detecting and creating the archive

GYTB and SBI do the following:

1. Try to open `ARCHIVE_EXTDATA` with the low path from 1.1.
2. If that fails, create the extdata, then open again. SBI only creates when the error's
   summary is "not found" (`R_SUMMARY(res) == RS_NOTFOUND`); GYTB creates on any failure.
3. Both create it with a hand-written IPC request using header `0x08300182`, which 3dbrew
   names `Obsoleted_3_0_CreateExtSaveData`: media type SD, ID low `0x14D1`, ID high 0,
   SMDH size `0x36C0`, directory limit 1000, file limit 1000, and a zero-filled
   `0x36C0`-byte buffer as the SMDH icon. 3dbrew says the obsolete call now wraps the
   current FS:CreateExtSaveData.
4. Deleting uses header `0x08350080` with media type and ID.

libctru equivalent (IPC `0x0851`):
`FSUSER_CreateExtSaveData(FS_ExtSaveDataInfo info, u32 directories, u32 files, u64 sizeLimit, u32 smdhSize, u8* smdh)`
with `info.mediaType = MEDIATYPE_SD`, `info.saveId = 0x14D1`, directory and file limits
1000, size limit -1 (3dbrew's default). `FSUSER_DeleteExtSaveData(info)` is IPC `0x0852`.
The app passes a real SMDH: the icon given at creation is what Data Management shows and
it can only be set when the extdata is created. The limits and icon Nintendo's own software
uses are unconfirmed.

### 2.2 Creating and writing the files

1. `FSUSER_CreateFile(archive, path, 0, size)` with the final size. An "already exists"
   error is harmless.
2. Open with `FS_OPEN_WRITE` (no create flag).
3. `FSFILE_Write` with `FS_WRITE_FLUSH`. GYTB and SBI write the whole 16 MB buffer at
   offset 0; the app writes in pieces at offsets with the same calls.
4. Close the file, close the archive.

File in use: GYTB treats result `0xC92044E6` as "Badge file in use" and tells the user to
open the badge case in the HOME Menu, wait for it to finish loading, and retry.
hacks.guide describes the same condition as "Ext Data Locked": return to the HOME Menu,
wait a few seconds, re-enter the app and retry.

Access rights: opening ExtSaveData with an ID not listed in the app's exheader requires FS
access bits (mask `0x100D` in 3dbrew's table). Under Luma3DS a `.3dsx` started through the
homebrew loader gets `fs_access_info = 0xFFFFFFFF` (`hbldr.c`). A CIA build needs suitable
`FileSystemAccess` entries in its RSF; SBI's template lists CategorySystemApplication,
CategoryFileSystemTool, CategorySystemSettings, CategoryHomeMenu, DirectSdmc and others.
Which single entry grants access to another title's extdata is unconfirmed. The app's RSF
is `app/meta/app.rsf`.

### 2.3 The NNID field

- `BadgeMngFile.dat` `0x1C` holds the principal ID of the console's Nintendo Network
  account (3dbrew: "Nintendo Network ID Number").
- GYTB and SBI read it through `act:u` IPC `0x000600C2` (GetAccountDataBlock) with
  account slot `0xFE` (default), size 4, block ID `0xC`, which 3dbrew lists as PrincipalId.
- libctru equivalent, as the app does it: `actInit(true)` (true selects `act:u`),
  `ACT_Initialize(0xB0002C8, 0, 0)`, then
  `ACT_GetAccountInfo(&pid, 4, ACT_DEFAULT_ACCOUNT, INFO_TYPE_PRINCIPAL_ID)`, then
  `actExit()`.
- The value matters: switching between an NNID and a PNID makes the badges disappear, and
  the fix is to rewrite this field with the current principal ID (SBI's "Fix NNID/PNID").
- On a console with no account the lookup can return 0 without error (GYTB issue 52), after
  which GYTB failed with `0xD900458B`; one user fixed it by creating an account. Behaviour
  with principal ID 0 is otherwise unconfirmed. Do not treat a failed lookup and a returned
  0 as the same case.

### 2.4 What the HOME Menu's own SaveData needs

Nothing is documented. 3dbrew's description of the HOME Menu extdata `SaveData.dat`
(format versions 0-4, size `0x2DA0`) lists icon arrays and theme entries and no badge
field. GYTB and SBI do not open the HOME Menu's extdata at all and work: running Badge
Arcade first is not required, and the "Decorate with Badges" button appears in HOME Menu
Settings after the tool has run.

The HOME Menu does keep some badge state outside the badge extdata: in GYTB issue 42 the
"Decorate with Badges" button stayed visible after the badge extdata was lost and tapping
it said "This feature cannot be used, because you don't have any badges yet"; deleting the
HOME Menu's extdata and letting it be recreated fixed it. Where that state lives is
unconfirmed.

Other behaviour:

- Opening Nintendo Badge Arcade replaces custom badge data with the official data.
- The HOME Menu opens the badge SD extdata during its startup sequence, so after installing
  badges it has to reload: hold POWER until the shutdown screen appears, then press HOME.
- 3dbrew's "Home Menu Jump Parameters" lists a jump command that opens the badge picker:
  a buffer with magic `ASHP` at offset 0 and command ID `0x3` at offset 4 (5 bytes, no
  arguments), passed to `APT:JumpToHomeMenu`. Not used by GYTB or SBI; unconfirmed.

Result codes seen in GYTB issues and not fully explained: `0xC92044E6` (file in use),
`0xC8804470` (no badge files), `0xC8804464`, `0xD900458B`, `0xC8A04573`.

## 3. Community input format

These are conventions, not a specification. They come from the GYTB README and from the
hacks.guide pages.

### 3.1 PNG files

- A badge is a PNG whose width and height are multiples of 64.
- 64x64 becomes one badge. Larger images are cut into 64x64 badges, so 128x128 becomes
  four. All pieces share the same name and shortcut.
- Size limit for one image: 72 tiles. GYTB's error text says "maximum 768x384" (12 x 6
  tiles) and its buffers hold 72 tiles; hacks.guide words it as "384 x 768 pixels (6 x 12
  badges)". The two disagree on which dimension is the long one; the app accepts any image
  whose tile count fits.
- GYTB's own size check is faulty (it tests the width's divisibility twice and compares
  against 12x384 and 6x384 pixels). Check both dimensions and the tile count.
- Slicing order in GYTB is column-major: tile number = `(x / 64) * (height / 64) + (y / 64)`,
  so a 2x2 image yields top-left, bottom-left, top-right, bottom-right. The mega-badge
  guides assume this order. What current packs expect is unconfirmed.
- GYTB gives every piece sub ID 0 and its own badge ID, so pieces are independent badges.
  A true multi-part ("mega") badge needs a shared badge ID and the sub IDs from 1.3; the
  community does this afterwards with ABE.
- Total limit 1000 badges per install.
- In 794 PNGs from 28 Theme Plaza badge zips: 510 are 8-bit palette PNGs, 246 are 8-bit
  RGBA, 33 are 1/2/4-bit palette, 5 are grey+alpha. The PNG decoder has to handle palette
  images with transparency and low bit depths. Sizes seen: 64x64 (733), 128x128 (24),
  192x256 (7), 64x128 (1), 256x256 (1).

### 3.2 File name conventions

- Badge name = file name up to the first period (GYTB README: "Everything after the first
  period will be ignored, so you can have multiple badges with the same name").
- Shortcut: `Name.<TIDLow>.png`, where the text between the first and second period is the
  8-hex-digit low half of a system application title ID. The high half is fixed at
  `0x00040010`, so `settings.00021000.png` launches title `0004001000021000` (USA System
  Settings). GYTB only accepts the field if it is exactly 8 characters long. The valid IDs
  are 3dbrew's Title list, section "00040010 - System Applications"; IDs are region
  specific. Shortcut badges show the target's banner instead of being a plain pin.
- GYTB's dump writes `Name.<badgeId hex>.<subId hex>.png`, or
  `Name.<TIDLow 8 hex>.<badgeId hex>.<subId hex>.png` for shortcut badges, replacing `.`,
  `:` and control characters in the name with spaces. Re-importing such a file restores
  name and shortcut under the rules above.
- GYTB processes files in byte-wise sorted file name order; that order becomes the slot
  order.
- None of the 28 sampled Theme Plaza badge zips used the shortcut naming.

### 3.3 Folders as sets

From hacks.guide, describing the current community installer:

- Badges live in `sdmc:/Badges/`. (GYTB instead used a `badges` folder next to the 3dsx,
  or on the SD root for the CIA.)
- A subfolder is a set; the folder name is the set name.
- `_seticon.png`, 48x48, inside a set folder is that set's icon. Without it a default icon
  is used.
- PNGs directly in `Badges/` go into a default set named "Other Badges".
- Badges downloaded from Theme Plaza go into a set named "ThemePlaza Badges".
- Installing overwrites existing badge data, which is why the guide dumps first.

### 3.4 Zip packs

Theme Plaza distributes badges as zips of PNGs. In 28 sampled zips (listing pages 0, 3,
20, 60, 90, 120):

- All are flat: no directories inside the zip.
- 16 older zips contain a `preview.png` of 512x1024. Those dimensions are multiples of 64,
  so a size check alone would import it as 128 badges; it has to be skipped by name.
- 12 newer zips contain `_seticon.png` (48x48) and no `preview.png`.
- Largest sampled pack: 502 entries.
- File names contain spaces, brackets and non-ASCII characters. Some entries have the zip
  UTF-8 flag (bit 11) set and some do not. Both deflate and stored entries occur.
- Nothing in the samples carries a set name. Under the 3.3 convention the name comes from
  the folder (or zip) name. No pack format carrying set names, per-badge quantities or
  mega-badge grouping was found.

## 4. Dumping and backing up existing badge data

Raw backup: read both files through the archive and store them unchanged. This is what
SBI does: it reads `/BadgeData.dat` (`0xF4DF80` bytes) and `/BadgeMngFile.dat` (`0xD4A8`
bytes) and writes them to `sdmc:/3ds/SimpleBadgeInjector/Dumped/`. Restore is the write
procedure from 2.2; SBI overwrites the NNID field with the console's current principal ID
when injecting. A raw pair is also the input format of ABE. Whether a backup restored after
the account changed needs anything beyond the NNID rewrite is unconfirmed; SBI's fix
suggests not.

Older raw method: MrCheeze's `extdata_dump` homebrew with a config file containing
`DUMP "000014d1:/BadgeData.dat" "BadgeData.dat"` and the matching `RESTORE` lines.

PNG export: GYTB walks slots 0-999, and for each slot whose bit is set in `0x358` decodes
the 64x64 image to a PNG named as in 3.2. It reads the name from the first language slot
and does not export set data, quantities, or the 32x32 images. Because it relies on the
bitfield, data written by ABE (bitfields zero) would export nothing; a dumper should also
accept "slot index < unique badge count" as an indicator.

Reading rules for a dumper:

- Extdata missing: open fails with a "not found" summary. Nothing to back up.
- Files missing inside an existing extdata: open-file fails; GYTB treats `0xC8804470` as
  "no official badges to dump".
- Files busy: see "File in use" in 2.2.
- GYTB converts 5/6-bit colour to 8-bit by scaling (`v * 255 / 31` or `/ 63`, rounded) and
  alpha by `a * 17`.

Restoring official badges: ABE's README says opening Badge Arcade restored the official
data from Nintendo's servers. Those servers have been shut down, so a raw backup taken
before the first install is the only reliable way back.

## 5. Luma3DS boot splashes

Read from the Luma3DS source (master as of 2026-09, latest release v13.4).

### 5.1 Files and locations

| File | Screen | Required size |
|---|---|---|
| `splash.bin` | top | 288,000 bytes (`3 * 400 * 240`) |
| `splashbottom.bin` | bottom | 230,400 bytes (`3 * 320 * 240`) |
| `splashpin.bin` | bottom, shown on the PIN entry screen (since v9.1) | 230,400 bytes |

- Luma opens the files by bare name relative to its working directory, which is `/luma`
  on the SD card, or `/rw/luma` on CTRNAND when Luma was started without an SD card. So
  the normal paths are `sdmc:/luma/splash.bin` and `sdmc:/luma/splashbottom.bin`.
- There are no other names or formats. `loadSplash` reads only `splash.bin` and
  `splashbottom.bin`; there is no animated or compressed variant, and no 3D (only the left
  top framebuffer is filled). Boot animations are a separate project (BAX).
- A file is used only if its size is exactly the value above. A wrong-size file is ignored
  without an error (`draw.c`).
- If neither file is valid, no splash is shown and no delay happens. One file alone is
  enough; the other screen stays black because the framebuffers are cleared when the
  screens are initialised.

### 5.2 Raw format

- No header. Raw framebuffer contents, 3 bytes per pixel, byte order B, G, R. Luma sets
  the framebuffer format registers to `0x80341` / `0x80301` (low bits 1 = the 24-bit
  format 3dbrew lists as GL_RGB8_OES, libctru's `GSP_BGR8_OES`) and the stride to
  `0x2D0` = 720 = 240 x 3.
- Layout is column-major: the 3DS panels are portrait panels mounted rotated, so the
  framebuffer is 240 pixels per line and 400 (top) or 320 (bottom) lines.
- For a pixel at (x, y) of the upright image, origin top-left, x to the right, y down,
  W = 400 or 320:

  `offset = 3 * (x * 240 + (239 - y))`, bytes at offset: B, G, R.

  So the file is W columns from left to right, each column stored bottom to top. This is
  the same as rotating the image 90 degrees clockwise and writing its rows as BGR.
- Cross-checks: Luma's own text drawing uses the same index expression (`draw.c`); libctru
  homebrew uses the same expression for its BGR8 framebuffer; the converter linked from the
  Luma wiki rotates +90 degrees and writes B, G, R. Decoding a Theme Plaza `splash.bin`
  with this formula gives an upright image with correct colours that matches the zip's
  `preview.png`.

### 5.3 Configuration

- File: `/luma/config.ini` on SD, or `/rw/luma/config.ini` on CTRNAND. INI format since
  v11.0 (2022-06-04); before that a binary `config.bin`.
- Section `[boot]`, two keys (`config.c`):

  | Key | Values | Default |
  |---|---|---|
  | `splash_position` | `off`, `before payloads`, `after payloads` (compared case-insensitively) | `off` |
  | `splash_duration_ms` | unsigned decimal, 0 to 4294967295 | `3000` |

- Splashes are off by default. Copying the files is not enough; the option has to be
  enabled. The boot-time menu (hold SELECT at power-on) shows it as "Splash: Off( )
  Before( ) After( ) payloads". The duration is no longer in the menu and can only be
  changed in `config.ini` (since v11.0).
- "Before payloads" shows the splash before Luma checks for chainloader button combos;
  "after payloads" shows it afterwards.
- Luma regenerates the whole `config.ini` from a template whenever settings are saved. If
  any option fails to parse, or the stored version numbers differ from the running Luma's
  (`config_version_major`/`minor` in `[meta]`, currently 3 / 13), Luma discards the file
  and falls back to defaults. The app therefore changes only the value on the existing
  `splash_position` line and keeps everything else intact.
- The Luma wiki page still describes a four-step "Splash duration" menu option (1/3/5/7
  seconds). That matches v9.1-v10.x and is out of date for v11+.

### 5.4 Detecting whether splashes are enabled

1. Parse `sdmc:/luma/config.ini`: find `splash_position` in `[boot]`; anything other than
   `off` means enabled. Read `splash_duration_ms` for the duration. If the file does not
   exist, Luma is older than v11.0, is running from CTRNAND, or the CFW is not Luma. This
   is what the app does.
2. Ask the running Luma: `svcGetSystemInfo(&out, 0x10000, param)`. `param = 4` returns
   `multiConfig`; the splash setting is `(out >> 4) & 3` (0 off, 1 before, 2 after;
   `SPLASH` is index 2 in `enum multiOptions`, 2 bits per option). `param = 6` returns the
   duration in ms. `param = 0x203` returns 1 if Luma booted from SD. `param = 0` returns
   the Luma version. The source says "Please do not use these, except 0, 1, and 0x200.
   Other types may get removed or reordered without notice", so treat params 4 and 6 as
   unstable.

The value reflects the configuration at boot. It does not say whether valid splash files
exist; check the two file sizes for that.

Not covered: the binary `config.bin` layout of Luma versions before v11.0, and splash
handling in other CFWs or Luma forks. fastboot3DS accepts the same two files but converts
them to its own format.

## 6. Community splash zips

In 26 Theme Plaza splash zips (listing pages 0, 3, 20, 60, 150, 220), all flat:

| Entry | Present in | Content |
|---|---|---|
| `splash.bin` | 26 / 26 | 288,000 bytes, format as in 5.2 |
| `splashbottom.bin` | 25 / 26 | 230,400 bytes. One zip is top-only |
| `preview.png` | 26 / 26 | 400x480 PNG: top screen image above, bottom screen image below it, the 320-wide bottom image horizontally centred |
| `icon.png` | 26 / 26 | 48x48 PNG |
| `info.smdh` | 26 / 26 | 14,016 bytes (`0x36C0`), a standard SMDH: item name as short description, description as long description, author as publisher |
| `name.txt` | 10 / 26 (the newer uploads) | The item name as plain text, no trailing newline |

Other conventions:

- Names are exact and lower-case in every sample; no variants such as `splashtop.bin` were
  seen. Zips from sources other than Theme Plaza and splash-ds may differ; unconfirmed.
- Manual install: extract `splash.bin` and `splashbottom.bin` into `sdmc:/luma/`.
- App install convention: zips are placed in a `Splashes` folder on the SD root (Theme
  Plaza FAQ; the splash-ds page says the same). The SD card's FAT file system is
  case-insensitive, so the folder's spelling only matters for display.
- The splash-ds web tool produces a zip with only `splash.bin`, `splashbottom.bin` and
  `preview.png`, so `info.smdh`, `icon.png` and `name.txt` cannot be assumed; fall back to
  the zip file name for the title.
- A zip may contain only one of the two `.bin` files. When installing a top-only splash, a
  `splashbottom.bin` left over from a previous splash would still be shown by Luma, so the
  installer deletes the file that the new splash does not provide. No bottom-only zip was
  seen.

## References

- 3dbrew: [Home_Menu](https://www.3dbrew.org/wiki/Home_Menu) (badge SD ExtData,
  BadgeData.dat, BadgeMngFile.dat, SaveData.dat, startup, jump parameters),
  [Extdata](https://www.3dbrew.org/wiki/Extdata),
  [Filesystem services](https://www.3dbrew.org/wiki/Filesystem_services) (CreateExtSaveData,
  DeleteExtSaveData, OpenArchive),
  [Nintendo_Badge_Arcade/PrizeCollection](https://www.3dbrew.org/wiki/Nintendo_Badge_Arcade/PrizeCollection)
  (PRB/CAB files, language order), [ACT_Services](https://www.3dbrew.org/wiki/ACT_Services),
  [GPU/External_Registers](https://www.3dbrew.org/wiki/GPU/External_Registers)
- GYTB: [github.com/MrCheeze/GYTB](https://github.com/MrCheeze/GYTB) (`source/main.c`,
  `ext.c`, `actu.c`, README, issues 42, 52, 55)
- ABE: [github.com/AntiMach/advanced-badge-editor](https://github.com/AntiMach/advanced-badge-editor)
  (`DataShift.cs`, `EditorForm.cs`, README)
- SBI: [github.com/AntiMach/simple-badge-injector](https://github.com/AntiMach/simple-badge-injector)
  (`include/badge.h`, `source/badge.c`, `source/main.c`, `buildtools/3ds/template.rsf`)
- wiki.hacks.guide: "3DS: Custom badges", "3DS: GYTB", "3DS: Splash screens"
- GBAtemp threads 403183 ("Nintendo Badge Arcade Hacking"), 403854 ("Full Guide to 3DS HOME
  Menu Badge Customization"), 448520 (ABE); Thysbelon, "How to Make Custom 3DS Mega Badges"
- Luma3DS: [github.com/LumaTeam/Luma3DS](https://github.com/LumaTeam/Luma3DS) (`fs.c`,
  `draw.c`, `screen.c`, `config.c`, `arm11/source/main.c`, `hbldr.c`, `config_template.ini`,
  README, wiki "Optional features", release notes)
- xem, [image-to-bin converter](https://xem.github.io/3DShomebrew/tools/image-to-bin.html);
  [splash-ds](https://github.com/jigglycrumb/splash-ds)
- libctru (github.com/devkitPro/libctru): `services/fs.h`, `services/act.h`, `services/gspgpu.h`, `gfx.h`
