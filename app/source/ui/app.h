// The whole user interface: state, input handling and drawing.
#pragma once
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>
#include "../core/backend.h"
#include "anim.h"
#include "gfx.h"
#include "input.h"

namespace ui {

enum Focus { FOCUS_MAIN, FOCUS_SEL };
enum Mode { MODE_BROWSE, MODE_PREVIEW, MODE_MORE, MODE_SEARCH, MODE_QR };
enum Dir { DIR_F = 0, DIR_R = 1, DIR_L = 2 };

constexpr int NTAGS = 10;
extern const char* const TAGS[NTAGS];
extern const char* const SRC_NAMES[2];
extern const char* const TYPE_NAMES[3];

struct FootBtn { char key; std::string label; bool off; };

struct RowAnim { anim::Tween y, sel, vis; bool placed = false; };   // position, how selected, how visible

struct App {
    // ---- navigation state ----
    int src = 0;                 // 0 Collection, 1 Theme Plaza
    int type = 0;                // 0 themes, 1 splashes, 2 badges
    Focus focus = FOCUS_MAIN;
    Mode mode = MODE_BROWSE;
    int sort = 2;                // Collection: 0 name, 1 creator, 2 recently added (the default)
    int order = 0;               // Theme Plaza: 0 newest, 1 most downloaded, 2 most liked
    int sel[2][3] = {};
    std::vector<Item*> ticks[2];
    std::string findQ;
    std::vector<int> findTags;
    struct { std::string q; std::vector<int> tags; int f = 0; bool reorder = false; } sf;
    int menu = 0;
    bool quit = false;

    std::vector<Item*> view;     // what the current page lists (sorted and filtered)
    Item more[3];                // the "Get more" tiles

    // ---- long operations ----
    Item* menuFor = nullptr;                       // the item the More menu was opened on
    Job* job = nullptr;
    std::string jobText;
    std::function<void(bool ok)> jobDone;
    std::vector<Item*> jobItems;      // a shuffle install's themes, in the order it writes them
    bool busyModal = false;
    std::vector<Item*> dlQueue; size_t dlPos = 0; Item* dlItem = nullptr;
    bool storeWasReady = false;
    bool soundChecked = false, homeLocked = false;
    Item* keepSel = nullptr;
    int homeRev = -1; bool wasScanning = true;   // to notice the Collection changing underneath

    // ---- animation state ----
    float T = 0;                                   // seconds since start
    anim::Clock stageClock; Dir stageDir = DIR_F; bool chipPop = false;
    anim::Clock paneClock; Dir paneDir = DIR_F;
    anim::Tween curX, curY; bool curPlaced = false;
    anim::Tween tileSel[10];
    std::vector<RowAnim> rows;
    anim::Tween scrollY;
    anim::Tween menuScroll;                        // More menu: rows scrolled off the top when it has more entries than fit
    anim::Tween tabAct[2], typeOn[3], focusSel;
    anim::Clock btnFlash[12];
    anim::Clock keyFlash[48];
    anim::Clock toastClock; std::string toastText;
    anim::Clock overClock;                         // dim / sheet / modal entrance
    anim::Clock chipClock; bool chipShown = false;
    anim::Clock modeClock;                         // preview fade
    std::unordered_map<Item*, std::pair<int, anim::Clock>> marks;   // marker state per item and when it appeared
    float pvWait = -1;                             // Theme Plaza: time until the preview of the resting item is requested
    bool pvEarly = false;                          // the cache has been asked for the resting item already
    float jobShown = 0;                            // progress bar value as drawn
    float progTick = 0;                            // time until the next progress blip
    Tex camTex; u32 camSerial = 0; float qrPause = 0;   // QR scanning: the camera picture, and a pause after a code that was no use
};

extern App g;

void init();
void update(float dt);       // input + logic + animation clocks
void drawTop();
void drawBottom();

// ---- helpers shared by logic and drawing ----
inline bool plaza() { return g.src == 1; }
inline int getSel() { return g.sel[g.src][g.type]; }
inline void setSel(int i) { g.sel[g.src][g.type] = i; }
inline std::vector<Item*>& ticks() { return g.ticks[g.src]; }
inline bool findOn() { return !g.findQ.empty() || !g.findTags.empty(); }
bool storeUsable();
Item* cur();
bool isInst(Item* it);
int tickIndex(Item* it);     // -1 when not ticked
std::vector<FootBtn> footSpec();
int homeCount();             // items in the current Collection view, not counting the "Get more" tile
std::string statusText();
struct MenuItem { const char* label; int act; bool red; };
std::vector<MenuItem> menuItems();
Item* deleteTarget(Item* it);
int menuFirst(int count);   // More menu: index of the first visible entry (7 fit on screen)
struct SfItem { bool tag; int k; const char* label; };
std::vector<SfItem> sfItems();
void sfLayout(std::vector<float>& x, std::vector<float>& y, std::vector<float>& w);   // chip rectangles on the top screen
void press(char key);        // 'a','b','x','y','l','r','s' (START),'e' (SELECT),'U','D','L','R' (directions)
void toast(const std::string& t);

}  // namespace ui
