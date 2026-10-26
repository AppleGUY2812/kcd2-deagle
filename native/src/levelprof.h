#pragma once
#include <cstdint>
// The level load's main-thread work, timed and sampled (docs/LOADTIME.md "Plan: the map build"). fastload_levelprof.txt:
//   0 off, 1 timing hooks + a 30 s sample of Main from CLevelSystem::LoadLevel (questsample.cpp, full call
//   stacks at ~1 kHz, phase per sample -> fastload_lsample.csv), 2 timing hooks only (the default; no sampler, so timing boots stay comparable).
// Timing hooks (WHGame.dll 1.5.6): LoadLevel, LoadEntities, the navmesh tile build, the object databases by type (the
// SmartObjectAnimations table is type 0x11), XGenAI's LoadGameGlobalPass, the material resource dedupe (count + total).
void Levelprof_Install(uintptr_t gameBase, void* selfModule);
// f runs first in each call of the object-database hook (FUN_180ef3afc), before the timing; false when that hook is not
// on. For dbasync, whose own hook on that function can't be made while this one holds it (MinHook: one hook per address).
bool Levelprof_SetDbPreCall(void (*f)());
// f(type) runs next in each call of that hook (dbahead's gate: a type load waits for its worker); false when the hook is off.
bool Levelprof_SetDbTypeGate(void (*f)(uint32_t type));
// The game-profile list's size now (C_GameProfileManager, filled by the AI module's level-load step after LoadEntities);
// negative when it can't be read.
long long Levelprof_GameProfiles();
// The thread that runs CLevelSystem::LoadLevel (0 until it starts), whether it runs now, and f called on that thread as
// it ends (false when the LoadLevel hook is off). For physlock.
unsigned long Levelprof_MainTid();
bool Levelprof_InLoadLevel();
bool Levelprof_SetEndCall(void (*f)());
