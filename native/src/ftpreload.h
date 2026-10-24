#pragma once
#include <cstdint>
// Fast-travel preload (WHGame 1.5.6, user 2026-10-23: "0 loading time when the monopoly piece gets to the destination").
// The map's fast travel walks a piece for up to ~22 s with the disk idle, then streams the destination behind a black
// screen (12.5 s on an 8.7 km trip). This asks the engine's own streaming to precache the destination WHILE the piece
// walks (I3DEngine::AddPrecachePoint, the cutscene precache), refreshed every few seconds, and stops asking the moment
// the walk stops (arrival or a random encounter), so an encounter loads where Henry actually is.
// Per frame: ~8 pointer reads under SEH. docs/whgame_re/fast_travel.md has the addresses.
// Modes ("[DN] ftpre N"): 0 off, 1 log only, 2 log + precache (default).
void Ftpre_Install(uintptr_t env, uintptr_t gameBase);
void Ftpre_Frame();                   // once a drawn frame, main thread (fastload_main.cpp)
void Ftpre_Command(const char* args);
