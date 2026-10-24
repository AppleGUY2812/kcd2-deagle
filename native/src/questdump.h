#pragma once
#include <cstdint>
// Quest graph dump: the finished graph of each quest project as canonical text, so two quest builds can be compared
// exactly (tools/questdump_diff.py). A detour on the per-project build FUN_181e39280 dumps the project after the
// original returns (fully deserialized, not yet activated), plus the deserializer's map at +0x2d0
// (re_quest_dump_binary.md part 1).
// What is compared: rttr properties the deserializer can write (public, not read-only, not static, metadata 0 not
// false), walked in rttr order; containers in stored order; shared objects by path on a second visit (@path); weak ids
// as paths (~path); C_Node raw fields (+0x18, parent id, ports); port class, kind and links; the project's children
// vector. Not compared: other C++ state the build writes outside rttr. Never prints pointers or resolver ids.
// Two walks per project (paths first, then text). Every game read and call is guarded; failures print ?err.
void Questdump_Install(uintptr_t gameBase);   // hook created and enabled once; costs a flag check unless armed
void Questdump_Arm(const char* path);         // dumps every project built on this thread to path (appended)
void Questdump_Disarm();                      // stop dumping (logs the totals)
