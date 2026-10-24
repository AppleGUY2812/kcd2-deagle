#pragma once
#include <cstdint>
// What holds the loading screen after the save is applied (load step 5, docs/PERFORMANCE.md "Step 5"). Decoded
// 2026-10-23 (~/re/work/save_load_step5/report.md on the ProLiant): in level-manager state 6 the game registers a
// readiness observer with category mask 0x3f93; the service fires it (state 7, then StartGameplay) once every
// registered readiness task in those categories says ready, or after wh_sys_ReadinessObserverTimeoutDuration (50 s).
// The tasks are the streaming systems: TextureStreaming, TerrainStreaming, MergedMeshStreaming, AnimationStreaming,
// OctreeStreaming, ObjStreaming, EntityStreaming, HLodStreaming, Svoti*, MovedObservers, FmodBankLoading...
// (a C_ReadinessProgress task is ready when its in-flight counter is 0).
// This only reads. It logs, for the first 20 minutes after start:
//   "readiness: level state A -> B" on every change of the level manager's state;
//   "readiness: waiting N s: not ready ..." once a second while in state 6 (with in-flight counts where known);
//   "readiness: state 6 -> B after N s ..." with each task's not-ready time over the wait, longest first, and the
//   task that was ready last.
// It asks the tasks exactly the way the game's IsReady does (their virtual category and ready calls, under the
// service's lock, on the main thread). An in-game reload of a save (same level) uses its own observer and is not
// covered.
void Readiness_Install(uintptr_t gameBase);
void Readiness_Frame();   // main thread, once a drawn frame (main.cpp OnCamera)
