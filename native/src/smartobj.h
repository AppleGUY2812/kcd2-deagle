#pragma once
#include <cstdint>
// The AI smart objects' animation lookups spread over cores (launch -> menu work, 2026-10-25). At CGame init the module
// manager's message 0 (FUN_1807e30c4) reaches C_SmartObjectsManager, which for every smart-object type (benches, beds,
// ...: the map at +0x10 of FUN_181096ec8's object) calls FUN_181096fdc: for each of the type's actions x each actor class
// (S_ActorClassData, the table at 0x185324230) it asks C_FragmentDatabase which animation fragments fit (tag-bitmask
// filtering FUN_181e5bb80, option scoring FUN_1809440c8 with an allocation and a sort per query) and adds their stance
// ids to the type's own list (+0x78). Main, one after another: 0.50-0.56 s CPU in boots 163a1-164z1 (exact stacks).
// Each type writes only its own list; the rest is lookups (and the game's rand() for random options; its seed comes
// from the clock). Mode 1: the first type runs on Main alone (so anything built on first use is built), then the rest
// on worker threads and Main, Main waiting for all, with the Mannequin database manager's Load (the one shared write:
// it loads and caches a database the first time) under a lock. Only the first call (start-up); later calls run as the
// game's.
// fastload_smartobj.txt next to the plugin: 1 spread, 2 the game's way timed per type (the base for a pair), 0 off
// (default); a second number = threads (default: cores - 1, at most 8). "smartobj:" lines in the log.
void Smartobj_Install(uintptr_t gameBase, void* selfModule);
