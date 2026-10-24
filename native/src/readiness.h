#pragma once
#include <cstdint>
// What holds a loading or black screen while the world streams in: after a save is applied (load step 5,
// docs/PERFORMANCE.md "Step 5") and, the same way, other waits such as the fast-travel arrival. Decoded 2026-10-23
// (~/re/work/save_load_step5/report.md on the ProLiant): the game starts a readiness observer with a category mask
// (FUN_180fc3ec0: 0x3f93 after a save load, others elsewhere); the readiness service fires it once every registered
// task in those categories says ready, or after wh_sys_ReadinessObserverTimeoutDuration (50 s). The tasks are the
// streaming systems: TextureStreaming, TerrainStreaming, MergedMeshStreaming, AnimationStreaming, OctreeStreaming,
// MovedObservers, HLodStreaming, EntityStreaming, Svoti* and 6 of other classes (a C_ReadinessProgress task is ready
// when its requests in flight are 0).
// A hook on that start (it only notes the mask and the call site, then runs the game's own) and, each frame while one
// waits, the same questions the game's IsReady asks (the tasks' virtual category and ready calls, under the
// service's lock, on the main thread). Log, all session:
//   "readiness: level state A -> B" on every change of the level manager's state;
//   "readiness: gate 0xMASK (from +RVA) waiting N s: not ready ..." once a second while a gate waits;
//   "readiness: gate 0xMASK (from +RVA) open after N s; not ready for: ...; ready last: X" when it opens
//   (gates that open within 0.25 s are left out).
void Readiness_Install(uintptr_t gameBase);   // after MH_Initialize
void Readiness_Frame();   // main thread, once a drawn frame (main.cpp OnCamera)
