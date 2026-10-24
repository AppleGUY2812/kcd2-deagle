#pragma once
#include <cstdint>
// Storm (wh::rpgmodule::storm) applies its 6,444 rules to every character when a save loads (the apply step, ~1 s on
// Main on boot 40, and again on a job worker at level load). Rules share 298 named condition sets
// (C_CustomSelector, e.g. isValidOpenworldNpc, 100 rules use it) and the game evaluates a set again for every rule
// that uses it. stormmemo remembers each set's answer for the character until a rule action changes something the set
// reads (2026-10-24: per state group, re_storm_invalidate.md: roles, contexts, schedule, perks, attributes, combat
// level, name; was: any action made every answer stale, 27 % reused), so the answers are the game's own. Actions the RE
// didn't follow all the way (PerkRemove, SetReputation, SetEquipDirectives, SetReincarnationParams, unknown classes)
// still make everything stale; attribute actions make every state group stale. Hooks: the per-character pass
// FUN_1806b6a08 (scope), the rule walk FUN_180698e84 (the game's loop, replicated, marking each action) and the set's
// evaluate FUN_1806990f4. Only sets made of the 27 known read-only condition kinds are remembered.
// Check: the first 100,000 remembered answers, then every 64th, are evaluated again the game's way and must match;
// a mismatch keeps the game's answer and turns the memo off for the session.
// fastload_stormmemo.txt next to the plugin: 1 on (default), 0 off, 2 check every remembered answer (no time saved).
// stormmemo: lines in fastload.log: characters, time in Storm per thread, sets looked up, remembered, checked.
void Stormmemo_Install(uintptr_t base, void* self);
