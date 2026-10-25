#pragma once
// Crash records for the load plugin's test boots (boots 66 and 68 crashed with only the 16 ms sampler to go on).
// A vectored exception handler writes each crash-type exception (access violation, illegal or privileged instruction,
// divide by zero, stack overflow, in-page error, misalignment) to fastload_crash.txt next to the plugin: time, thread,
// code, the faulting address as module+offset, the address read / written / run, the registers, and the code addresses
// found on the stack (return-address candidates, as module+offset). It always returns "continue search", so the game's
// own handling (BugSplat) runs unchanged. It can't tell a crash from a fault someone handles (readiness.cpp's guarded
// reads), so each faulting address is written once per run, at most 24; the last block before a crash is the crash.
// One "--- start" line per launch separates the runs.
void Crashlog_Install(void* self);
