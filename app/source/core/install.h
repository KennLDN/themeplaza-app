// Applying things to the console: themes into the HOME Menu's theme cache, splashes into /luma,
// and reading back what is applied now. All of it runs on the disk worker.
#pragma once
#include <string>
#include <vector>
#include "backend.h"

namespace inst {

constexpr u32 BODY_MAX = 0x150000, BGM_MAX = 0x337000;

// packs: paths of 1 theme, or 2-10 for shuffle. Fills err with a sentence for the user on failure.
// touched: the HOME Menu's settings were changed. That is also true for an install that failed part-way: the
// HOME Menu is then set to "no theme" (and must be restarted, like after a successful install).
bool themes(const std::vector<std::string>& packs, InstallMode mode, Job* job, std::string& err, bool& touched);

struct Applied {
    bool shuffle = false;
    struct Body { u32 size, crc; bool repacked; };   // repacked: the size is not the original file's (see install.cpp), match by checksum only
    std::vector<Body> bodies;         // themes applied now, in shuffle order
    bool splashTop = false, splashBottom = false;
    u32 splashTopCrc = 0, splashBottomCrc = 0;
};
void readApplied(Applied& out);

// Copies the applied theme (the first one, when shuffle is on) to a new folder under /Themes. outPath: that folder.
bool dumpTheme(std::string& outPath, Job* job, std::string& err);

bool splash(const std::string& pack, Job* job, std::string& err, std::string& note);
void removeSplash();

// Rebuilds the HOME Menu's badge data from these badge sets (zip files or folders of PNGs). The HOME Menu
// then has exactly these sets: badges from anywhere else (Badge Arcade, another tool) are replaced, which is
// how every badge tool works. They are copied to sdmc:/3ds/Theme Plaza/backup* first, and an empty list puts the
// newest such copy back instead of leaving the console with no badges. Badges already placed on the HOME
// Menu stay where they are if their set is still in the list.
bool badges(const std::vector<std::string>& sets, Job* job, std::string& err, std::string& note);

}  // namespace inst
