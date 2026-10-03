// Bytes that arrive from the network while another thread is already reading them: the music preview
// of a Theme Plaza item starts playing as soon as its beginning is there.
#pragma once
#include <3ds.h>
#include <vector>

struct StreamBuf {
    LightLock lock;                 // guards data
    std::vector<u8> data;
    volatile bool finished = false; // nothing more will be added
    StreamBuf() { LightLock_Init(&lock); }
};
