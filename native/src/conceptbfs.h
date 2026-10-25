#pragma once
#include <cstdint>
// The concept graph's find-by-name (FUN_181e32040), run for every C_LinkableObject registration in the save load's
// apply step (0.35 s on boot 13) and before it (0.22 s): the game's breadth-first walk keeps its queue in a std::deque
// of counted pointers (4 AddRef + 4 Release per node, a heap block per 2 pushes). Replaced by conceptbfs_core.h: the
// same visit order on a plain vector, one AddRef + one Release per node. The first 200 finds also run the game's and
// must return the same node (a mismatch turns it off for the session), and every 64th after.
// fastload_conceptbfs.txt next to the plugin: 1 on (default), 0 off (2, a deleted remembered walk, reads as 1). conceptbfs: lines in fastload.log give finds, nodes looked at and time.
void Conceptbfs_Install(uintptr_t base, void* self);
