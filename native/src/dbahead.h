#pragma once
#include <cstdint>
// The SmartObjectAnimations table (1.2-1.3 s of Main at the level load's end) loaded in the game's own level-start
// database job instead (WHGame 1.5.6). C_ObjectDatabaseManager starts a job at level-load start (system event 0x1b) that
// loads database types 0xd and 0xe on a worker (FUN_181e3a140); at level-load end (0x1c) Main waits for that job, then
// loads types 0x10 and 0x11 itself. Type 0x11 is the table: the level pak's tables/ai/smartobjectanimations.xml (trosecko
// 38 MB, kutnohorsko 70 MB), parsed by rttr. Its one outside input is ProfileId -> game profile id (FUN_18074cc6c, the
// game profiles load at the main menu with the other tables). Here the job also loads type 0x11 after its own two, on
// the same worker, ~8 s before level-load end; at level-load end the game's call finds the table loaded (vt+0x30) and
// only rebuilds its index (vt+0x50, the same index again).
// Measured every load (any mode): the table's load (thread, ms, game profiles in the map, ProfileId conversions and a
// hash of them: the same save and level give the same hash whichever thread loads it), the two readers of the table's
// index (FUN_1805d0f48, FUN_180f8a1e4) counted while the job loads it / during the rest of the level load / after, and
// conversions on other threads while the game's profile-map flag is set.
// fastload_dbahead.txt: 1 on, 0 the game's order (default). Boot 58 (2026-10-24): 1 is WRONG as built: at level-load start
// the game-profile source is still empty (0 profiles vs 879 at level end), so all 29,007 ProfileIds became unknown (hash
// 49344503a2c40145 vs the game's 0e0db8ba0d0f0f20). Type 0x10 (loaded just before 0x11 at level end) likely fills it.
// Saved 0.98 s of LoadLevel; no readers of the table ran during the level load (0 / 0 / 379, 0 / 0 / 47).
void Dbahead_Install(uintptr_t gameBase, void* selfModule);
void Dbahead_OnLoaded();   // gameplay started: the counts
