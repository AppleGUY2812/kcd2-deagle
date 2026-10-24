#pragma once
#include <cstdint>
// Inner profile of the quest graph build (re_quest_replay_parallel.md plan step 1: "measure the replay floor first").
// questbench N prof turns on 36 timing hooks on the builder's inner functions for its builds only (MinHook
// enable/disable around them), counts only the bench thread, and logs per build the self time (time not spent in another
// hooked function) and calls of each, plus the resolver Register / Unregister counts (other threads separately). The
// functions a replay must still call (ctor, attribute convert, OnAfterDeserialization, port add, connect, Register) are
// marked "kept", so one bench says whether a serial replay can reach 5 s.
void Questprof_Install(uintptr_t gameBase);   // checks every function's first 16 bytes; hooks stay disabled
bool Questprof_Begin();                       // on the bench thread before a build: enable the hooks, zero the counters
void Questprof_End(int build);                // after it: one or two "questprof:" lines
void Questprof_Stop();                        // after the bench: disable the hooks
