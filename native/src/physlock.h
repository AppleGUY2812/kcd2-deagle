#pragma once
#include <cstdint>
// Main's CryPhysics lock waits in the level load (WHGame 1.5.6; docs/LOADTIME.md "The map build" item 6, MODLOG
// "CryPhysics locks"). In LoadEntities Main's DestroyPhysicalEntity (FUN_180846e10) queues its work when it sees the step
// lock (world+0x5fdc) held, else it takes the lock; the streaming callback thread's AddGeometry (FUN_18084b2d4 ->
// FUN_18084bf68) holds that lock and the shared caller slot (+0x5ff4) through each geometry's transform, back to back
// for ~1.3 s: Main spins 0.5-0.7 s per load (boots 133-140).
// Boot 140 tried a gate (another thread's AddGeometry waited while Main was in DestroyPhysicalEntity): no gain. Main's
// slow calls all began while an AddGeometry was running (366 of 1489, 684 ms of Main's 685): Main write-locks the step
// lock itself (FUN_18050b638) and waits out that AddGeometry (up to 8.8 ms each). It queues instead only when bit 0 of
// world+0x1a0 is set; this logs that value. The other thread is "Streaming AsyncCallback" in one stream callback,
// FUN_181500dac -> FUN_180845740, unpacking a physics file into ~58,600 AddGeometry calls (2.1 s, 5.0-7.0 s into LoadLevel).
// fastload_physlock.txt next to the plugin: 0 off (no hooks), 1 (default) the timing, only while CLevelSystem::LoadLevel
// runs (levelprof's hook; with levelprof off this does nothing). The totals go to the log as LoadLevel ends.
// 2 [window ms, default 2] [cap ms, default 20]: Main's quiet window. A non-Main AddGeometry does not start while Main is
// in DestroyPhysicalEntity or left it under <window> ms ago, waiting at most <cap> ms a call (game locks untouched; the
// worker's physq.md: the alternatives change CryPhysics' queue/slot rules mid-load).
void Physlock_Install(uintptr_t gameBase, void* selfModule);
