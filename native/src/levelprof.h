#pragma once
#include <cstdint>
// The level load's main-thread work, timed and sampled (docs/LOADTIME.md "Plan: the map build"). fastload_levelprof.txt:
//   0 off, 1 timing hooks + a 30 s sample of Main from CLevelSystem::LoadLevel (questsample.cpp, full call
//   stacks at ~1 kHz, phase per sample -> fastload_lsample.csv), 2 timing hooks only (the default; no sampler, so timing boots stay comparable).
// Timing hooks (WHGame.dll 1.5.6): LoadLevel, LoadEntities, the navmesh tile build, the object databases by type (the
// SmartObjectAnimations table is type 0x11), XGenAI's LoadGameGlobalPass, the material resource dedupe (count + total).
void Levelprof_Install(uintptr_t gameBase, void* selfModule);
