#pragma once
#include <cstdint>
// Faster pieces of the save load's "apply save" step (level state 5, ~6 s on the main thread; RE report 2026-10-24,
// scratchpad re_stepG.md). Every fast path gives the game's own answer.
// 1. Map clouds vs fog of war (~0.86 s in boot 13). After the save's fog of war is read, every map cloud (2,601:
//    mapClouds.xml, trosecko 1137 + kutnohorsko 1464) asks C_FogOfWar<3,4> "is any revealed cell inside this 12-point
//    ellipse?" (vtable slot 0x183a88bf8 = FUN_180b59660). The game walks the whole fog grid for every cloud and runs
//    the point-in-polygon test (FUN_180b59d88) on every revealed 16 m cell; the cloud's bounding box is never used, so
//    ~99 % of the tests can't hit. The hook computes the cloud's box and answers "no" for every 64 m grid node
//    (FUN_180b59c54) that lies outside it padded by 1 m. Exact: a point outside a polygon's box has an even crossing
//    count (FUN_180b59d88 is the even-odd test), and a rectangle outside the box can't overlap the polygon.
// Mode in deagle_applyfast.txt next to the plugin: 0 off, 1 on, 2 on + every 64th skipped node also asked the game's
// way (a "MISMATCH" line if it ever says yes; default until boots confirm). Totals go to the log at gameplay start.
void Applyfast_Install(uintptr_t gameBase, void* selfModule);
void Applyfast_OnLoaded();   // Lua "loaded": log the totals since the last time
