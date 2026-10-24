#pragma once
#include <cstdint>
// Storm (wh::rpgmodule::storm) applies its 6,444 rules to every character when a save loads (the apply step, ~1 s on
// Main on boot 40, and again on a job worker at level load). Rules share 298 named condition sets
// (C_CustomSelector, e.g. isValidOpenworldNpc, 100 rules use it) and the game evaluates a set again for every rule
// that uses it. stormmemo remembers each set's answer for the character until the next rule action runs (actions
// change roles, contexts, attributes...), so the answers are the game's own. Hooks: the per-character pass
// FUN_1806b6a08 (scope), the rule walk FUN_180698e84 (the game's loop, replicated, marking each action) and the set's
// evaluate FUN_1806990f4. Only sets made of the 26 known read-only condition kinds are remembered.
// Check: the first 100,000 remembered answers, then every 64th, are evaluated again the game's way and must match;
// a mismatch keeps the game's answer and turns the memo off for the session.
// deagle_stormmemo.txt next to the plugin: 1 on (default), 0 off, 2 check every remembered answer (no time saved).
// stormmemo: lines in deagle_native.log: characters, time in Storm per thread, sets looked up, remembered, checked.
void Stormmemo_Install(uintptr_t base, void* self);
