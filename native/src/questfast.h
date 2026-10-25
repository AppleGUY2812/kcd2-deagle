#pragma once
#include <cstdint>
// The quest graph build (ConceptLoadJob, ~14 s on one thread) made faster; target 5 s or less (the user, 2026-10-24).
// Step 1, exact by construction: rttr's type registry re-sorts two whole lists for every new type (std::stable_sort,
// 0.4-0.76 s per first load, re_quest_builder.md 3a). Both sorts are replaced by one sorted insert (questfast_sort.h);
// the first 64 calls of each also run the game's own sort on a copy and must match, or the original sort is used for
// the rest of the session.
// Step 2, exact with a self-check: rttr metadata lookups (FUN_1804fbb98, 11 M calls per build, 0.8 s) cached per
// thread by (owner, E_PropertyMetadata key, default); the first 20,000 hits and every 1024th also ask the game.
// Step 3, exact with a self-check: the port-definition walks cached (questports.h).
// fastload_questfast.txt: 1 on (default), 0 off.
void Questfast_Install(uintptr_t gameBase, void* selfModule);
// From fastload.asi's DllMain when it runs before WHGame.dll's static initializers (the ASI loader's global.ini
// DontLoadFromDllMain=0, 2026-10-25): step 1's two sort hooks on at once, so the ~20k type registrations of the game's
// start-up code (2.5 s before the plugin's Init, boot 97: the two stable_sorts' moves among the hottest places) get the
// sorted insert too. Returns how many of the 2 are on; Questfast_Install keeps them. No logging here (DllMain).
int Questfast_EarlyInstall(uintptr_t gameBase, void* selfModule);
void Questfast_Report(const char* when);   // one log line with the counts and times so far
bool Questfast_SetOn(bool on);             // questbench's A/B: the sorted insert on or off (false = not installed)
