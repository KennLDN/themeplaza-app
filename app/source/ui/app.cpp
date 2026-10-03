#include "app.h"
#include "layout.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <strings.h>
#include "../core/log.h"
#include "../core/plaza.h"
#include "../core/qr.h"
#include "../core/sound.h"
#include "../dev/testctl.h"

namespace ui {

App g;
// the most used tags on Theme Plaza that are not colours or the site's own name for things
const char* const TAGS[NTAGS] = {"anime", "pokemon", "cute", "nintendo", "mario", "zelda", "sonic", "music", "retro", "dark"};
const char* const SRC_NAMES[2] = {"Collection", "Theme Plaza"};
const char* const TYPE_NAMES[3] = {"Themes", "Splashes", "Badges"};
static const char* const KIND_PLURAL[3] = {"themes", "splashes", "badge sets"};
static const char* const KIND_SINGLE[3] = {"theme", "splash", "badge set"};
static const char* const SORT_NAMES[3] = {"Name", "Creator", "Recently added"};
static const char* const ORDER_NAMES[3] = {"Newest", "Most downloaded", "Most liked"};

enum { ACT_NONE, ACT_CLEAR, ACT_DEL, ACT_MUSIC, ACT_BADGES, ACT_BGM_ONLY, ACT_NO_BGM, ACT_DUMP, ACT_EXIT };

namespace {

std::string lower(std::string s) { for (auto& c : s) c = tolower((unsigned char)c); return s; }

void stage(Dir d, bool pop = false) { g.stageClock.start(); g.stageDir = d; g.chipPop = pop; }

void rebuildView() {
    g.view.clear();
    if (plaza()) {
        auto& st = backend::store();
        if (st.ready) g.view = st.list;
        return;
    }
    auto list = backend::home((Kind)g.type);
    if (g.sort == 0) std::stable_sort(list.begin(), list.end(), [](Item* a, Item* b) { return strcasecmp(a->name.c_str(), b->name.c_str()) < 0; });
    else if (g.sort == 1) std::stable_sort(list.begin(), list.end(), [](Item* a, Item* b) { int c = strcasecmp(a->author.c_str(), b->author.c_str()); return c ? c < 0 : strcasecmp(a->name.c_str(), b->name.c_str()) < 0; });
    else std::stable_sort(list.begin(), list.end(), [](Item* a, Item* b) { return a->order > b->order; });
    std::string q = lower(g.findQ);
    for (Item* it : list)
        if (q.empty() || lower(it->name + " " + it->author).find(q) != std::string::npos) g.view.push_back(it);
    if (!backend::scanning()) g.view.push_back(&g.more[g.type]);
}

void clampSel() { int n = (int)g.view.size(); setSel(std::max(0, std::min(getSel(), n - 1))); }

void buildPane(Dir d) {
    rebuildView(); clampSel();
    g.paneClock.start(); g.paneDir = d;
    g.curPlaced = false;
    for (auto& t : g.tileSel) t.set(0);
    if (!plaza()) g.tileSel[getSel() % 10].set(1);   // the selected tile is already at its size when a page appears
    g.rows.assign(g.view.size(), RowAnim());
}

void renderAll(Dir d) { buildPane(d); stage(d); }

void queuePreview() {
    g.pvWait = -1;
    backend::dropPreloads();
    Item* it = cur();
    if (!it || it->more) return;
    if (plaza() && (!storeUsable() || (it->previewReady && it->statsReady))) return;
    g.pvWait = plaza() ? 0.32f : 0.10f;
    g.pvEarly = false;
}

void bgmQueue() {
    Item* it = cur();
    bool want = it && !it->more && it->kind == K_THEME && it->bgm && !g.job && g.focus == FOCUS_MAIN && (g.mode == MODE_BROWSE || g.mode == MODE_PREVIEW) && !(plaza() && !storeUsable());
    // a catalogue item's music waits for its preview; the app's music stays down meanwhile
    bool waiting = want && it->plaza && it->path.empty() && !it->previewReady;
    sound::wantTheme(want && !waiting ? it : nullptr, waiting);
}

void loadStore(bool force) {
    auto& st = backend::store();
    // force: something about the request changed (type, search, order), so a load that is still running is
    // for the wrong thing and is replaced
    if (!backend::online() || ((st.loading || st.ready) && !force)) return;
    std::vector<std::string> tags;
    for (int t : g.findTags) tags.push_back(TAGS[t]);
    backend::storeLoad((Kind)g.type, g.findQ, tags, g.order);
    g.storeWasReady = false;
}

void select(int i, Dir d) {
    int old = getSel();
    if (i == old) return;
    bool pageChange = !plaza() && i / 10 != old / 10;
    setSel(i);
    if (pageChange) { sound::page(); int keep = i; buildPane(i > old ? DIR_R : DIR_L); setSel(keep); }
    else sound::move();
    stage(d);
    queuePreview(); bgmQueue();
}

void setFocus(Focus f) { g.focus = f; sound::move(); bgmQueue(); }

void setType(int t) {
    if (t < 0 || t > 2 || (plaza() && !backend::online())) { sound::err(); return; }
    Dir d = t > g.type ? DIR_R : DIR_L;
    g.type = t; g.ticks[0].clear(); g.ticks[1].clear(); g.findQ.clear(); g.findTags.clear();
    sound::tab();
    backend::storeInvalidate(); g.sel[1][g.type] = 0;
    if (plaza()) loadStore(true);
    renderAll(d); queuePreview(); bgmQueue();
}

void switchSrc(int to) {
    if (to == g.src) { sound::err(); return; }
    if (findOn()) { g.findQ.clear(); g.findTags.clear(); backend::storeInvalidate(); g.sel[1][g.type] = 0; g.ticks[1].clear(); }
    g.src = to; g.focus = FOCUS_MAIN; sound::tab();
    if (plaza()) loadStore(false);
    renderAll(to ? DIR_R : DIR_L); queuePreview(); bgmQueue();
}

void move(char btn) {
    if (g.focus == FOCUS_SEL) {
        if (btn == 'L' || btn == 'R') return setType(g.type + (btn == 'L' ? -1 : 1));
        if (btn == 'D') return setFocus(FOCUS_MAIN);
        return sound::err();
    }
    int n = (int)g.view.size(), s = getSel();
    if (plaza()) {
        if (!backend::online()) return sound::err();
        if (btn == 'L' || btn == 'R') return setType(g.type + (btn == 'L' ? -1 : 1));
        if (btn == 'U' && (s == 0 || !n || !backend::store().ready)) return setFocus(FOCUS_SEL);
        if (!storeUsable() || !n) return sound::err();
        int c = std::max(0, std::min(n - 1, s + (btn == 'U' ? -1 : 1)));
        if (c == s) return sound::err();
        return select(c, c > s ? DIR_R : DIR_L);
    }
    int page = s / 10, col = s % 5, row = (s % 10) / 5, i = s;
    if (btn == 'U') { if (!row) return setFocus(FOCUS_SEL); i = s - 5; }
    if (btn == 'D' && !row && s + 5 < n) i = s + 5;
    if (btn == 'L') i = col > 0 ? s - 1 : page > 0 ? (page - 1) * 10 + row * 5 + 4 : s;
    if (btn == 'R') { int t = (page + 1) * 10 + row * 5; i = col < 4 ? (s + 1 < n ? s + 1 : s) : t < n ? t : (page + 1) * 10 < n ? (page + 1) * 10 : s; }
    if (i == s) return sound::err();
    select(i, btn == 'L' || btn == 'U' ? DIR_L : DIR_R);
}

void startJob(Job* j, const std::string& text, bool modal, std::function<void(bool)> done) {
    g.job = j; g.jobText = text; g.busyModal = modal; g.jobDone = std::move(done); g.jobShown = 0;
    if (modal) g.overClock.start();
    bgmQueue();
}

// Goes to the Collection page that holds the item just added (a QR download, a dumped theme) and selects it.
void showAdded(Item* it = nullptr) {
    if (!it) it = backend::lastAdded();
    if (!it) return renderAll(DIR_F);
    // The type or the search changes under the Theme Plaza list: it is dropped, like on any type change.
    if (g.type != (int)it->kind || findOn()) {
        backend::storeInvalidate(); g.sel[1][it->kind] = 0; g.ticks[1].clear();
        if (g.type != (int)it->kind) g.ticks[0].clear();
    }
    g.src = 0; g.type = it->kind; g.focus = FOCUS_MAIN;
    g.findQ.clear(); g.findTags.clear();
    rebuildView();
    auto f = std::find(g.view.begin(), g.view.end(), it);
    if (f != g.view.end()) setSel((int)(f - g.view.begin()));
    g.keepSel = it;
    renderAll(DIR_F); queuePreview();
}

void refresh(const std::string& msg) { sound::done(); stage(DIR_F, true); toast(msg); bgmQueue(); }

void install(std::vector<Item*> items, const std::string& label, InstallMode mode = INSTALL_NORMAL) {
    sound::ok();
    size_t n = items.size();
    g.jobItems = n > 1 ? items : std::vector<Item*>();
    startJob(backend::installThemes(items, mode), label, true, [n, mode](bool ok) {
        if (!ok) return;
        g.ticks[0].clear();
        refresh(mode == INSTALL_BGM_ONLY ? "Music installed. It plays on your HOME Menu." : n > 1 ? std::to_string(n) + " themes set to shuffle" : "Installed. It shows on your HOME Menu.");
    });
}

void installItem(Item* it) {
    if (it->kind == K_BADGE) {
        sound::ok();
        return startJob(backend::installBadges({it}), "Installing " + std::to_string(it->badgeCount) + " badges\xe2\x80\xa6", true, [](bool ok) { if (ok) refresh("Badges added to your HOME Menu badge box"); });
    }
    if (it->kind == K_SPLASH) {
        sound::ok();
        return startJob(backend::installSplash(it), "Installing " + it->name + "\xe2\x80\xa6", true, [](bool ok) { if (ok) refresh("Splash installed. It shows at next boot."); });
    }
    install({it}, "Installing " + it->name + "\xe2\x80\xa6");
}

void downloadNext();
void download(std::vector<Item*> list) {
    sound::ok();
    // in the order of the list, so the progress bar moves down the screen
    std::stable_sort(list.begin(), list.end(), [](Item* a, Item* b) {
        return std::find(g.view.begin(), g.view.end(), a) < std::find(g.view.begin(), g.view.end(), b);
    });
    g.dlQueue = std::move(list); g.dlPos = 0;
    downloadNext();
}
void downloadNext() {
    Item* it = g.dlQueue[g.dlPos];
    g.dlItem = it;
    startJob(backend::download(it), "", false, [it](bool ok) {
        g.dlItem = nullptr;
        if (ok) { it->onSD = true; auto& t = g.ticks[1]; t.erase(std::remove(t.begin(), t.end(), it), t.end()); }
        if (ok && ++g.dlPos < g.dlQueue.size()) return downloadNext();
        size_t n = g.dlQueue.size(); g.dlQueue.clear();
        if (ok) refresh(n > 1 ? std::to_string(n) + " saved to your SD card" : "Saved to your SD card. Press A to install.");
    });
}

void activate() {
    Item* it = cur(); auto& t = ticks();
    if (plaza()) {
        if (!backend::online()) { backend::retryOnline(); if (!backend::online()) { sound::err(); return toast("Still offline"); } sound::ok(); g.ticks[1].clear(); loadStore(true); renderAll(DIR_F); return; }
        if (backend::store().failed) { sound::ok(); g.ticks[1].clear(); loadStore(true); renderAll(DIR_F); return; }   // "Try again"
        if (!storeUsable() || !it) return sound::err();
        if (!t.empty()) return download(t);
        return it->onSD ? installItem(it) : download({it});
    }
    if (!it) return sound::err();
    if (it->more) return switchSrc(1);
    if (g.type) return installItem(it);
    if (t.size() >= 2) return install(t, "Installing " + std::to_string(t.size()) + " themes as shuffle\xe2\x80\xa6");
    install({it}, "Installing " + it->name + "\xe2\x80\xa6");
}

void tick() {
    Item* it = cur(); auto& t = ticks();
    if (!plaza() && g.type && it && !it->more) {
        if (!isInst(it)) return sound::err();
        sound::untick();
        if (it->kind == K_BADGE) return startJob(backend::removeBadges(it), "Removing " + std::to_string(it->badgeCount) + " badges\xe2\x80\xa6", true, [](bool ok) { if (ok) { stage(DIR_F); toast("Badges removed from the HOME Menu"); } });
        backend::removeSplash();
        stage(DIR_F);
        return toast("Splash removed");
    }
    if (!it || it->more || (plaza() && (!storeUsable() || it->onSD))) return sound::err();
    auto f = std::find(t.begin(), t.end(), it);
    if (f != t.end()) { sound::untick((int)t.size()); t.erase(f); }
    else {
        if (!plaza() && t.size() >= 10) { sound::err(); return toast("Shuffle takes 10 themes at most"); }
        if (plaza() && t.size() >= 24) { sound::err(); return toast("24 at a time at most"); }
        t.push_back(it); sound::tick((int)t.size());
    }
}

void openSearch() {
    g.mode = MODE_SEARCH; g.sf.q = g.findQ; g.sf.tags = g.findTags; g.sf.f = 0; g.sf.reorder = false;
    sound::open(); g.toastClock.t = 1e9f; g.overClock.start(); stage(DIR_F); bgmQueue();
}
void sortOrSearch() { if (plaza() && (!backend::online() || !backend::store().ready)) return sound::err(); openSearch(); }

void preview(bool on) {
    Item* it = cur();
    if (on && (!it || it->more || it->kind == K_BADGE || (plaza() && !storeUsable()))) return sound::err();
    g.mode = on ? MODE_PREVIEW : MODE_BROWSE;
    on ? sound::open() : sound::close();
    g.modeClock.start();
    if (on) { if (!it->previewReady) backend::wantPreview(it); backend::wantFullPreview(it); }
    stage(DIR_F); bgmQueue();
}

void openMenu() { g.mode = MODE_MORE; g.menuFor = cur(); g.menu = 0; g.menuScroll.set(0); sound::open(); g.toastClock.t = 1e9f; g.overClock.start(); bgmQueue(); }
void closeOver(bool snd) { if (g.mode == MODE_QR) qr::stop(); g.mode = MODE_BROWSE; if (snd) sound::close(); stage(DIR_F); bgmQueue(); }

void menuChoose() {
    auto items = menuItems();
    if (g.menu < 0 || g.menu >= (int)items.size()) return;
    MenuItem m = items[g.menu];
    if (m.act == ACT_MUSIC) { sound::setTrack(sound::nextTrackSetting(sound::track())); sound::tick(1); return; }
    Item* it = cur();
    closeOver(false);
    switch (m.act) {
        case ACT_CLEAR: g.ticks[g.src].clear(); sound::untick(); break;
        case ACT_DEL: {
            Item* gone = deleteTarget(it);
            if (!gone) break;
            std::string name = gone->name;
            auto& t = g.ticks[0]; t.erase(std::remove(t.begin(), t.end(), gone), t.end());
            sound::wantTheme(nullptr);    // its music may be playing from the file about to go
            backend::deleteFromSD(gone);
            sound::untick(); renderAll(DIR_F); queuePreview(); toast(name + " deleted");
            break;
        }
        case ACT_BADGES: {
            auto& b = backend::home(K_BADGE);
            if (backend::scanning()) { sound::err(); toast("Still reading the SD card. Try again in a moment."); break; }
            if (b.empty()) { sound::err(); toast("No badges on your SD card yet. Theme Plaza has some."); break; }
            int n = 0; for (Item* x : b) n += x->badgeCount;
            sound::ok();
            startJob(backend::installBadges(b), "Installing " + std::to_string(n) + " badges\xe2\x80\xa6", true, [](bool ok) { if (ok) refresh("Badges added to your HOME Menu badge box"); });
            break;
        }
        case ACT_BGM_ONLY: install({it}, "Installing the music from " + it->name + "\xe2\x80\xa6", INSTALL_BGM_ONLY); break;
        case ACT_NO_BGM: install({it}, "Installing " + it->name + " without music\xe2\x80\xa6", INSTALL_NO_BGM); break;
        case ACT_DUMP:
            sound::ok();
            startJob(backend::dumpTheme(), "Copying the installed theme\xe2\x80\xa6", true, [](bool ok) { if (ok) { showAdded(); Item* a = backend::lastAdded(); refresh("Copied to your Collection as \xe2\x80\x9c" + (a ? a->name : std::string("Dumped theme")) + "\xe2\x80\x9d"); } });
            break;
        case ACT_EXIT: if (!dev::monkey()) g.quit = true; break;
        default: sound::ok(); toast(std::string(m.label) + " is not available yet"); break;
    }
}

void afterFind() {
    if (plaza()) { backend::storeInvalidate(); g.sel[1][g.type] = 0; g.ticks[1].clear(); loadStore(true); } else setSel(0);
    renderAll(DIR_F); queuePreview(); bgmQueue();
}
void doSearch() {
    std::string q = g.sf.q;
    while (!q.empty() && q.back() == ' ') q.pop_back();
    g.findQ = q; g.findTags = g.sf.tags; sound::ok(); g.mode = MODE_BROWSE; afterFind();
}
void closeSearch() { bool re = g.sf.reorder; g.mode = MODE_BROWSE; sound::close(); if (re) afterFind(); else { stage(DIR_F); bgmQueue(); } }
void clearFind() { g.findQ.clear(); g.findTags.clear(); sound::untick(1); afterFind(); toast("Search cleared"); }

int keyIndex(const char* k);
void typeKey(const char* k) {
    auto& f = g.sf;
    int ki = keyIndex(k); if (ki >= 0) g.keyFlash[ki].start();
    if (!strcmp(k, "clear")) { if (f.q.empty() && f.tags.empty()) return sound::err(); f.q.clear(); f.tags.clear(); sound::del(); }
    else if (!strcmp(k, "bksp")) { if (f.q.empty()) return sound::err(); f.q.pop_back(); sound::del(); }
    else { if (f.q.size() >= 24 || (k[0] == ' ' && f.q.empty())) return sound::err(); f.q += k; sound::key(); }
}

void sfToggle(int i) {
    auto its = sfItems();
    if (i < 0 || i >= (int)its.size()) return;
    auto& f = g.sf; f.f = i;
    if (its[i].tag) {
        auto j = std::find(f.tags.begin(), f.tags.end(), its[i].k);
        if (j != f.tags.end()) { sound::untick((int)f.tags.size()); f.tags.erase(j); } else { f.tags.push_back(its[i].k); sound::tick((int)f.tags.size()); }
    } else {
        if (plaza()) { g.order = its[i].k; backend::setSetting("order", g.order); } else { g.sort = its[i].k; backend::setSetting("sort", g.sort); }
        f.reorder = true; sound::tick(1);
    }
}

void sfMove(char btn) {
    std::vector<float> x, y, w; sfLayout(x, y, w);
    int n = (int)x.size(), i = g.sf.f;
    if (!n) return;
    if (btn == 'L') i = (i + n - 1) % n;
    else if (btn == 'R') i = (i + 1) % n;
    else {
        std::vector<float> tops;
        for (float v : y) if (std::find(tops.begin(), tops.end(), v) == tops.end()) tops.push_back(v);
        std::sort(tops.begin(), tops.end());
        int r = (int)(std::find(tops.begin(), tops.end(), y[i]) - tops.begin()) + (btn == 'U' ? -1 : 1);
        if (r < 0 || r >= (int)tops.size()) return sound::err();
        float cx = x[i] + w[i] / 2, bd = 1e9f;
        for (int k = 0; k < n; k++) if (y[k] == tops[r]) { float d = fabsf(x[k] + w[k] / 2 - cx); if (d < bd) { bd = d; i = k; } }
    }
    g.sf.f = i; sound::move();
}

void openQr() {
    if (!backend::online()) backend::retryOnline();
    if (!backend::online()) { sound::err(); return toast("Scanning a QR code needs internet"); }
    if (!qr::start()) { sound::err(); return toast("The camera could not be started"); }
    g.camSerial = 0; g.qrPause = 0;
    g.mode = MODE_QR; sound::open(); g.toastClock.t = 1e9f; g.overClock.start(); stage(DIR_F); bgmQueue();
}

int btnIndex(char k) { const char* all = "abxylrseUDLR"; const char* p = strchr(all, k); return p ? (int)(p - all) : 0; }

}  // namespace

// ---------- keyboard layout (shared with drawing) ----------
static std::vector<KeyDef> g_keys;
const std::vector<KeyDef>& keyDefs() {
    if (g_keys.empty()) {
        static const char* rows[4] = {"1234567890", "qwertyuiop", "asdfghjkl", "zxcvbnm"};
        static const int off[4] = {5, 5, 20, 36};
        static char names[40][2];
        int n = 0;
        for (int r = 0; r < 4; r++)
            for (int i = 0; rows[r][i]; i++) { names[n][0] = rows[r][i]; names[n][1] = 0; g_keys.push_back({names[n], off[r] + i * 31, 34 + r * 31, 29}); n++; }
        g_keys.push_back({"bksp", 253, 127, 62});
        g_keys.push_back({"clear", 5, 158, 56});
        g_keys.push_back({" ", 67, 158, 186});
    }
    return g_keys;
}
namespace { int keyIndex(const char* k) { auto& d = keyDefs(); for (size_t i = 0; i < d.size(); i++) if (!strcmp(d[i].k, k)) return (int)i; return -1; } }

// ---------- shared helpers ----------
bool storeUsable() { return plaza() && backend::online() && backend::store().ready && !backend::store().failed; }
Item* cur() { int s = getSel(); return s >= 0 && s < (int)g.view.size() ? g.view[s] : nullptr; }
int tickIndex(Item* it) { auto& t = ticks(); auto f = std::find(t.begin(), t.end(), it); return f == t.end() ? -1 : (int)(f - t.begin()); }
bool isInst(Item* it) {
    if (!it || it->more) return false;
    if (it->kind != K_THEME) {
        if (!it->plaza || it->path.empty()) return it->installed;
        for (Item* x : backend::home(it->kind)) if (x->path == it->path) return x->installed;
        return false;
    }
    // a catalogue entry that has been downloaded stands for the same file as its Collection entry
    for (Item* x : backend::installedThemes()) if (x == it || (!it->path.empty() && x->path == it->path)) return true;
    return false;
}
int homeCount() { int n = 0; for (Item* it : g.view) if (!it->more) n++; return n; }

std::string statusText() {
    int n = homeCount();
    int done = 0, total = 0;
    if (!plaza() && backend::scanProgress((Kind)g.type, done, total)) return "Reading the SD card \xc2\xb7 " + std::to_string(done) + " of " + std::to_string(total);
    if (plaza() && !backend::online()) return "Offline";
    if (findOn()) {
        std::string t = g.findQ.empty() ? "Tagged" : "\xe2\x80\x9c" + g.findQ + "\xe2\x80\x9d";
        t += " \xc2\xb7 ";
        if (plaza() && !backend::store().ready) return t + "searching";
        return t + std::to_string(n) + (plaza() && backend::store().more ? "+" : "") + (n == 1 ? " result" : " results");
    }
    if (plaza()) return std::string(backend::storeCount((Kind)g.type)) + " \xc2\xb7 " + lower(ORDER_NAMES[g.order]);
    return std::to_string(n) + " " + (n == 1 ? KIND_SINGLE[g.type] : KIND_PLURAL[g.type]) + " on SD \xc2\xb7 " + (g.sort == 2 ? "newest first" : "by " + lower(SORT_NAMES[g.sort]));
}

std::vector<FootBtn> footSpec() {
    Item* it = cur(); int n = (int)ticks().size();
    std::vector<FootBtn> v;
    if (plaza()) {
        if (!backend::online() || backend::store().failed) v = {{'x', "Search", true}, {'y', "Preview", true}, {'b', "Tick", true}, {'a', "Try again", false}};
        else {
            bool ready = backend::store().ready, ok = ready && it;
            v = {{'x', "Search", !ready}, {'y', "Preview", !ok || it->kind == K_BADGE}, {'b', ok && tickIndex(it) >= 0 ? "Untick" : "Tick", !ok || it->onSD},
                 {'a', n ? "Download " + std::to_string(n) : ok && it->onSD ? "Install" : "Download", !ok}};
        }
    } else if (!it || it->more) v = {{'x', "Search", false}, {'y', "Preview", true}, {'b', "Tick", true}, {'a', "Open", false}};
    else if (g.type) v = {{'x', "Search", false}, {'y', "Preview", g.type == 2}, {'b', "Remove", !isInst(it)}, {'a', "Install", false}};
    else v = {{'x', "Search", false}, {'y', "Preview", false}, {'b', tickIndex(it) >= 0 ? "Untick" : "Tick", false}, {'a', n >= 2 ? "Shuffle \xc3\x97" + std::to_string(n) : "Install", false}};
    if (n >= 2) v.erase(v.begin() + 1);   // with a batch ticked, Preview leaves the bar (Y still works)
    return v;
}

// What "Delete from SD card" removes for the selected item: the item itself in the Collection, its downloaded
// copy on the Theme Plaza tab. Null when there is nothing of its own to delete.
Item* deleteTarget(Item* it) {
    if (!it || it->more) return nullptr;
    Item* t = it;
    if (it->plaza) {
        t = it->onSD ? backend::collectionItem(it->plazaId) : nullptr;
        if (!t && it->onSD && !it->path.empty()) for (Item* x : backend::home(it->kind)) if (x->path == it->path) t = x;
    }
    return backend::deletable(t) ? t : nullptr;
}

std::vector<MenuItem> menuItems() {
    Item* it = cur();
    bool real = it && !it->more, th = real && it->kind == K_THEME && (!plaza() || it->onSD);
    std::vector<MenuItem> v;
    if (th) { v.push_back({"Install BGM only", ACT_BGM_ONLY, false}); v.push_back({"Install without BGM", ACT_NO_BGM, false}); }
    if (!ticks().empty()) v.push_back({"Clear ticks", ACT_CLEAR, false});
    if (deleteTarget(it)) v.push_back({"Delete from SD card", ACT_DEL, true});
    v.push_back({"App music", ACT_MUSIC, false});
    v.push_back({"Install all badges on the SD card", ACT_BADGES, false});
    v.push_back({"Dump the installed theme", ACT_DUMP, false});
    v.push_back({backend::needsReboot() ? "Exit and restart to apply" : "Exit Theme Plaza", ACT_EXIT, false});
    return v;
}

int menuFirst(int count) {
    if (count <= 7) return 0;
    return std::max(0, std::min(count - 7, g.menu - 3));
}

std::vector<SfItem> sfItems() {
    std::vector<SfItem> v;
    if (plaza()) for (int i = 0; i < NTAGS; i++) v.push_back({true, i, TAGS[i]});
    // the Collection's orders are offered with "Recently added" (the default) first; the numbers kept in the settings stay as they were
    static const int SORT_SHOWN[3] = {2, 0, 1};
    for (int i = 0; i < 3; i++) { int k = plaza() ? i : SORT_SHOWN[i]; v.push_back({false, k, plaza() ? ORDER_NAMES[k] : SORT_NAMES[k]}); }
    return v;
}

void sfLayout(std::vector<float>& x, std::vector<float>& y, std::vector<float>& w) {
    auto its = sfItems();
    int nt = plaza() ? NTAGS : 0;
    float cx = 16, cy = 108;
    for (int i = 0; i < (int)its.size(); i++) {
        if (i == nt) { cx = 16; cy = nt ? 176 : 113; }
        float cw = gfx::textWidth(gfx::F11, its[i].label) + 18;   // fractional, as the browser lays the row out; drawing snaps each chip
        if (cx + cw > 16 + 372 && cx > 16) { cx = 16; cy += 25; }
        x.push_back(cx); y.push_back(cy); w.push_back(cw);
        cx += cw + 5;
    }
}

void toast(const std::string& t) { g.toastText = t; g.toastClock.start(); }

void press(char key) {
    g.btnFlash[btnIndex(key)].start();
    if (g.job) return;
    bool arrow = key == 'U' || key == 'D' || key == 'L' || key == 'R';
    if (g.mode == MODE_PREVIEW) { if (key == 'b' || key == 'y') preview(false); else if (!arrow) sound::err(); return; }
    if (g.mode == MODE_MORE) {
        int n = (int)menuItems().size();
        if (key == 'U' || key == 'D') { g.menu = (g.menu + (key == 'U' ? n - 1 : 1)) % n; sound::move(); }
        else if (key == 'a') menuChoose();
        else if (key == 'b' || key == 's') closeOver(true);
        return;
    }
    if (g.mode == MODE_SEARCH) {
        if (arrow) sfMove(key); else if (key == 'a') sfToggle(g.sf.f); else if (key == 'b') closeSearch(); else if (key == 's') doSearch();
        else if (key == 'x') typeKey("bksp"); else if (key == 'y') typeKey(" ");
        return;
    }
    if (g.mode == MODE_QR) { if (key == 'b' || key == 'e') closeOver(true); return; }
    if (arrow) return move(key);
    if (key == 'l' || key == 'r') return switchSrc(key == 'l' ? 0 : 1);
    if (g.focus == FOCUS_SEL && (key == 'a' || key == 'b')) return setFocus(FOCUS_MAIN);
    if (g.focus == FOCUS_SEL) g.focus = FOCUS_MAIN;
    switch (key) {
        case 'a': activate(); break;
        case 'b': tick(); break;
        case 'x': sortOrSearch(); break;
        case 'y': preview(true); break;
        case 's': openMenu(); break;
        case 'e': openQr(); break;
    }
}

// ---------- touch ----------

static void touch(int px, int py) {
    if (g.job) return;
    if (g.mode == MODE_SEARCH) {
        auto& keys = keyDefs();
        for (auto& k : keys) if (Rect{(float)k.x, (float)k.y, (float)k.w, 29}.has(px, py)) return typeKey(k.k);
        if (Rect{8, 206, 84, 26}.has(px, py)) return press('b');
        if (Rect{206, 206, 106, 26}.has(px, py)) return press('s');
        return;
    }
    if (g.mode == MODE_MORE) {
        auto items = menuItems();
        int first = menuFirst((int)items.size());
        for (int i = first; i < (int)items.size() && i < first + 7; i++)
            if (Rect{10, 28.0f + 34 + (i - first) * 25, 300, 24}.has(px, py)) { g.menu = i; return menuChoose(); }
        if (py < 28) return press('b');
        return;
    }
    if (g.mode == MODE_PREVIEW) { if (Rect{320 - 8 - 70, 240 - 8 - 22, 70, 22}.has(px, py)) press('b'); return; }
    if (g.mode == MODE_QR) return;
    // header
    if (Rect{238, 2, 48, 24}.has(px, py)) return press('e');
    if (Rect{288, 2, 30, 24}.has(px, py)) return press('s');
    if (Rect{29, 2, 62, 26}.has(px, py)) { if (g.src != 0) switchSrc(0); return; }
    if (Rect{93, 2, 80, 26}.has(px, py)) { if (g.src != 1) switchSrc(1); return; }
    // footer
    auto spec = footSpec(); std::vector<Rect> fr; footLayout(spec, fr);
    for (size_t i = 0; i < spec.size(); i++) if (Rect{fr[i].x, 207, fr[i].w, 33}.has(px, py)) return press(spec[i].key);
    if (py >= 207) return;
    // search chip and type selector
    if (findOn() && !(plaza() && !backend::online()) && chipRect().has(px, py)) return clearFind();
    if (!(plaza() && !backend::online())) {
        Rect sr[3]; segLayout(sr);
        for (int i = 0; i < 3; i++) if (Rect{sr[i].x, sr[i].y - 3, sr[i].w, sr[i].h + 6}.has(px, py)) { if (i != g.type) setType(i); return; }
    }
    // tiles and rows: first tap selects, tapping the selected one ticks it
    int hit = -1;
    if (plaza()) {
        if (!storeUsable()) return;
        for (int i = 0; i < (int)g.rows.size(); i++) {
            float y = 28 + g.rows[i].y.to, h = i == getSel() ? 52 : 24;
            if (g.rows[i].placed && g.rows[i].vis.to > 0.5f && y > 28 + 24 && Rect{6, y, 296, h}.has(px, py)) hit = i;
        }
    } else {
        int page = getSel() / 10;
        for (int i = page * 10; i < (int)g.view.size() && i < page * 10 + 10; i++) {
            float x = 11 + (i % 5) * 61, y = 28 + 58 + ((i % 10) / 5) * 59;
            if (Rect{x - 3, y - 2, 60, 58}.has(px, py)) hit = i;
        }
    }
    if (hit < 0) return;
    if (g.focus == FOCUS_SEL) g.focus = FOCUS_MAIN;
    if (hit != getSel()) return select(hit, hit > getSel() ? DIR_R : DIR_L);
    Item* it = cur();
    if (it->more) press('a'); else if (plaza() || g.type == 0) press('b');
}

// ---------- per-frame ----------
void init() {
    static const char* names[3] = {"Get more themes", "Get more splashes", "Get more badges"};
    for (int i = 0; i < 3; i++) { g.more[i].more = true; g.more[i].name = names[i]; g.more[i].kind = (Kind)i; }
    g.tabAct[0].set(1); g.typeOn[0].set(1);
    g.sort = std::max(0, std::min(2, backend::setting("sort", 2)));     // 2: recently added first
    g.order = std::max(0, std::min(2, backend::setting("order", 0)));
    renderAll(DIR_F);
    queuePreview();
}

void update(float dt) {
    g.T += dt;
    { SLOW("backend update"); backend::update(dt); }

    // a finished operation
    if (g.job && g.job->done) {
        Job* j = g.job; bool ok = j->ok; std::string err = j->error;
        std::string note = j->note;
        Item* keep = cur();            // the list may gain or lose entries; stay on the same item
        g.keepSel = nullptr;
        auto done = std::move(g.jobDone);
        g.job = nullptr; g.busyModal = false; g.jobDone = nullptr; g.jobItems.clear();
        backend::freeJob(j);
        if (!ok) { sound::err(); toast(err.empty() ? "That did not work" : err); }
        if (done) done(ok);
        if (ok && !note.empty() && !g.job) toast(note);
        rebuildView();
        if (g.keepSel) keep = g.keepSel;   // the finished job asked for a particular item to be selected
        if (keep) { auto f = std::find(g.view.begin(), g.view.end(), keep); if (f != g.view.end()) setSel((int)(f - g.view.begin())); }
        clampSel();
        g.rows.resize(g.view.size());
        g.homeRev = backend::revision();
    }
    // No sleep or HOME while files are being written. After a theme or badge install the HOME button stays
    // off for the rest of the session: the HOME Menu would come back with its old settings in memory and
    // could write them over the new ones. Leaving through the More menu restarts the console instead.
    bool lock = g.job != nullptr || backend::needsReboot();
    if (lock != g.homeLocked) { g.homeLocked = lock; aptSetHomeAllowed(!lock); }
    aptSetSleepAllowed(g.job == nullptr);
    if (aptCheckHomePressRejected() && !g.job) { sound::err(); toast("To apply what you installed, leave with START, then Exit. The console restarts."); }
    if (!g.soundChecked && !backend::scanning()) {
        g.soundChecked = true;
        if (!sound::available()) toast("No sound: dump the DSP firmware from Luma's Rosalina menu (Miscellaneous options)");
    }
    // the Collection changing underneath: the first scan finishing, a download arriving, a file deleted
    bool scanEnded = g.wasScanning && !backend::scanning();
    if (scanEnded) g.wasScanning = false;
    if (backend::revision() != g.homeRev || scanEnded) {
        g.homeRev = backend::revision();
        if (!plaza()) {
            // the first items to arrive come in with the page's entrance; later changes keep the cursor on its item
            bool first = homeCount() == 0;
            Item* keep = cur();
            rebuildView();
            auto f = std::find(g.view.begin(), g.view.end(), keep);
            if (keep && !keep->more && f != g.view.end()) setSel((int)(f - g.view.begin()));
            clampSel(); g.rows.resize(g.view.size());
            if (first) { renderAll(DIR_F); queuePreview(); }
        }
    }
    // the Theme Plaza list arriving
    auto& st = backend::store();
    if (plaza() && st.ready && !g.storeWasReady) {
        g.storeWasReady = true;
        if (g.mode != MODE_SEARCH) { buildPane(DIR_F); stage(DIR_F); queuePreview(); bgmQueue(); }
    }
    if (!st.ready) g.storeWasReady = false;
    if (st.sortFailed) { st.sortFailed = false; if (g.order != 0) { g.order = 0; toast("Sorting is not available right now. Showing the newest."); } }
    if (st.moreFailed) { st.moreFailed = false; if (plaza()) toast("The rest of the list could not be loaded"); }
    // offline: the type selector is not shown, so the cursor cannot stay on it
    if (plaza() && !backend::online() && g.focus == FOCUS_SEL) g.focus = FOCUS_MAIN;
    // the More menu was opened for one item; if the list under it changes, its entries would shift under the cursor
    if (g.mode == MODE_MORE && cur() != g.menuFor) closeOver(false);
    // rows keep arriving after the list first shows; further pages are fetched when the cursor nears the end
    if (plaza() && st.ready && g.storeWasReady && g.view.size() != st.list.size()) {
        Item* keep = cur();
        g.view = st.list; g.rows.assign(g.view.size(), RowAnim());
        auto f = std::find(g.view.begin(), g.view.end(), keep);
        if (keep && f != g.view.end()) setSel((int)(f - g.view.begin()));
        clampSel();
    }
    if (plaza() && st.ready && st.more && !st.loading && getSel() >= (int)g.view.size() - 8) backend::storeMore();
    // preview of the resting item
    if (g.pvWait >= 0) {
        g.pvWait -= dt;
        // Theme Plaza: the network is only asked once the cursor has rested for a third of a second, but a
        // preview kept from an earlier visit can be shown sooner
        if (plaza() && !g.pvEarly && g.pvWait < 0.22f) { g.pvEarly = true; if (Item* it = cur(); it && !it->more && !it->previewReady) backend::wantPreview(it, true, it->path.empty()); }   // one that is on the SD card needs no network at all
        if (g.pvWait < 0) {
            Item* it = cur();
            if (it && !it->more) { if (!it->previewReady) backend::wantPreview(it); if (it->plaza) backend::wantStats(it); }
            // in the Collection, also get the neighbours ready so that moving on shows a picture at once
            if (!plaza()) for (int d : {1, -1, 5, -5, 2}) {
                int i = getSel() + d;
                if (i >= 0 && i < (int)g.view.size() && !g.view[i]->more && !g.view[i]->previewReady) backend::wantPreview(g.view[i], false);
            }
        }
    }
    // The resting item's preview counts as in use for as long as it is shown, and is asked for again if it
    // was dropped to make room for others.
    if (Item* it = cur(); it && !it->more && !g.job) {
#ifdef THEME_PLAZA_DEV
        // how long the selected item's preview took to appear (dev log)
        static float waited = 0; static Item* waitedFor = nullptr;
        if (it != waitedFor) { waitedFor = it; waited = 0; }
        if (!it->previewReady) waited += dt;
        else if (waited > 0) { LOG("preview of the selected item after %.0f ms", waited * 1000); waited = 0; }
#endif
        if (it->previewReady) backend::wantPreview(it);
        else if (g.pvWait < 0 && !(plaza() && !storeUsable())) g.pvWait = plaza() ? 0.32f : 0.10f;   // ask again shortly; nothing queued is dropped
    }

    bgmQueue();
    SLOW("ui input and animation");

    // QR scanning: show the camera picture and act on a code as soon as one is read
    qr::update();
    if (g.mode == MODE_QR) {
        SLOW("qr step");
        if (!qr::active()) { bool failed = qr::failed(); closeOver(false); if (failed) { sound::err(); toast("The camera could not be started"); } }
        else {
            if (!g.camTex.ok && C3D_TexInit(&g.camTex.tex, qr::TEX_W, qr::TEX_H, GPU_RGB565)) {
                C3D_TexSetFilter(&g.camTex.tex, GPU_LINEAR, GPU_LINEAR);
                g.camTex.ok = true; g.camTex.w = qr::W; g.camTex.h = qr::H;
            }
            if (g.camTex.ok && qr::picture((u8*)g.camTex.tex.data, g.camSerial)) C3D_TexFlush(&g.camTex.tex);
            g.qrPause -= dt;
            std::string text;
            if (qr::take(text) && g.qrPause <= 0) {
                long id = plaza::idFromLink(text);
                if (!id) { sound::err(); toast("That is not a Theme Plaza code"); g.qrPause = 2.5f; }
                else if (Item* have = backend::collectionItem(id)) {
                    closeOver(false);
                    sound::ok(); showAdded(have); toast("Already in your Collection");
                } else {
                    closeOver(false);
                    sound::ok();
                    startJob(backend::downloadId(id), "Downloading\xe2\x80\xa6", true, [](bool ok) { if (ok) { sound::done(); showAdded(); } });
                }
            }
        }
    }

    const auto& in = input::get();
    struct { u32 k; char c; } map[] = {{input::A, 'a'}, {input::B, 'b'}, {input::X, 'x'}, {input::Y, 'y'}, {input::L, 'l'}, {input::R, 'r'}, {input::START, 's'}, {input::SELECT, 'e'}};
    {
#ifdef THEME_PLAZA_DEV
        u64 t0 = svcGetSystemTick(); int modeWas = g.mode;
#endif
        for (auto& m : map) if (in.down & m.k) press(m.c);
        if (in.repeat & input::UP) press('U');
        if (in.repeat & input::DOWN) press('D');
        if (in.repeat & input::LEFT) press('L');
        if (in.repeat & input::RIGHT) press('R');
        if (in.touchDown) touch(in.tx, in.ty);
#ifdef THEME_PLAZA_DEV
        float ms = (float)(svcGetSystemTick() - t0) * 1000.0f / SYSCLOCK_ARM11;
        if (ms > 2.0f) LOG("slow: input down %04lx repeat %04lx touch %d (%d,%d), mode %d to %d, page %d, took %.1f ms", (unsigned long)in.down, (unsigned long)in.repeat, (int)in.touchDown, (int)in.tx, (int)in.ty, modeWas, (int)g.mode, g.src, ms);
#endif
    }

    // animation clocks and tweens
    g.stageClock.step(dt); g.paneClock.step(dt); g.toastClock.step(dt); g.overClock.step(dt); g.chipClock.step(dt); g.modeClock.step(dt);
    for (auto& c : g.btnFlash) c.step(dt);
    for (auto& c : g.keyFlash) c.step(dt);
    for (auto& m : g.marks) m.second.second.step(dt);
    for (int i = 0; i < 2; i++) { g.tabAct[i].go(g.src == i ? 1 : 0, 0.12f); g.tabAct[i].step(dt); }
    for (int i = 0; i < 3; i++) { g.typeOn[i].go(g.type == i ? 1 : 0, 0.15f); g.typeOn[i].step(dt); }
    g.focusSel.go(g.focus == FOCUS_SEL ? 1 : 0, 0.15f); g.focusSel.step(dt);
    g.curX.step(dt); g.curY.step(dt); g.scrollY.step(dt); g.menuScroll.step(dt);
    for (auto& t : g.tileSel) t.step(dt);
    for (auto& r : g.rows) { r.y.step(dt); r.sel.step(dt); r.vis.step(dt); }
    bool chip = findOn() && !(plaza() && !backend::online());
    if (chip && !g.chipShown) g.chipClock.start();
    g.chipShown = chip;
    if (g.job) {
        // a shuffle install: the top screen shows the theme being written, as if the cursor moved to it
        int s = g.job->step;
        if (s >= 0 && s < (int)g.jobItems.size() && g.jobItems[s] != cur()) {
            auto f = std::find(g.view.begin(), g.view.end(), g.jobItems[s]);
            if (f != g.view.end()) {
                int i = (int)(f - g.view.begin()), old = getSel();
                setSel(i);
                if (!plaza() && i / 10 != old / 10) { buildPane(i > old ? DIR_R : DIR_L); setSel(i); }
                stage(i > old ? DIR_R : DIR_L);
                queuePreview();
            }
        }
        g.jobShown += (g.job->progress - g.jobShown) * std::min(1.0f, dt * 12);
        // the ticking while it works: rising with the progress of an install, at random pitches during a download
        if ((g.progTick -= dt) <= 0) {
            g.progTick = g.busyModal ? 0.14f : 0.16f;
            sound::prog(g.busyModal ? g.job->progress : (rand() % 1000) / 999.0f);
        }
    } else g.progTick = 0.14f;
}

}  // namespace ui
