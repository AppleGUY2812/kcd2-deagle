#pragma once
#include <cstdint>
// The native round simulation's plugin side (docs/ARCHITECTURE.md step 6, Claude 5): the DN.* Lua binding over
// native/sim/sim.cpp, with KCD2's world (rays, water, living NPCs, the game clock) behind the sim's callbacks.
// Rounds_Install only notes gEnv (no hooks, no engine calls); nothing runs until Lua sends "[DN] rounds bind" (then
// only on DN.* calls, all on the Lua thread). "[DN] rounds stats" logs what the world calls have cost.
void Rounds_Install(uintptr_t env, uintptr_t gameBase);
void Rounds_Command(const char* rest);
// the global Lua table DN (created once on the first bind; probe.cpp adds its functions to it too). 0 on failure
uintptr_t Dn_Table();
