#pragma once
#include <cstdint>
// Quest build step 3 (re_quest_portbind.md, 2026-10-24): the port-definition walks cached.
// Adding a port to a quest node binds its definition by walking every port definition of the node's class
// (node vt+0xe0, a std::function visitor, no early exit, the last match wins), and a lazily created port walks twice
// (lookup, then bind): ~3 s of the build. The static part of a walk (the rttr-property port definitions, one shared object
// per property in the game's global map) depends only on (enumerator, rttr type, port name), so its final match is
// cached and handed to the visitor once. Per-instance definitions (Function parameters, State values, OnExec...) still
// run the game's code; AutoTriggerable nodes' "Set..." path runs the game's code for names that could match it.
// Only on the quest job's thread (loadwatch marks it). The first 4096 hits, the first 2 of each key and every 64th after
// also run the game's walk and must give the same definition, or the cache is off for the session.
// Switched with questfast (deagle_questfast.txt) and questbench's ab.
void Questports_Install(uintptr_t gameBase);
void Questports_JobScope(bool enter);    // loadwatch, around the ConceptLoadJob body (any thread)
void Questports_Report(const char* when);
bool Questports_SetOn(bool on);          // false = not installed
