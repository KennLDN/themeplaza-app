#include "extdata.h"
#include <cstdio>
#include <cstring>
#include "log.h"

namespace ext {

namespace {
u8 region() {
    static int r = -1;
    if (r < 0) { u8 v = 2; if (R_FAILED(CFGU_SecureInfoGetRegion(&v))) v = 2; r = v; }
    return (u8)r;
}

// JPN, USA, EUR: theme extdata and the HOME Menu extdata that goes with it.
const u32 THEME_IDS[3] = {0x2CC, 0x2CD, 0x2CE}, HOME_IDS[3] = {0x82, 0x8F, 0x98};

// Which of the three regions' data this SD card has. Normally that is the console's region; the check
// also covers a console whose region setting and HOME Menu disagree.
int themeRegion() {
    static int found = -2;
    if (found != -2) return found;
    u8 r = region();
    int first = r == 0 ? 0 : r == 1 ? 1 : (r == 2 || r == 3) ? 2 : -1;
    int order[3] = {first < 0 ? 2 : first, 0, 1};
    if (order[0] == 0) order[1] = 2; else if (order[0] == 1) order[2] = 2;
    found = -1;
    for (int i : order) {
        Archive a;
        if (R_SUCCEEDED(a.open(THEME_IDS[i]))) {
            found = i;
            if (i != first) LOG("extdata: no theme data for this console's region (%d); using the one found for region index %d", (int)r, i);
            break;
        }
    }
    if (found < 0 && first >= 0) found = first;   // none exists yet: report the console's own, the open then fails with a clear message
    return found;
}
}  // namespace

u32 themeId() { int i = themeRegion(); return i < 0 ? 0 : THEME_IDS[i]; }
u32 homeMenuId() {
    int i = themeRegion();
    if (i >= 0) return HOME_IDS[i];
    switch (region()) { case 4: return 0xA1; case 5: return 0xA9; case 6: return 0xB1; default: return 0x98; }
}

Result Archive::open(u32 id) {
    close();
    u32 low[3] = {MEDIATYPE_SD, id, 0};
    FS_Path path = {PATH_BINARY, sizeof low, low};
    Result rc = FSUSER_OpenArchive(&arc_, ARCHIVE_EXTDATA, path);
    open_ = R_SUCCEEDED(rc);
    return rc;
}

void Archive::close() {
    if (open_) { FSUSER_CloseArchive(arc_); open_ = false; }
}

Result Archive::fileSize(const char* path, u64& size) {
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arc_, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
    if (R_FAILED(rc)) return rc;
    rc = FSFILE_GetSize(h, &size);
    FSFILE_Close(h);
    return rc;
}

Result Archive::read(const char* path, std::vector<u8>& out) {
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arc_, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
    if (R_FAILED(rc)) return rc;
    u64 size = 0;
    rc = FSFILE_GetSize(h, &size);
    if (R_SUCCEEDED(rc)) {
        out.resize((size_t)size);
        u32 got = 0;
        rc = FSFILE_Read(h, &got, 0, out.data(), (u32)size);
        if (R_SUCCEEDED(rc) && got != size) out.resize(got);
    }
    FSFILE_Close(h);
    return rc;
}

Result Archive::readAt(const char* path, u64 offset, void* buf, u32 size) {
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arc_, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0);
    if (R_FAILED(rc)) return rc;
    u32 got = 0;
    rc = FSFILE_Read(h, &got, offset, buf, size);
    if (R_SUCCEEDED(rc) && got != size) rc = -1;
    FSFILE_Close(h);
    return rc;
}

Result Archive::writeAt(const char* path, u64 offset, const void* data, u32 size) {
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arc_, fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE, 0);
    if (R_FAILED(rc)) return rc;
    u32 put = 0;
    rc = FSFILE_Write(h, &put, offset, data, size, FS_WRITE_FLUSH);
    if (R_SUCCEEDED(rc) && put != size) rc = -1;
    FSFILE_Close(h);
    return rc;
}

Result Archive::canWrite(const char* path) {
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arc_, fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE, 0);
    if (R_SUCCEEDED(rc)) FSFILE_Close(h);
    return rc;
}

bool missing(Result rc) { return R_SUMMARY(rc) == RS_NOTFOUND || R_DESCRIPTION(rc) == 120; }

Result Archive::ensure(const char* path, u64 size) {
    u64 have = 0;
    Result rc = fileSize(path, have);
    if (R_SUCCEEDED(rc)) {
        if (have == size) return 0;
        rc = FSUSER_DeleteFile(arc_, fsMakePath(PATH_ASCII, path));
        if (R_FAILED(rc)) return rc;
    } else if (!missing(rc)) return rc;      // in use, or not allowed: say so instead of trying to replace the file
    return FSUSER_CreateFile(arc_, fsMakePath(PATH_ASCII, path), 0, size);
}

Result Archive::ensureMin(const char* path, u64 size) {
    u64 have = 0;
    Result rc = fileSize(path, have);
    if (R_SUCCEEDED(rc)) {
        if (have >= size) return 0;
        LOG("extdata: %s is %lu bytes, expected %lu; left as it is", path, (unsigned long)have, (unsigned long)size);
        return WRONG_SIZE;
    }
    if (!missing(rc)) return rc;
    return FSUSER_CreateFile(arc_, fsMakePath(PATH_ASCII, path), 0, size);
}

Result create(u32 id) {
    FS_ExtSaveDataInfo info{};
    info.mediaType = MEDIATYPE_SD;
    info.saveId = id;
    // The archive's icon, shown in Data Management. A proper SMDH is made at build time (build.sh); without
    // the file at least the header is right.
    static u8 smdh[0x36C0];
    memset(smdh, 0, sizeof smdh);
    memcpy(smdh, "SMDH", 4);
    if (FILE* f = fopen("romfs:/extdata.smdh", "rb")) {
        if (fread(smdh, 1, sizeof smdh, f) != sizeof smdh) { memset(smdh, 0, sizeof smdh); memcpy(smdh, "SMDH", 4); }
        fclose(f);
    }
    return FSUSER_CreateExtSaveData(info, 1000, 1000, (u64)-1, sizeof smdh, smdh);
}

}  // namespace ext
