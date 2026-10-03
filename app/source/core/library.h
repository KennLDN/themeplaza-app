// What the disk worker knows about the files on the SD card. A small index (names, sizes, checksums)
// and one file of 48x48 icons are kept in sdmc:/3ds/Theme Plaza/cache so that only new or changed files
// have to be opened at start-up. Everything here is used from the disk worker thread only.
#pragma once
#include <functional>
#include <string>
#include <vector>
#include "formats.h"
#include "model.h"
#include "pack.h"

namespace lib {

extern const char* const DIRS[3];        // sdmc:/Themes, /Splashes, /Badges

struct Rec {
    u8 kind = 0;
    u8 flags = 0;                         // bit 0: has music
    u16 badgeCount = 0;
    u32 fileSize = 0, mtime = 0;
    u32 bodySize = 0, bodyCrc = 0;        // theme body_LZ.bin, or splash.bin
    u32 auxSize = 0, auxCrc = 0;          // theme bgm.bcstm size, or splashbottom.bin checksum
    s32 iconSlot = -1;                    // record number in the icon file
    std::string path, name, author, desc;
    bool seen = false;                    // found again in the current scan (not stored)
};

void load();                              // read the index (once)
void save();                              // write it back if something changed
// Reading one folder of the SD card, in steps: each step handles the files already in the index and at
// most maxNew new ones, appends their records to `found`, and returns true when the folder is finished
// (the Scan is then freed). In between, the worker can serve icons and previews for what is on screen.
struct Scan;
Scan* scanBegin(Kind kind);
bool scanStep(Scan* s, int maxNew, std::vector<Rec>& found, int& done, int& total);
// Builds (or refreshes) the record of one file, e.g. after a download. False if it is not usable.
bool add(Kind kind, const std::string& path, Rec& out);
void forget(const std::string& path);
void dropUnseen();                        // after a full scan: forget files that are gone
bool readIcon(int slot, u8* icon565);     // 0x1200 bytes

// The badge images of a set in install order (sorted by name), leaving out preview.png and _seticon.png.
// For the /Badges folder itself (loose PNGs, the "Other Badges" set) only the files directly in it count.
std::vector<const PackEntry*> badgeEntries(const Pack& pk, const std::string& path);
std::string baseName(const std::string& path);

}  // namespace lib
