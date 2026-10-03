// Access to other titles' extra data on the SD card: the HOME Menu's settings, its theme cache and
// its badge data. Files in these archives have a fixed size and cannot grow.
#pragma once
#include <3ds.h>
#include <vector>

namespace ext {

// Extdata ids for this console's region. 0 when the region has no themes (CHN, KOR, TWN).
u32 themeId();
u32 homeMenuId();
constexpr u32 BADGE_ID = 0x14D1;

class Archive {
public:
    ~Archive() { close(); }
    Result open(u32 extdataId);
    void close();
    bool isOpen() const { return open_; }
    Result fileSize(const char* path, u64& size);
    Result read(const char* path, std::vector<u8>& out);                 // the whole file
    Result readAt(const char* path, u64 offset, void* buf, u32 size);
    // Writes size bytes at offset into an existing file.
    Result writeAt(const char* path, u64 offset, const void* data, u32 size);
    // Makes sure the file exists with exactly this size (deleting and recreating it if not).
    Result ensure(const char* path, u64 size);
    // For a file whose size is not in the documentation: creates it with this size if it is missing; one that
    // exists is left alone, and WRONG_SIZE is returned if it is smaller than this.
    Result ensureMin(const char* path, u64 size);
    // Tries to open the file for writing and closes it again; fails if another process holds it.
    Result canWrite(const char* path);
    FS_Archive handle() const { return arc_; }

private:
    FS_Archive arc_ = 0;
    bool open_ = false;
};

// Not a system result: an existing file is smaller than what is about to be written into it.
constexpr Result WRONG_SIZE = (Result)0xE0000001;

// True if a result code means "there is no such file or archive" (as opposed to busy or forbidden).
bool missing(Result rc);

// Creates an extdata archive on the SD card (used for the badge data on consoles that never had badges).
Result create(u32 extdataId);

}  // namespace ext
