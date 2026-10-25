#pragma once
#include <cstdint>
// The concept graph's find-by-name (FUN_181e32040), run for every C_LinkableObject registration in the save load's
// apply step (0.35 s on boot 13) and before it (0.22 s): the game's breadth-first walk keeps its queue in a std::deque
// of counted pointers (4 AddRef + 4 Release per node, a heap block per 2 pushes). Replaced by conceptbfs_core.h: the
// same visit order on a plain vector, one AddRef + one Release per node. The first 200 finds also run the game's and
// must return the same node (a mismatch turns it off for the session), and every 64th after. Mode 2 (opt-in, for a test
// boot: boot 66 crashed reading a non-name field, fixed; it was slower than mode 1 in the first 200 finds; boot 61: 1,204 finds, ~2,070 nodes each, 406 of them find nothing and walk everything, ~0.25 s): the
// walk is remembered per root (cbfs::Snap) and each find only checks that the part of the graph the game would walk
// is unchanged (children, vtables, Named(), names), else walks again. fastload_conceptbfs.txt next to the plugin:
// 2 (test), 1 the walk without the deque only (default), 0 off; mode 2 turns itself back to 1 when more than half
// of its first 200 finds had to walk again. conceptbfs: lines in fastload.log give finds, nodes looked
// at, time, and the remembered walks (roots, walks built, rebuilt after a change).
void Conceptbfs_Install(uintptr_t base, void* self);
