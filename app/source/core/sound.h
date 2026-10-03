// UI sounds and music. The UI sounds are pre-rendered PCM clips (romfs:/audio/sfx.bin, see audio_gen.h).
#pragma once
struct Item;

namespace sound {
void init();
void fini();
void update(float dt);
bool available();              // false when the console's sound could not be started

// UI effects
void move(); void page(); void tab(); void ok(); void back(); void tick(int n); void untick(int n = 1);
void err(); void done(); void open(); void close(); void prog(float p); void key(); void del();

// music: the app's own tracks, and the selected theme's BGM preview
enum { MUSIC_OFF = -1, MUSIC_SHUFFLE = 3 };    // besides the tracks 0..2
const char* trackName(int i);     // also "Off" and "Shuffle"
int track();                      // the setting: a track, MUSIC_SHUFFLE or MUSIC_OFF
void setTrack(int i);
int nextTrackSetting(int i);      // the order the menu steps through them: Shuffle, the tracks, Off
// Tells the mixer which item's own music should play (null: none). quiet: with no item, keep the app's
// music down as well (a catalogue item's music is on its way).
void wantTheme(Item* it, bool quiet = false);
}
