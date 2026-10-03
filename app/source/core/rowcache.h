// What a Theme Plaza list row shows (name, creator, description, icon), kept on the SD card by item id.
// The published list only has ids; each row otherwise costs one more request for its SMDH. With this, a
// page that was seen before costs the one list request.
//
// sdmc:/3ds/Theme Plaza/cache/plaza_rows.bin   fixed-size records
// sdmc:/3ds/Theme Plaza/cache/plaza_rows.idx   id and time of each record
//
// A record is used for 14 days and then fetched again, so a renamed item or a new icon shows up eventually.
// Everything here is called from the network worker thread only.
#pragma once
#include <string>
#include "formats.h"

namespace rowcache {

bool get(long plazaId, fmt::Smdh& out);
void put(long plazaId, const fmt::Smdh& row);     // rows without an icon are not kept

// The figures shown for the selected row (one more request per row on the published API). Kept for a day
// with the row; desc is the site's own description, which replaces the SMDH's when there is one.
struct Stats { u32 downloads = 0, likes = 0; bool bgm = false; std::string desc; };
bool getStats(long plazaId, Stats& out);
void putStats(long plazaId, const Stats& stats);

}  // namespace rowcache
