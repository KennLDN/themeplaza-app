// A cache of finished previews on the SD card. Unpacking a theme to draw its preview card costs a
// noticeable part of a second on an original 3DS; the result is two small pictures and three colours,
// which are kept here so that the next visit reads one record of 131 KB instead.
//
// sdmc:/3ds/Theme Plaza/cache/previews.bin   fixed-size records, appended in the order they were made
// sdmc:/3ds/Theme Plaza/cache/previews.idx   one 32-bit key per record
//
// The key is made from what the item's files contain, so a file that is replaced gets a new record and a
// renamed one keeps its record. Everything here is called from the disk worker thread only.
#pragma once
#include "formats.h"

namespace pvcache {

struct Colours { u32 bar = 0, tile = 0, glyph = 0; bool set = false; };

// 0 means "do not cache this item".
u32 key(int kind, u32 bodySize, u32 bodyCrc, u32 auxSize, u32 auxCrc);

// The key of a Theme Plaza item that is not on the SD card (its pictures come from the site). 0 for badge sets.
u32 plazaKey(int kind, long plazaId);

bool get(u32 key, fmt::Image& top, fmt::Image& bottom, Colours& col);
void put(u32 key, const fmt::Image& top, const fmt::Image& bottom, const Colours& col);

}  // namespace pvcache
