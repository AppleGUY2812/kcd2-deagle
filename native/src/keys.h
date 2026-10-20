#pragma once
#include <cstdint>
#include "scan.h"
// Key presses from Lua through KCD2's own input system, the way its keyboard posts them (keys.cpp). For the AFK walk
// (~/kcd2-afkwalk, the other session's mod): Lua's SimulateOnAction doesn't move Henry.
//   key <name> down   press once (KCD2's key names: w a s d space lshift lctrl e q ...)
//   key <name> up     release (and stop holding)
//   key <name> hold   pressed until "up", pressed again whenever KCD2 lets go of it while its window is in the
//                     background (focus loss clears the keys); in the foreground the real keyboard has it
//   key allup         release every key we hold or pressed
void Keys_Install(const Module& game, uintptr_t env);
void Keys_Command(const char* rest);   // any thread: queued
void Keys_Frame();                     // main thread, every frame (the camera step)
void Keys_Reset();                     // a load ("hello"): release ours
