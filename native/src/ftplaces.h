#pragma once
#include <cstdint>
#include <windows.h>
// The fast-travel arrival's random-event places, loaded ahead. WHGame.dll 1.5.6, decoded 2026-10-24
// (~/re/work/ft_xml_stall/report.md, ~/re/work/ft_place_prefetch/report.md on the ProLiant; docs/whgame_re/fast_travel.md):
// every place whose trigger area the player enters while a fast travel runs (the player is put at the destination when
// the walk stops) goes into a list on the random-event manager (+0x60). When the arrival wait ends, the fast-travel END
// listener (FUN_182ece874) tries those places in a random order, and a place whose concept graph isn't loaded is loaded
// on the main thread right there: XML read and parse, then the graph build, 0.5-0.8 s of frozen frame per arrival.
// This watches that list each frame while a trip runs and (mode 2) asks the game's own streaming to load each new place
// in the background (FUN_18051a8e0 with sync 0, notify 0: the call the game uses for its walk-time prefetch, without
// the "start an event" notification). The END listener then finds the graphs loaded (or waits for one still loading,
// never worse than today), tries the same places in the same random order, and unloads what it didn't use, as before.
// Modes (fastload_ftplaces.txt next to the plugin, `ftplaces 0|1|2`): 0 off, 1 log only (default), 2 log + load ahead.
// Log: "ftplaces: ..." lines per trip: when each place entered the list (seconds after the walk ended) and what was
// asked; the arrival's frozen frame itself shows in the hitch: lines (hitch.cpp) and the readiness gate 0x3f91 line.
void Ftplaces_Install(uintptr_t gameBase, HMODULE self);
void Ftplaces_Frame();                    // main thread, once a drawn frame
void Ftplaces_Command(const char* args);  // "ftplaces [0|1|2]"
