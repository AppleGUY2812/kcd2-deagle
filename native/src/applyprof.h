#pragma once
#include <cstdint>
// The save load's apply step (level state 5, ~4.4 s on Main) timed by piece, log only (2026-10-24,
// re_apply_xgen_profiles.md): the CryEngine save data, SoulList::LoadGameSouls (Storm inside), the XGenAI linkables
// passes, and the concept / profile sync (the ConceptState chunks, the node set build, the state broadcasts 5 and 2,
// the NodeManager registration, GameProfileManager::SynchronizeProfiles). Each piece is timed only while the level
// state is 5, inclusive (a piece contains what it calls), Main and other threads apart. When the state leaves 5, one
// "applyprof: apply step N done ..." line in fastload.log gives every piece's ms and calls. fastload_applyprof.txt next
// to the plugin: 1 on (default), 0 off.
void Applyprof_Install(uintptr_t gameBase, void* self);   // after MH_Initialize
void Applyprof_SetMainThread(unsigned long tid);          // the main thread's id (fastload_main.cpp, each frame)
