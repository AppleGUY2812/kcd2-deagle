#pragma once
#include <cstdint>
// keepload.cpp: the map's screen handler (FUN_180562300) calls this first, through menutime's hook when menutime is on,
// else through keepload's own: a kept map that a save load or a fast travel made stale is unloaded the game's way.
void Keepload_MapScreen(void* panel, uint64_t prev, uint8_t next);
// menutime.cpp: true when menutime hooked the map's screen handler (it then calls Keepload_MapScreen first).
bool Menutime_HooksMapHandler();
