#pragma once
#include <cstdint>
// Bigger streaming reads for step 5 of the boot load (after the save is applied: ~15 s of loading-screen frames while
// the area around Henry streams in; docs/PERFORMANCE.md "Step 5").
// The streaming engine reads every file through ZipDir::Cache::ReadFileStreaming (FUN_180464928) on one thread
// ("Streaming File IO HDD"): with sys_UncachedStreamReads 1 (the game's default) a synchronous unbuffered ReadFile
// in 128 KB pieces into a 128 KB buffer, one piece at a time, under the pak's lock. Measured on this PC's E: drive
// (SATA 870 EVO, 2026-10-23, unbuffered, scattered offsets): 128 KB reads 213 MB/s, 1 MB reads 437 MB/s; more reads
// in flight gained almost nothing (16 at once = 1 at once).
// This hook reads each request in ONE positional ReadFile (same handle, same lock) into its own aligned buffer.
// It sees every stream-engine read, so the terrain step's async mesh loaders too, not only step 5.
// Mode, read at start from fastload_streamio.txt next to the plugin, set live by "[DN] streamio N":
//   0 the game's reads, counted only (default until mode 1 is proven in game); 1 one read per request;
//   2 verify: both, the game gets its own bytes, every mismatch is logged.
//   Mode 1 checks itself first: its first 300 requests run as verify, and one mismatch drops it to 0 for the session.
// Every 2 s with activity the log gets one "streamio:" line: requests, MB, MB/s, how busy the read threads were (time
// inside the hook over wall time, summed over the threads that call it), time waiting for the pak lock, request
// sizes; every 30 s the paks read the most. A "streamio locks:" line (every 2 s with reads, first 15 minutes) says how
// often a pak IO lock was held, by which thread, and how often a thread was queued behind it: the lock is held across
// the whole 128 KB read loop, and the main thread's own pak reads take the same lock.
void Streamio_Install(uintptr_t gameBase, void* selfModule);   // before the level load
void Streamio_Command(const char* args);
