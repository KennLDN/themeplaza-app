#include "install.h"
#include <cstdio>
#include <cstring>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>
#include "extdata.h"
#include "formats.h"
#include "log.h"
#include "pack.h"
#include "appdir.h"

#ifdef THEME_PLAZA_DEV
namespace dev { extern volatile bool failInstall; }
#endif

namespace inst {

namespace {

inline u32 rd32(const u8* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((u32)p[3] << 24); }
inline void wr32(u8* p, u32 v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

constexpr u32 SAVE_THEME = 0x13B8, SAVE_SHUFFLE = 0x13C0, SAVE_SHUFFLE_FLAG = 0x141B;
constexpr u32 MANAGE_SIZE = 0x800, MANAGE_BODY_SIZES = 0x338, MANAGE_BGM_SIZES = 0x360;
constexpr u32 PIECE = 128 * 1024;

std::string hex(Result rc) { char b[16]; snprintf(b, sizeof b, "%08lX", (unsigned long)rc); return b; }

struct Theme { std::vector<u8> body, bgm; };

bool loadTheme(const std::string& path, bool wantBody, bool wantBgm, Theme& t, std::string& err) {
    Pack pk;
    if (!pk.open(path)) { err = "That theme file could not be opened"; return false; }
    if (wantBody) {
        const PackEntry* e = pk.find("body_LZ.bin");
        if (!e) { err = "This theme has no body_LZ.bin"; return false; }
        if (e->size > BODY_MAX) { err = "This theme is larger than the HOME Menu allows"; return false; }
        if (!pk.read(*e, t.body, BODY_MAX) || t.body.size() < 8 || t.body[0] != 0x11) { err = "This theme's data is damaged"; return false; }
        // unpack it once the way the HOME Menu will, so that nothing it would choke on gets installed
        std::vector<u8> plain;
        if (!fmt::lz11(t.body.data(), t.body.size(), plain, BODY_MAX) || plain.size() < 0xC4 || rd32(plain.data()) != 1) { err = "This theme's data is not valid"; return false; }
    }
    if (wantBgm) {
        const PackEntry* e = pk.find("bgm.bcstm");
        // music that is too large or not a BCSTM is left out rather than refusing the theme
        if (e && e->size <= BGM_MAX && (!pk.read(*e, t.bgm, BGM_MAX) || t.bgm.size() < 0x40 || memcmp(t.bgm.data(), "CSTM", 4) != 0)) t.bgm.clear();
    }
    return true;
}

// Writes data at an offset in pieces so the progress bar moves; p0..p1 is this write's share of the job.
Result writePieces(ext::Archive& arc, const char* path, u64 offset, const std::vector<u8>& data, Job* job, float p0, float p1) {
    Handle h;
    Result rc = FSUSER_OpenFile(&h, arc.handle(), fsMakePath(PATH_ASCII, path), FS_OPEN_WRITE, 0);
    if (R_FAILED(rc)) return rc;
    u32 size = (u32)data.size();
    for (u32 done = 0; done < size && R_SUCCEEDED(rc);) {
        u32 n = size - done < PIECE ? size - done : PIECE, put = 0;
        rc = FSFILE_Write(h, &put, offset + done, data.data() + done, n, done + n >= size ? FS_WRITE_FLUSH : 0);
        if (R_SUCCEEDED(rc) && put != n) rc = -1;
        done += n;
        if (job) job->progress = p0 + (p1 - p0) * done / size;
    }
    FSFILE_Close(h);
    return rc;
}

// The cache files only need the real bytes; the size is recorded in ThemeManage.bin. Round up a little
// with zeros so nothing of an older, longer file directly follows the new data.
void padTo(std::vector<u8>& v, u32 limit) {
    u32 want = ((u32)v.size() + 0x1000 + 0xfff) & ~0xfffu;
    v.resize(want < limit ? want : limit, 0);
}

void themeEntry(u8* p, u32 index, bool used) {
    memset(p, 0, 8);
    if (used) { wr32(p, index); p[5] = 3; }
}

bool openBoth(ext::Archive& theme, ext::Archive& home, std::string& err) {
    if (!ext::themeId()) { err = "This console's region has no HOME Menu themes"; return false; }
    Result rc = theme.open(ext::themeId());
    if (R_FAILED(rc)) {
        LOG("theme extdata open failed %08lX", (unsigned long)rc);
        err = "Open Change Theme in the HOME Menu settings once, then try again";
        return false;
    }
    rc = home.open(ext::homeMenuId());
    if (R_FAILED(rc)) { LOG("home extdata open failed %08lX", (unsigned long)rc); err = "The HOME Menu's data could not be opened (" + hex(rc) + ")"; return false; }
    return true;
}

// A theme whose music switch this app turned on is stored repacked, so its checksum no longer matches the
// file it came from. This small file maps the repacked copy's checksum back to the original's.
const char* const ALIAS_FILE = APP_DIR "/repacked.txt";
u32 aliasOf(u32 crc) {
    u32 result = crc;
    if (FILE* f = fopen(ALIAS_FILE, "r")) {
        unsigned long a = 0, b = 0;
        if (fscanf(f, "%lx %lx", &a, &b) == 2 && (u32)a == crc) result = (u32)b;
        fclose(f);
    }
    return result;
}

bool fail(std::string& err, const char* what, Result rc) {
    LOG("%s failed %08lX", what, (unsigned long)rc);
    if ((u32)rc == 0xC92044E6) err = "The HOME Menu is using its theme files. Pick the default theme there, then try again";
    else if (rc == ext::WRONG_SIZE) err = std::string("A HOME Menu file for ") + what + " has a size this app does not know. Nothing was changed";
    else err = std::string("Could not write ") + what + " (" + hex(rc) + ")";
    return false;
}

constexpr u32 SAVE_PART = SAVE_SHUFFLE_FLAG + 1 - SAVE_THEME;      // 0x64 bytes: the theme entries and the shuffle switch

// Writes the part of SaveData.dat that says which theme is in use, and only that part: the rest of the file
// is the HOME Menu's own (its icon layout) and is not written back from a copy that may be minutes old.
Result putThemeState(ext::Archive& home, const std::vector<u8>& save) {
    return home.writeAt("/SaveData.dat", SAVE_THEME, &save[SAVE_THEME], SAVE_PART);
}

// "No theme": what the HOME Menu must find if anything stops before the new data is complete. It reads the
// whole of BodyCache.bin whenever its settings name a custom theme, whatever sizes ThemeManage.bin gives, so
// half-written cache files are only safe while the settings say there is no theme. The bytes between the
// shuffle entries and the switch (the HOME Menu's own shuffle bookkeeping) are kept.
Result switchThemeOff(ext::Archive& home, const std::vector<u8>& save) {
    std::vector<u8> off(save);
    memset(&off[SAVE_THEME], 0, 8);
    memset(&off[SAVE_SHUFFLE], 0, 80);
    off[SAVE_SHUFFLE_FLAG] = 0;
    return putThemeState(home, off);
}

}  // namespace

bool themes(const std::vector<std::string>& packs, InstallMode mode, Job* job, std::string& err, bool& touched) {
    touched = false;
    const int n = (int)packs.size();
    if (n < 1 || n > 10) { err = "Shuffle takes 2 to 10 themes"; return false; }
    const bool shuffle = n > 1;
    ext::Archive theme, home;
    if (!openBoth(theme, home, err)) return false;
    std::vector<u8> save, manage;
    Result rc = home.read("/SaveData.dat", save);
    if (R_FAILED(rc) || save.size() <= SAVE_SHUFFLE_FLAG) return fail(err, "the HOME Menu settings", rc);
    // the first byte is the file's format version: 3 came with themes (system 9.0), 4 with shuffle (9.3)
    if (save[0] < 3) { err = "This system version has no HOME Menu themes (9.0 or later is needed)"; return false; }
    if (shuffle && save[0] < 4) { err = "Shuffle needs system version 9.3 or later"; return false; }
    if (R_FAILED(rc = home.canWrite("/SaveData.dat"))) return fail(err, "the HOME Menu settings", rc);
    if (R_FAILED(theme.read("/ThemeManage.bin", manage)) || manage.size() != MANAGE_SIZE) {
        manage.assign(MANAGE_SIZE, 0);
        if (R_FAILED(rc = theme.ensure("/ThemeManage.bin", MANAGE_SIZE))) return fail(err, "the theme settings", rc);
    }

    // Order of the writes, here and below: everything that can be checked is checked first. Then the HOME
    // Menu's settings are set to "no theme", so that whatever happens next (an error, the power going) it
    // starts with its default theme and not with half-written data. Then ThemeManage.bin with zero sizes, the
    // cache files, ThemeManage.bin with the real sizes, and last of all the settings naming the new theme.
    if (mode == INSTALL_BGM_ONLY) {
        u32 bodySize = rd32(&manage[0x08]);
        if (save[SAVE_SHUFFLE_FLAG] || save[SAVE_THEME + 5] < 2 || bodySize == 0 || bodySize > BODY_MAX) { err = "Install a single theme first; its music can then be replaced"; return false; }
        Theme t;
        if (!loadTheme(packs[0], false, true, t, err)) return false;
        if (t.bgm.empty()) { err = "This theme has no music"; return false; }
        u32 size = (u32)t.bgm.size();
        padTo(t.bgm, BGM_MAX);
        if (R_FAILED(rc = theme.ensure("/BgmCache.bin", BGM_MAX)) || R_FAILED(rc = theme.canWrite("/BgmCache.bin"))) return fail(err, "the music", rc);
        // The theme in place decides whether music plays at all (a switch in its header). If it is off, the
        // theme is unpacked, switched on and packed again.
        std::vector<u8> body(bodySize), plain, packed;
        u32 oldCrc = 0;
        if (R_FAILED(rc = theme.readAt("/BodyCache.bin", 0, body.data(), bodySize))) return fail(err, "the theme", rc);
        if (!fmt::lz11(body.data(), body.size(), plain, BODY_MAX) || plain.size() < 0xC4) { err = "The installed theme could not be read"; return false; }
        job->progress = 0.1f;
        if (plain[5] == 0) {
            oldCrc = crc32Of(body.data(), body.size());
            plain[5] = 1;
            fmt::lz11Compress(plain.data(), plain.size(), packed);
            if (packed.size() > BODY_MAX) { err = "The installed theme is too large to have music added"; return false; }
            if (R_FAILED(rc = theme.canWrite("/BodyCache.bin"))) return fail(err, "the theme", rc);
        }
        job->progress = 0.2f;
        if (R_FAILED(rc = switchThemeOff(home, save))) return fail(err, "the HOME Menu settings", rc);
        touched = true;
        wr32(&manage[0x08], 0); wr32(&manage[0x0C], 0);
        if (R_FAILED(rc = theme.writeAt("/ThemeManage.bin", 0, manage.data(), MANAGE_SIZE))) return fail(err, "the theme settings", rc);
        if (!packed.empty()) {
            bodySize = (u32)packed.size();
            u32 newCrc = crc32Of(packed.data(), packed.size());
            padTo(packed, BODY_MAX);
            if (R_FAILED(rc = writePieces(theme, "/BodyCache.bin", 0, packed, job, 0.2f, 0.35f))) return fail(err, "the theme", rc);
            // remember which theme this repacked copy is, so the Collection still marks it as installed
            if (FILE* f = fopen(ALIAS_FILE, "w")) { fprintf(f, "%08lx %08lx\n", (unsigned long)newCrc, (unsigned long)aliasOf(oldCrc)); fclose(f); }
        }
        if (R_FAILED(rc = writePieces(theme, "/BgmCache.bin", 0, t.bgm, job, 0.35f, 0.95f))) return fail(err, "the music", rc);
        wr32(&manage[0x08], bodySize); wr32(&manage[0x0C], size);
        if (R_FAILED(rc = theme.writeAt("/ThemeManage.bin", 0, manage.data(), MANAGE_SIZE))) return fail(err, "the theme settings", rc);
        // the theme itself has not changed: its entry goes back as it was
        if (R_FAILED(rc = putThemeState(home, save))) return fail(err, "the HOME Menu settings", rc);
        job->progress = 1;
        return true;
    }

    // Look at every theme before anything is changed, so a bad file cannot leave a half-made install.
    // A single theme is kept in memory from here; for shuffle each one is read again when its turn comes,
    // because ten themes with music do not fit in an old 3DS's memory at once.
    std::vector<Theme> loaded(shuffle ? 0 : 1);
    for (int i = 0; i < n; i++) {
        Theme probe;
        Theme& t = shuffle ? probe : loaded[0];
        if (shuffle) job->step = 0;     // the screen shows the first theme while all are checked, then follows the writing
        if (!loadTheme(packs[i], true, !shuffle && mode != INSTALL_NO_BGM, t, err)) return false;
        job->progress = 0.25f * (i + 1) / n;
    }

    // Every file that will be written must exist with its size and be free to write before the first change.
    char bgmName[10][24];
    for (int i = 0; i < 10; i++) snprintf(bgmName[i], sizeof bgmName[i], "/BgmCache_%02d.bin", i);
    if (!shuffle) {
        if (R_FAILED(rc = theme.ensure("/BodyCache.bin", BODY_MAX)) || R_FAILED(rc = theme.canWrite("/BodyCache.bin"))) return fail(err, "the theme", rc);
        if (R_FAILED(rc = theme.ensure("/BgmCache.bin", BGM_MAX)) || R_FAILED(rc = theme.canWrite("/BgmCache.bin"))) return fail(err, "the music", rc);
    } else {
        // The sizes of the shuffle files are known from other tools, not from the documentation: a file of
        // another size that is already there is the HOME Menu's and is never deleted (see ensureMin).
        if (R_FAILED(rc = theme.ensureMin("/BodyCache_rd.bin", 10ull * BODY_MAX)) || R_FAILED(rc = theme.canWrite("/BodyCache_rd.bin"))) return fail(err, "the shuffle themes", rc);
        for (int i = 0; i < n; i++)
            if (R_FAILED(rc = theme.ensureMin(bgmName[i], BGM_MAX)) || R_FAILED(rc = theme.canWrite(bgmName[i]))) return fail(err, "the music", rc);
    }

    if (R_FAILED(rc = switchThemeOff(home, save))) return fail(err, "the HOME Menu settings", rc);
    touched = true;
    wr32(&manage[0x00], 1); wr32(&manage[0x04], 0); wr32(&manage[0x08], 0); wr32(&manage[0x0C], 0);
    wr32(&manage[0x10], 0xFF); wr32(&manage[0x14], 1); wr32(&manage[0x18], 0xFF); wr32(&manage[0x1C], 0x200);
    memset(&manage[MANAGE_BODY_SIZES], 0, 40); memset(&manage[MANAGE_BGM_SIZES], 0, 40);
    if (R_FAILED(rc = theme.writeAt("/ThemeManage.bin", 0, manage.data(), MANAGE_SIZE))) return fail(err, "the theme settings", rc);

    const float span = 0.7f / n;
    if (!shuffle) {
        Theme& t = loaded[0];
        u32 bodySize = (u32)t.body.size(), bgmSize = (u32)t.bgm.size();
        padTo(t.body, BODY_MAX); padTo(t.bgm, BGM_MAX);
        if (R_FAILED(rc = writePieces(theme, "/BodyCache.bin", 0, t.body, job, 0.25f, 0.45f))) return fail(err, "the theme", rc);
#ifdef THEME_PLAZA_DEV
        if (dev::failInstall) return fail(err, "the music (a failure made for testing)", -1);     // the test channel's "failinstall 1"
#endif
        if (R_FAILED(rc = writePieces(theme, "/BgmCache.bin", 0, t.bgm, job, 0.45f, 0.95f))) return fail(err, "the music", rc);
        wr32(&manage[0x08], bodySize); wr32(&manage[0x0C], bgmSize);
    } else {
        for (int i = 0; i < n; i++) {
            job->step = i;
            Theme t;
            if (!loadTheme(packs[i], true, mode != INSTALL_NO_BGM, t, err)) return false;
            u32 bodySize = (u32)t.body.size(), bgmSize = (u32)t.bgm.size();
            padTo(t.body, BODY_MAX); padTo(t.bgm, BGM_MAX);
            float p = 0.25f + span * i;
            if (R_FAILED(rc = writePieces(theme, "/BodyCache_rd.bin", (u64)i * BODY_MAX, t.body, job, p, p + span * 0.3f))) return fail(err, "the shuffle themes", rc);
            if (R_FAILED(rc = writePieces(theme, bgmName[i], 0, t.bgm, job, p + span * 0.3f, p + span))) return fail(err, "the music", rc);
            wr32(&manage[MANAGE_BODY_SIZES + 4 * i], bodySize); wr32(&manage[MANAGE_BGM_SIZES + 4 * i], bgmSize);
        }
    }
    if (R_FAILED(rc = theme.writeAt("/ThemeManage.bin", 0, manage.data(), MANAGE_SIZE))) return fail(err, "the theme settings", rc);
    save[SAVE_SHUFFLE_FLAG] = shuffle ? 1 : 0;
    themeEntry(&save[SAVE_THEME], 0xFF, true);
    for (int i = 0; i < 10; i++) themeEntry(&save[SAVE_SHUFFLE + 8 * i], i, shuffle && i < n);
    if (R_FAILED(rc = putThemeState(home, save))) return fail(err, "the HOME Menu settings", rc);
    job->progress = 1;
    return true;
}

void readApplied(Applied& out) {
    ext::Archive theme, home;
    std::vector<u8> save, manage, buf;
    if (ext::themeId() && R_SUCCEEDED(theme.open(ext::themeId())) && R_SUCCEEDED(home.open(ext::homeMenuId())) &&
        R_SUCCEEDED(home.read("/SaveData.dat", save)) && save.size() > SAVE_SHUFFLE_FLAG &&
        R_SUCCEEDED(theme.read("/ThemeManage.bin", manage)) && manage.size() == MANAGE_SIZE) {
        out.shuffle = save[SAVE_SHUFFLE_FLAG] != 0;
        if (!out.shuffle) {
            u32 size = rd32(&manage[0x08]);
            if (size && size <= BODY_MAX && save[SAVE_THEME + 5] >= 2) {
                buf.resize(size);
                if (R_SUCCEEDED(theme.readAt("/BodyCache.bin", 0, buf.data(), size))) {
                    u32 crc = crc32Of(buf.data(), size), original = aliasOf(crc);
                    out.bodies.push_back({size, original, original != crc});
                }
            }
        } else {
            for (int i = 0; i < 10; i++) {
                u32 size = rd32(&manage[MANAGE_BODY_SIZES + 4 * i]);
                if (!size || size > BODY_MAX || save[SAVE_SHUFFLE + 8 * i + 5] < 2) continue;
                buf.resize(size);
                if (R_SUCCEEDED(theme.readAt("/BodyCache_rd.bin", (u64)i * BODY_MAX, buf.data(), size))) out.bodies.push_back({size, crc32Of(buf.data(), size), false});
            }
        }
    }
    auto fileCrc = [&](const char* path, size_t expect, u32& crc) {
        FILE* f = fopen(path, "rb");
        if (!f) return false;
        buf.resize(expect + 1);
        size_t got = fread(buf.data(), 1, expect + 1, f);
        fclose(f);
        if (got != expect) return false;
        crc = crc32Of(buf.data(), expect);
        return true;
    };
    out.splashTop = fileCrc("sdmc:/luma/splash.bin", 288000, out.splashTopCrc);
    out.splashBottom = fileCrc("sdmc:/luma/splashbottom.bin", 230400, out.splashBottomCrc);
}

bool dumpTheme(std::string& outPath, Job* job, std::string& err) {
    ext::Archive theme, home;
    if (!openBoth(theme, home, err)) return false;
    std::vector<u8> save, manage;
    if (R_FAILED(home.read("/SaveData.dat", save)) || save.size() <= SAVE_SHUFFLE_FLAG || R_FAILED(theme.read("/ThemeManage.bin", manage)) || manage.size() != MANAGE_SIZE) {
        err = "The theme settings could not be read"; return false;
    }
    bool shuffle = save[SAVE_SHUFFLE_FLAG] != 0;
    u32 bodySize = shuffle ? rd32(&manage[MANAGE_BODY_SIZES]) : rd32(&manage[0x08]);
    u32 bgmSize = shuffle ? rd32(&manage[MANAGE_BGM_SIZES]) : rd32(&manage[0x0C]);
    if (!bodySize || bodySize > BODY_MAX) { err = "No custom theme is installed"; return false; }
    std::vector<u8> body(bodySize), bgm(bgmSize <= BGM_MAX ? bgmSize : 0);
    if (R_FAILED(theme.readAt(shuffle ? "/BodyCache_rd.bin" : "/BodyCache.bin", 0, body.data(), bodySize))) { err = "The installed theme could not be read"; return false; }
    job->progress = 0.3f;
    if (!bgm.empty() && R_FAILED(theme.readAt(shuffle ? "/BgmCache_00.bin" : "/BgmCache.bin", 0, bgm.data(), (u32)bgm.size()))) bgm.clear();
    job->progress = 0.6f;
    mkdir("sdmc:/Themes", 0777);
    char dir[64];
    for (int i = 1; i < 1000; i++) {
        snprintf(dir, sizeof dir, i == 1 ? "sdmc:/Themes/Dumped theme" : "sdmc:/Themes/Dumped theme %d", i);
        struct stat st;
        if (stat(dir, &st) != 0) break;
    }
    if (mkdir(dir, 0777) != 0) { err = "Could not create a folder on the SD card"; return false; }
    auto put = [&](const char* name, const std::vector<u8>& d) {
        FILE* f = fopen((std::string(dir) + "/" + name).c_str(), "wb");
        if (!f) return false;
        bool ok = fwrite(d.data(), 1, d.size(), f) == d.size();
        fclose(f);
        return ok;
    };
    if (!put("body_LZ.bin", body) || (!bgm.empty() && !put("bgm.bcstm", bgm))) { err = "Could not write to the SD card"; return false; }
    outPath = dir;
    job->progress = 1;
    return true;
}

// ---------- splashes ----------
namespace {

// Luma only shows a splash when its setting is on. Turns "off" into "before payloads" on the existing line
// of /luma/config.ini and leaves everything else as it is. Returns true if the setting is on afterwards.
bool lumaSplashOn(bool& changed) {
    changed = false;
    FILE* f = fopen("sdmc:/luma/config.ini", "rb");
    if (!f) return false;
    std::string s; char buf[1024]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
    fclose(f);
    size_t k = s.find("splash_position");
    if (k == std::string::npos) return false;
    size_t eq = s.find('=', k), eol = s.find_first_of("\r\n", k);
    if (eq == std::string::npos || (eol != std::string::npos && eq > eol)) return false;
    if (eol == std::string::npos) eol = s.size();
    std::string val = s.substr(eq + 1, eol - eq - 1);
    size_t a = val.find_first_not_of(" \t"), b = val.find_last_not_of(" \t");
    val = a == std::string::npos ? "" : val.substr(a, b - a + 1);
    if (strcasecmp(val.c_str(), "off") != 0) return true;
    s.replace(eq + 1, eol - eq - 1, " before payloads");
    // write the new text beside the file and swap it in, so a failed write cannot leave Luma without its settings
    const char* tmp = "sdmc:/luma/config.ini.new";
    f = fopen(tmp, "wb");
    if (!f) return false;
    bool ok = fwrite(s.data(), 1, s.size(), f) == s.size();
    ok = fclose(f) == 0 && ok;
    if (ok) { remove("sdmc:/luma/config.ini.old"); ok = rename("sdmc:/luma/config.ini", "sdmc:/luma/config.ini.old") == 0 && rename(tmp, "sdmc:/luma/config.ini") == 0; }
    if (ok) remove("sdmc:/luma/config.ini.old"); else { remove(tmp); rename("sdmc:/luma/config.ini.old", "sdmc:/luma/config.ini"); }
    changed = ok;
    return ok;
}

}  // namespace

bool splash(const std::string& pack, Job* job, std::string& err, std::string& note) {
    Pack pk;
    if (!pk.open(pack)) { err = "That splash file could not be opened"; return false; }
    const PackEntry* top = pk.find("splash.bin"); const PackEntry* bot = pk.find("splashbottom.bin");
    if (top && top->size != 288000) top = nullptr;
    if (bot && bot->size != 230400) bot = nullptr;
    if (!top && !bot) { err = "This file has no splash images of the right size"; return false; }
    mkdir("sdmc:/luma", 0777);
    std::vector<u8> buf;
    auto put = [&](const PackEntry* e, const char* path) {
        if (!e) { remove(path); return true; }   // an older image for this screen would otherwise stay
        if (!pk.read(*e, buf, 300000)) return false;
        // written beside the old file and swapped in, so a failed write cannot cost the splash that was there
        std::string part = std::string(path) + ".new";
        FILE* f = fopen(part.c_str(), "wb");
        if (!f) return false;
        bool ok = fwrite(buf.data(), 1, buf.size(), f) == buf.size();
        if (fclose(f) != 0) ok = false;
        if (ok) { remove(path); ok = rename(part.c_str(), path) == 0; }
        if (!ok) remove(part.c_str());
        return ok;
    };
    if (!put(top, "sdmc:/luma/splash.bin")) { err = "Could not write the splash to the SD card"; return false; }
    job->progress = 0.55f;
    if (!put(bot, "sdmc:/luma/splashbottom.bin")) { err = "Could not write the splash to the SD card"; return false; }
    job->progress = 0.9f;
    bool changed = false;
    if (lumaSplashOn(changed)) note = changed ? "Splash installed and switched on in Luma's settings" : "";
    else note = "Splash installed. Switch splashes on in Luma's settings (hold SELECT at boot)";
    job->progress = 1;
    return true;
}

void removeSplash() {
    remove("sdmc:/luma/splash.bin");
    remove("sdmc:/luma/splashbottom.bin");
}

}  // namespace inst
