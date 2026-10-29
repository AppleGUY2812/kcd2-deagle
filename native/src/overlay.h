#pragma once
// deagle_native's own 2D drawing layer (work/BRIEF.md): text, rectangles and pictures (item icons) straight onto the
// game's DX12 back buffer, outside Scaleform. Producer/consumer: the screen code fills a list on the main thread, the
// render thread (our Present hook) draws it. No input, no windows: the display size comes from the swap chain.
#include <windows.h>

// Installs the DXGI / D3D12 hooks once, from the plugin's start-up thread (never the render thread). Safe to call
// twice; logs and stays off if the probe device, the vtables or MinHook fail.
void Overlay_Install(HMODULE self);

// "overlay test 1|0" and "overlay stats" (main.cpp's command list).
void Overlay_Command(const char* rest);

// ---- draw list (the producer side) -------------------------------------------------------------------------------
// Coordinates are pixels of the swap chain's back buffer, origin top-left. Colors are 0xRRGGBBAA (0xFFFFFFFF =
// white). One Begin ... End block is one frame; End hands it to the render thread, which draws the latest one and
// ignores an unchanged list. Text is UTF-8; the built-in font has ASCII/Latin glyphs only.
void Ovl_Begin();
void Ovl_Rect(float x, float y, float w, float h, unsigned rgba);
void Ovl_Text(float x, float y, float size_px, unsigned rgba, const char* utf8);
void Ovl_Image(float x, float y, float w, float h, unsigned texId);
void Ovl_End();

// Registers a .png (any size up to 8192 px, RGBA) and returns its texId at once; the render thread decodes it and
// uploads it to an SRV in the ImGui heap on the next frame (an image drawn before that simply isn't drawn yet).
// 0 = failed (queue full, no slot left, or the file couldn't be read - logged once).
unsigned Ovl_LoadImage(const char* path);
