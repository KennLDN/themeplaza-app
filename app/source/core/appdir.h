// The two places where the app's name is part of how it works: the folder it keeps its own files in on the
// SD card, and the name it gives itself to web servers. (The name shown on the HOME Menu is set in build.sh,
// the one in the More menu's "Exit" entry in ui/app.cpp.)
// Tools that know the folder: emulator/*.py, and tools/hosttest/cache_check.py, which makes the same path on the PC.
#pragma once

#define APP_DIR "sdmc:/3ds/Theme Plaza"
#ifndef APP_VERSION
#define APP_VERSION "0.0.0"
#endif
#define APP_USER_AGENT "ThemePlaza/" APP_VERSION " (Nintendo 3DS)"      // APP_VERSION comes from build.sh
