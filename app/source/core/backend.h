// Everything the UI asks of the outside world: what is on the SD card, the Theme Plaza catalogue,
// and the long-running operations (install, download, delete). The UI never blocks; it starts a Job
// and polls it each frame.
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "model.h"
#include "streambuf.h"

struct Job {
    volatile float progress = 0;   // 0..1, written by the worker
    volatile bool done = false;
    volatile int step = -1;        // a shuffle install: which of its themes is being read or written now
    bool ok = true;
    std::string error;             // shown to the user when !ok
    std::string note;              // when ok: a message to show instead of the usual one (optional)
};

enum InstallMode { INSTALL_NORMAL = 0, INSTALL_BGM_ONLY = 1, INSTALL_NO_BGM = 2 };

namespace backend {

void init();
void fini();
void update(float dt);        // called every frame on the main thread

// ---- Collection: what is on the SD card, in the order it was added ----
std::vector<Item*>& home(Kind k);
// Goes up whenever a Collection list or an installed mark changes, so the UI knows to rebuild its view.
int revision();
bool scanning();              // the first read of the SD card is still running
bool scanProgress(Kind k, int& done, int& total);   // while scanning this kind's folder: how far it is
// The theme(s) currently applied to the HOME Menu: one for a normal install, 2-10 for shuffle.
const std::vector<Item*>& installedThemes();

// ---- Theme Plaza ----
struct Store {
    bool ready = false, loading = false, failed = false;
    bool more = false;            // further pages exist
    bool sortFailed = false;      // the chosen order could not be had; the list is newest first instead
    bool moreFailed = false;      // a later page could not be had; the list ends with what is shown
    std::vector<Item*> list;      // grows while rows arrive
    std::string error;
};
Store& store();
// order: 0 newest, 1 most downloaded, 2 most liked
void storeLoad(Kind k, const std::string& query, const std::vector<std::string>& tags, int order);
void storeMore();             // ask for the next page (no effect while one is loading)
// Drops the list and whatever request is on its way; the next storeLoad starts afresh. For when what the
// list was for (type, search) has changed while Theme Plaza is not on screen.
void storeInvalidate();
bool online();
void retryOnline();           // after "Try again": look at the connection again
void setOffline(bool off);    // development switch
const char* storeCount(Kind k);   // e.g. "73,000 themes"
void wantStats(Item* it);     // downloads, likes, tags, description of a catalogue item

// ---- artwork ----
void wantIcon(Item* it);      // call each frame for the items on screen; loads and frees icons as needed
// Load wallpapers and colours in the background; sets it->previewReady. urgent = false queues it behind
// the other work (used to get neighbouring items ready ahead of time).
// cacheOnly: only look in the cache of earlier previews on the SD card, never ask the network (a catalogue
// item can be shown at once if it was seen before, while a network request waits for the cursor to rest).
void wantPreview(Item* it, bool urgent = true, bool cacheOnly = false);
void dropPreloads();          // the cursor moved: previews asked for with urgent = false and not yet started are skipped
void wantFullPreview(Item* it);   // also load the full-size wallpapers of this one item (for full-screen preview)
// Downloads the music preview of a catalogue item (an Ogg file, kept in memory). done is called as soon as
// the beginning has arrived, with a buffer that keeps filling, or with null if it could not be had. A newer
// call, or cancelBgmPreview(), makes a running download stop.
void fetchBgmPreview(Item* it, std::function<void(const std::shared_ptr<StreamBuf>& ogg)> done);
void cancelBgmPreview();

// ---- operations ----
Job* installThemes(const std::vector<Item*>& items, InstallMode mode);   // one item, or 2-10 for shuffle
Job* installSplash(Item* it);
void removeSplash();
Job* installBadges(const std::vector<Item*>& sets);
Job* removeBadges(Item* set);
Job* download(Item* it);      // Theme Plaza -> SD card; adds the item to home(kind) when done
Job* downloadId(long plazaId);    // the same from an id (QR code); the kind is found out from the file
Job* dumpTheme();             // copies the installed theme out of the HOME Menu's data to the SD card
void deleteFromSD(Item* it);
bool deletable(Item* it);     // a Collection item with a file or folder of its own (not the loose badges in /Badges itself)
Item* lastAdded();            // the Collection item most recently added by a download or a dump (may be null)
Item* collectionItem(long plazaId);   // the Collection item that came from this Theme Plaza id, or null
void freeJob(Job* j);
bool needsReboot();           // something was installed that the HOME Menu only reads at start-up

// ---- settings kept between runs ----
int setting(const char* key, int fallback);
void setSetting(const char* key, int value);

}  // namespace backend
