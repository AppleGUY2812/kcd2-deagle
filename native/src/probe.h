#pragma once
#include <cstdint>
// Probes for docs/ARCHITECTURE.md migration step 1 (Claude 5): each one checks a static finding of docs/whgame_re/ in
// the running game, on its console command only ("[DN] probe <name> ...", snippets in tools/probe/). No per-frame
// work, no hooks; every engine call under SEH; what a probe creates it removes on its own command.
void Probe_Install(uintptr_t env, uintptr_t gameBase);
void Probe_Command(const char* rest);
