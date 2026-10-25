#pragma once
#include <cstdint>
// The object database's tables timed one by one (launch -> menu work, 2026-10-24). At start-up the game init loads
// database types 0x1, 0x4, 0x8, 0x3, 0x6, 0x5, 0x2 on Main, 1.58 s (boot 95; type 0x4 alone 0.72 s), and the level
// load loads more (0xd / 0xe in its job, 0x10 / 0x11 on Main). Per type, C_ObjectDatabaseManager (FUN_180ef3afc ->
// FUN_180ef3e9c -> FUN_180ef4048) sorts that type's tables by dependency and, per table, calls its load (vt+0x48) and
// its link (vt+0x50), both with the table only. This hooks FUN_180ef4048 and, before it runs, points the vt+0x48 /
// vt+0x50 slots of every table class in the list at timing wrappers (a vtable write, no thread freeze). One line per
// call: "dbprof: type 0xT on thread N: K tables, X ms (load A, link B); slowest: <name> ms (load / link), ...".
// Log only; no setting. Its list-load hook is also dbasync's gate there (true when it is on).
bool Dbprof_Install(uintptr_t gameBase);
