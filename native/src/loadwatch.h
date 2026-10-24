#pragma once
#include <cstdint>
// Exact timing of the terrain step (load step 2; user 2026-10-23 "u work on step 2"). kcd.log is buffered, so its
// "Terrain: Waiting for N async loaders" / "DONE" lines arrive late. CTerrain::Load_T (FUN_18052c81c) waits with
// while (loaders.begin != loaders.end) FUN_18354ff84(&loaders, 0);   // drops the loaders whose +8 is 0 (done)
// The hook times that loop and logs how many loaders are left every 250 ms: a steady fall means the work is
// throughput-bound; a long tail means a few late loaders hold the step.
// Also the quest load (the quest-loader decompile): the ConceptLoadJob on one job worker, its per-project phases
// (2a node building, 2b CreatePorts / CreateEdges) and the main thread's wait for it, one "loadwatch: quest" line per
// call of 20 ms or more (timing only; every hook checks its first 10 bytes against WHGame.dll 1.5.6).
void Loadwatch_Install(uintptr_t gameBase);
// The quest job (piece 3b's wait): -1 unknown (the job-body hook is off), 0 not finished since its last submit, 1 done.
int Loadwatch_QuestJob();
// Cumulative time (ms) and top-level calls of each quest hook so far: [0] job submit, [1] job body, [2] project,
// [3] 2a node building, [4] 2b CreatePorts, [5] CreateEdges, [6] main thread waits. Returns how many were filled (7).
int Loadwatch_QuestTotals(double ms[7], long long calls[7]);
