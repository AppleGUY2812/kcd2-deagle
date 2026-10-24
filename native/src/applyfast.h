#pragma once
#include <cstdint>
// Faster pieces of the save load: the "apply save" step (level state 5, ~6 s on the main thread; RE report 2026-10-24,
// scratchpad re_stepG.md). Every fast path gives the game's own answer.
// 1. Map clouds vs fog of war (~0.86 s in boot 13). After the save's fog of war is read, every map cloud (2,601:
//    mapClouds.xml, trosecko 1137 + kutnohorsko 1464) asks C_FogOfWar<3,4> "is any revealed cell inside this 12-point
//    ellipse?" (vtable slot 0x183a88bf8 = FUN_180b59660). The game walks the whole fog grid for every cloud and runs
//    the point-in-polygon test (FUN_180b59d88) on every revealed 16 m cell; the cloud's bounding box is never used, so
//    ~99 % of the tests can't hit. The hook computes the cloud's box and answers "no" for every 64 m grid node
//    (FUN_180b59c54) that lies outside it padded by 1 m. Exact: a point outside a polygon's box has an even crossing
//    count (FUN_180b59d88 is the even-odd test), and a rectangle outside the box can't overlap the polygon.
// 2. The facial DBA list (objects step, ~0.66 s in boot 13): the chrparams loader inserts 19,800 facial .dba paths
//    one by one into a sorted array, in the file finder's order, so every insert shifted the tail (O(n^2)). The hook
//    sorts the finder's result by the array's own key first, so every insert lands at the end. Same set either way.
// 3. The PSO precache (level state 4, ~2.4 s the main thread waits): the game's own call, made once at the main menu
//    (no XML load for 1.5 s and no level load yet), on the main thread from the render sync (FUN_1809a6190, every frame; ISystem::Render
//    does not run at the menu).
//    The list comes from the pipeline cache file, so the load's own precache then finds the PSOs made. The menu
//    freezes while it runs: at the menu it also parses the 416 shaders the load would parse later (6.2 s, boots 22-23;
//    state 4 2.4 -> 0.3-0.6 s, ~1.9 s off the load on average). deagle_psoahead.txt 1.
//    3b (deagle_psoahead.txt 2, default): the same call during the load instead, with no menu freeze. Just before the
//    main thread waits for the quest job, it waits until the render loading thread has finished its texture precache
//    (it then sits idle until "finish level" is done), queues the precache and hands it over without blocking, so it
//    runs while the main thread finishes the level. 0 = the game's own precache in state 4.
// 4. Quests first in the level data message (~2-3 s of the main thread's wait for the quest job): the concept module's
//    handler of module message 0x1e (it reads the level's ConceptManager block and submits the ConceptLoadJob) runs
//    first in the module loop instead of after the character and AI-tree loaders, so the job starts ~3 s earlier.
//    Same message, same thread, same code; its own turn is skipped. deagle_questearly.txt: 0 off, 1 on (default).
// Mode in deagle_applyfast.txt next to the plugin: 0 off, 1 on, 2 on + every 64th skipped node also asked the game's
// way (a "MISMATCH" line if it ever says yes; default until boots confirm). Totals go to the log at gameplay start.
void Applyfast_Install(uintptr_t gameBase, void* selfModule);
void Applyfast_OnLoaded();
void Applyfast_BeforeQuestWait();   // main thread, just before it waits for the quest job (loadwatch.cpp): piece 3b   // Lua "loaded": log the totals since the last time
