#pragma once
#include <cstdint>
// ashang.cpp: names the Flash (AS2) script function running when the main thread stops making frames.
void AsHang_Install(uintptr_t whgameBase);
void AsHang_Frame();   // from the frame tick, main thread
void AsHang_TestStart();                              // tests only: this thread is "main", start the sampler
void AsHang_TestCall(void* fn, void (*body)());       // tests only: run body as the Flash function object fn
