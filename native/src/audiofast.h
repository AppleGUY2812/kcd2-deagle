#pragma once
#include <cstdint>
// The sound system's start-up requests without Main waiting for them (launch -> menu work, 2026-10-25).
// The sound system's init (FUN_181174234, ~4 s after the plugin) makes the audio system (CAudioSystem, vtable
// 0x183d281e0), starts its AudioThread and pushes four blocking requests (PushRequest, slot 3, flags 6 = high priority +
// ExecuteBlocking): the FMOD implementation (FUN_181810b10's push), then the global controls, the global preloads and
// the global preload request (FUN_181174530's three). Each push waits on the audio system's event (+0x5e8) until the
// audio thread has run it: boot 18:45, 28 Main samples (0.59 s) at 3.95-4.53 s, with Main never waiting on audio
// again before the menu.
// Here those four pushes return at once: the requests are copied into the queue (paths in their own 320-byte buffers)
// and the audio thread runs them in order (high-priority queue first, one at a time, FUN_180500b90), before any later
// request. The audio thread signals the event once per finished blocking request (FUN_180500de8): those four signals
// are counted and swallowed (the game's SetEvent import). Until they are all done, everything that reads the audio
// tables on the calling thread waits (vtable slots 6-14, 16, 22-24: the id lookups, listeners, impl info,
// ExternalUpdate), and so do other blocking pushes (they share the event; later normal pushes queue behind the four);
// the first frame waits too, then the slots and the import go back. "audiofast:" lines: when each request finished and
// every wait. Mode 2: also CryMovie's 16 TrackView parameter-id lookups (the one wait left in boots 158-159, 418-526 ms
// on Main) don't wait: they get the id at once (CRC32 of the lower-case name, what the lookup gives a known name), and
// the game's own lookup runs for them at the first frame and writes the real answer.
// fastload_audiofast.txt next to the plugin: 1 on, 2 on + the TrackView ids, 0 off (default until a boot pair tests it).
void Audiofast_Install(uintptr_t gameBase, void* selfModule);   // Init, before the sound system's init
void Audiofast_Frame();   // the frame tick: waits for the requests if needed, puts everything back, logs once
