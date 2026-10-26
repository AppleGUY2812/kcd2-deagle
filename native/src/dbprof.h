#pragma once
#include <cstdint>
// The object database's tables timed one by one (launch -> menu work, 2026-10-24). At start-up the game init loads
// database types 0x1, 0x4, 0x8, 0x3, 0x6, 0x5, 0x2 on Main, 1.58 s (boot 95; type 0x4 alone 0.72 s), and the level
// load loads more (0xd / 0xe in its job, 0x10 / 0x11 on Main). Per type, C_ObjectDatabaseManager (FUN_180ef3afc ->
// FUN_180ef3e9c -> FUN_180ef4048) sorts that type's tables by dependency and, per table, calls its load (vt+0x48) and
// its link (vt+0x50), both with the table only. This hooks FUN_180ef4048 and, before it runs, points the vt+0x48 /
// vt+0x50 slots of every table class in the list at timing wrappers (a vtable write, no thread freeze). One line per
// call: "dbprof: type 0xT on thread N: K tables, X ms (load A, link B); slowest: <name> ms (load / link), ...".
// Its list-load hook is also dbasync's gate there (true when it is on).
// dbpool (fastload_dbpool.txt next to the plugin: 0 the game's order, the default; 1 pooled), 2026-10-26: type 0x4's
// three big tables, CharacterComponent, Item and InventoryPreset (0.42 s of Main's 0.51 s of type 0x4 loads, boots
// 181A1-4; links 0.22 s more), are loaded on their own threads from the game init's first list load (type 0x1), while
// they are still pending (FUN_180ef3afc(4) activates them ~0.25 s later). Their load is the table's own: the XML file
// through the game's loader (FUN_1806e3edc) and the reflection fill (FUN_181e25520, which morphpool already runs on
// 4 threads) into the table's own rows. The game's order stays: activation, FUN_180ef44a4, the +0xa1 flag and every
// link run on Main as before; when the driver reaches one of them, its +0x48 (HkLoad) waits for that thread and returns
// its result, and its +0x30 says "not loaded" until then (the thread clears byte +0x60 after its load, HkLoad sets it).
// Basis for running them early: their loads touch only the table (objdb.md section 3.3; InventoryPreset's +0x88 index
// is built from its own rows). "dbpool:" lines: per table the thread, its time, how long the game waited, and in both
// modes its row count and key hash (compare across boots).
bool Dbprof_Install(uintptr_t gameBase, void* selfModule);
