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
// "Let me in early" (readiness_early.h; off by default, `readiness early 1`, kept in fastload_readiness.txt): the save-load
// and fast-travel gates open once terrain, octree, animations, entities and objects are ready, while textures, merged
// meshes, far LODs and voxel GI still load (through the game's own timeout path). Off, the open line says how much
// sooner it would have let you in.
//   "readiness: gate ... let in early after N s; still loading: ..."
// While a gate holds the screen (0.25 s or more), and 1.5 s after, every main-thread file read is logged (mainreads.h).
void Readiness_Install(uintptr_t gameBase);   // after MH_Initialize
// "readiness temp <MB>" (32-512; 0 = the game's own, the default; kept as the file's second number): while a save-load
// or fast-travel gate waits, the stream engine's temp memory budget (sys_streaming_memory_budget, 20 MB in Warhorse's
// system.cfg) is raised to that, then put back. The "waiting" lines show the reader: asleep with work queued (temp
// memory or a pause holds it), asleep with nothing queued, temp memory full.
// "readiness spawn <N>" (4-64; 0 = the game's own, the default; kept as the file's fourth number): while a save-load or
// fast-travel gate waits, es_PerInstanceStreamingClassSpawnBudget (the entities spawned a frame per streamable class: 2,
// 4 while a fader is up) is raised to that, and so is the constant 4 the game uses while a fader (the loading screen)
// is up; both put back after. With morphpool on, the settle gate waits on EntityStreaming (lights and particles).
// "readiness hold <s>" (0.05-1, the default 0.3; kept as the file's third number): how long the must-have tasks stay
// ready before "let me in early" opens the gate.
// The AI module's frame job (one "XGenUpdateJob" a frame, which Main waits for at the end of C_XGenAIModule's Update) is
// timed on the main thread: each gate's "open" line ends with "N AI frame jobs, Main waited W ms (X % of the gate)...",
// and 10 s after a save-load gate opens "readiness: AI update in the first 10 s of play after the gate: ..." gives the
// same numbers in play. Plain C_ReadinessTask tasks are named by their name string and their function's class.
void Readiness_Command(const char* args);     // "readiness [early 0|1] [hold 0.05-1] [temp 0|32-512] [spawn 0|4-64]"
void Readiness_Frame();   // main thread, once a drawn frame (fastload_main.cpp)
int Readiness_LevelState();   // the level manager's state (0 in game, 1-9 while a level loads), -1 = none yet; any thread
void Readiness_WatchState();   // logs "readiness: level state A -> B"; fastload's watcher thread, every 10 ms
