#pragma once
#include <cstdint>
#include <windows.h>
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
// "fttex MB JOBS": the texture streamer's outstanding caps raised while the arrival wait runs (fastload_fttex.txt, off by default)
void Fttex_Install(HMODULE self);
void Fttex_Command(const char* args);
// "ftinstant S": the fast-travel arrival's readiness wait let in S seconds after it starts (fastload_ftinstant.txt, off by default)
void Ftinstant_Install(HMODULE self);
void Ftinstant_Command(const char* args);
// NPC detail switch budget raised while a readiness gate (0x3f91) holds the screen in play (readiness.cpp)
void Ftpre_GateNpc(bool on);
// fast-travel walks started so far (main thread)
unsigned Ftpre_TripSeq();
// ms since a fast-travel walk or arrival wait was last on (0xffffffff: none this run; main thread)
DWORD Ftpre_TripQuietMs();
// "ftspeed R" (fastload_ftspeed.txt, default 44): the fast-travel piece walks R game seconds per real second instead of
// the game's 0.5 s a frame (which made its speed the frame rate: Kuttenberg slower than Trosky); 0 = the game's step
void Ftspeed_Install(uintptr_t base, HMODULE self);
void Ftspeed_Command(const char* args);
