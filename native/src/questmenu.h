#pragma once
#include <cstdint>
// The quest graph (ConceptLoadJob, ~14 s on one thread) built at the main menu and kept by the first save load
// (re_questmenu_keep.md, re_questmenu_when.md, 2026-10-24; user: "start everything as early as possible").
// - Record: every real load's job body (loadwatch's hook of FUN_181e2b650) writes its inputs (M = level modules, P =
//   project files incl. mod graphs), the concept cvars, the quest manager's delegate counts and WHGame.dll's stamp to
//   deagle_questcache.txt. No cache = no pre-build.
// - Start: after boot event 0x24 (FUN_180fc4de8: save list read, ~2.6 s before "main menu idle"), on Main, once per
//   game start, if the newest save (what Continue loads) is on the cached level, the manager is empty, nothing has
//   changed, the delegate counts match (the quest log is subscribed, so activation at the menu reaches it) and RAM
//   allows: a plugin thread runs the game's own job body with the cached inputs. xmlahead's menu workers start at once
//   to feed it parsed XML.
// - Keep: the load's own job body (same hook) waits for the pre-build, compares P, M, manager, cvars and the project
//   list, and on a match returns without rebuilding (mgr+0x378 = 0, as the body would leave it). Otherwise it runs the
//   game's body, whose reset unloads the pre-build the way it unloads any old graph.
// - Concept messages that touch the projects wait for the build (while it runs) or disarm the keep (after it).
// deagle_questmenu.txt: 1 on (default), 0 off, 2 no pre-build (test: the load builds its own, replayed when recorded).
void Questmenu_Install(uintptr_t gameBase, void* selfModule);
bool Questmenu_OnJobBody(uint64_t impl);   // loadwatch, job-body detour, depth 0: true = kept (skip the body)
// The load builds its own graph (a load during play, or the menu build not kept): phase 2a replayed from
// deagle_questreplay.bin and the XML phase skipped when the record was made for these inputs; never recorded in a load.
// Begin: 2 = armed (loadwatch, job-body detour, depth 0, before the body). End: true = the replay failed, the record is
// dropped and the caller runs the game's body again (its reset unloads the failed graph, as between questbench builds).
int Questmenu_JobReplayBegin(uint64_t impl);
bool Questmenu_JobReplayEnd(int mode);
void Questmenu_OnConceptMsg(int type);    // applyfast's concept OnMessage hook (main thread)
void Questmenu_SetConceptHook(bool on);   // applyfast: that hook is installed
// questbench N [ab]: at the main menu, rebuild the quest graph N times (1-20) on a plugin thread with the cached inputs,
// one "questbench:" line per build with the time of each part (the profile/test loop without a boot). ab = questfast on
// for odd builds, off for even ones. A pending main-menu build stays kept (re-snapshotted). No args: the state.
void Questmenu_Bench(const char* args);
