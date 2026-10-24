#pragma once
#include <cstdint>
// Logs every file the main thread reads for a short window (asked by the step-5 session, 2026-10-23: name the file
// behind each slow readiness gate: fast travel, save load, sleep; readiness.cpp calls it when a gate of 0.25 s or more
// opens). Works at any time, not only in pakahead's / xmlahead's first 10 minutes.
// MainReads_Log(ms) opens a window on the calling thread (call it on the main thread); a call while one is open
// extends it. xmlahead reports XML loads (path, ms) and pakahead pak entry reads (pak, data offset, sizes, ms). When the
// window ends, a "mainreads:" block goes to deagle_native.log: the totals, the 15 slowest, then every read in time order
// (at most 200 lines). Pak reads made inside an XML load are listed under it, so their time is counted in both.
void MainReads_Log(int ms);
bool MainReads_On();   // cheap: is a window open on this thread?
// kind 'x' XML load, 'X' XML handed over ready-made, 'p' pak entry read, 'P' pak entry handed over ready-made.
// `off` = the entry's data offset in the pak (pak reads), `bytes` = unpacked size, `ticks` = QueryPerformanceCounter
// time it took, `startTicks` = QueryPerformanceCounter when it started.
void MainReads_Add(char kind, const char* what, uint32_t off, uint64_t bytes, long long startTicks, long long ticks);
