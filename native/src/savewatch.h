#pragma once
#include <cstdint>
// Save writers during the load (fastload_savewatch.txt: 1 on, 0 off = the default). The load's last step (state 7 of
// the level switch, docs/LOADTIME.md "settle") polls "is any save writer alive" (FUN_1817ea370 -> CPlatformOS_PC
// vt+0x90, the count DAT_185570258); this logs each writer (vt+0x88 FUN_182440d40: its file name, thread, caller),
// when it dies, and how long the poll said busy. Logging only: nothing in the game changes.
void Savewatch_Install(uintptr_t gameBase, void* selfModule);
