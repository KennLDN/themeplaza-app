// What the UI shows: themes, splashes and badge sets, either on the SD card or in the Theme Plaza catalogue.
#pragma once
#include <3ds.h>
#include <string>
#include <vector>
#include "tex.h"

enum Kind { K_THEME = 0, K_SPLASH = 1, K_BADGE = 2 };

struct Item {
    Kind kind = K_THEME;
    bool more = false;            // the "Get more" tile at the end of the Collection grid
    bool plaza = false;           // an entry from the Theme Plaza catalogue
    long plazaId = 0;
    std::string name, author, desc;
    std::string path;             // zip file or folder on the SD card
    std::vector<std::string> tags;
    bool bgm = false;
    int downloads = 0, likes = 0;
    int badgeCount = 0;
    bool onSD = false;            // catalogue entry that has been downloaded
    bool installed = false;       // splash / badge set currently installed
    u32 order = 0;                // when it was added (for "recently added")

    // artwork; filled in by the loader, possibly after the item first appears
    Tex icon48, icon24;           // rounded corners baked into the alpha
    Tex wallTop, wallBot;         // wallpapers at half size, as the preview cards show them
    Tex fullTop, fullBot;         // the same at full size; only loaded for the item in full-screen preview
    bool previewReady = false;    // wallpapers and colours are loaded
    bool previewWanted = false;
    bool previewAskedAgain = false;   // asked for urgently while an earlier, weaker request was still queued
    u8 previewTries = 0;              // a catalogue item's pictures that failed to arrive are tried a few times
    u32 colBar = 0xff7a7415, colTile = 0xfff6fbe9, colGlyph = 0xff8e9122;   // ABGR, used by the overlay
    std::vector<Tex> badgeArt;    // badge sets: the first few badge images

    // bookkeeping for the backend
    int iconSlot = -1;            // Collection items: record number in the icon cache file
    std::vector<u8> icon565;      // Theme Plaza items: the 48x48 icon as downloaded (RGB565, tiled)
    u32 fileSize = 0;             // size of the zip, to notice when it changes
    u32 bodySize = 0, bodyCrc = 0;   // theme body_LZ.bin / splash.bin, used to recognise what is installed
    u32 auxSize = 0, auxCrc = 0;     // theme bgm.bcstm size / splashbottom.bin checksum
    u32 iconUsed = 0, previewUsed = 0;   // frame numbers, to free what was shown least recently
    bool iconWanted = false;
    bool statsReady = false, statsWanted = false;   // Theme Plaza: downloads, likes, tags, description
    int storeRank = 0;            // Theme Plaza: position in the current list
    bool gone = false;            // deleted from the SD card
};
