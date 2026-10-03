// Stand-ins so core/tex.cpp compiles on the PC; nothing here draws.
#pragma once
#include <cstdlib>
#include "3ds.h"
enum GPU_TEXCOLOR { GPU_RGBA8, GPU_RGB565 };
enum { GPU_LINEAR, GPU_CLAMP_TO_EDGE };
struct C3D_Tex { void* data; size_t size; u16 width, height; };
inline bool C3D_TexInit(C3D_Tex* t, int w, int h, GPU_TEXCOLOR f) { t->size = (size_t)w * h * (f == GPU_RGBA8 ? 4 : 2); t->data = malloc(t->size); t->width = w; t->height = h; return true; }
inline void C3D_TexDelete(C3D_Tex* t) { free(t->data); }
inline void C3D_TexSetFilter(C3D_Tex*, int, int) {}
inline void C3D_TexSetWrap(C3D_Tex*, int, int) {}
inline void C3D_TexFlush(C3D_Tex*) {}
