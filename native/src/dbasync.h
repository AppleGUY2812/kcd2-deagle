#pragma once
#include <cstdint>
// The dialogue dubbing database loaded on a worker (launch -> menu work, 2026-10-25). At start-up the dialogue module's
// init (FUN_180e2f1e0) loads database type 0xf on Main: one table, response_depot_wave_file
// (C_ObjectDatabase<S_DialogueDubbing>, the only XML in Localization\english-part3.pak, 63k rows), 1.08-1.13 s in
// every boot 96-102, its XML parse ~0.43 s and the rest the rttr row building. No reader before the first frame is
// known (not proven: the gates below cover the database's entry points, not every reader of the table).
// The game itself loads types 0xd / 0xe (Scheduler etc., the same kind of rttr object database) as an
// ObjectDatabaseJob on a worker at the level-load start and waits for it at LevelLoadEnd (FUN_180ef37d8). This does
// the same for 0xf: the table's registration stays on Main (FUN_180ef3afc), and its load, FUN_180ef3e9c(mgr, 0xf, 0),
// runs on our thread while Main goes on; FUN_180ef3e9c returns 1 to Main at once. Main waits for that thread before
// any other database load (FUN_180ef3afc, FUN_180ef3e9c, FUN_180ef5668, and FUN_180ef4048 through dbprof's hook) and at
// its first frame tick, so nothing after the menu can see a half-built table. A failed load shows the game's own
// "Database system error" box at that wait, as Main would have.
// fastload_dbasync.txt: 1 = on (default), 0 = off (the load on Main as before).
void Dbasync_Install(uintptr_t gameBase, void* self, unsigned long mainTid, bool listGateOk);
void Dbasync_Gate(const char* where);   // Main: waits for the worker's load if it is still running (no-op otherwise)
void Dbasync_Frame();                   // Main, each frame tick: the first-frame gate and the report line
