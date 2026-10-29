#pragma once
#include <cstdint>
// KCD2's save load onto the level already loaded (WHGame 1.5.6). SaveGameManager FUN_1825bdc7c(mgr, user, save) compares
// the save's level with the loaded one: same level (and the level manager's vt+0xa0 says no) -> FUN_1825bda54: the save
// read into the loaded world, "SaveGameManager::PostLoadGame", readiness gate 0x3f93; no `map`, no LoadLevel. Otherwise
// the level manager's load (vt+0x50: states 1-9, `map <level>`, LoadLevel; state 5 calls it again, now the same level).
// Logged on every load: each call (user, save, caller, which path, ms). `samelevel load` replays the last call on the next
// level-manager update while in game (the context the game's own state 5 uses): a test of what Continue onto a level that
// is already loaded would cost (the 10 s plan's hand-off, docs/LOADTIME.md).
void Samelevel_Install(uintptr_t gameBase);
unsigned Samelevel_LoadSeq();               // save loads seen so far (any thread; ++ at every load, full or same-level)
void Samelevel_Command(const char* args);   // "samelevel [load]"
