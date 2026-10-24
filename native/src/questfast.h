#pragma once
#include <cstdint>
// The quest graph build (ConceptLoadJob, ~14 s on one thread) made faster; target 5 s or less (the user, 2026-10-24).
// Step 1, exact by construction: rttr's type registry re-sorts two whole lists for every new type (std::stable_sort,
// 0.4-0.76 s per first load, re_quest_builder.md 3a). Both sorts are replaced by one sorted insert (questfast_sort.h);
// the first 64 calls of each also run the game's own sort on a copy and must match, or the original sort is used for
// the rest of the session.
// deagle_questfast.txt: 1 on (default), 0 off.
void Questfast_Install(uintptr_t gameBase, void* selfModule);
void Questfast_Report(const char* when);   // one log line with the counts and times so far
