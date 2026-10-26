#pragma once
#include <cstdint>
// The shader-quality settings sent to the render thread without waiting for it (launch -> menu work, 2026-10-25).
// While the game applies the profile's settings (FUN_180c9da44 game init, defaultProfile.xml), each shader-quality
// setting goes through the renderer's SetShaderQuality (FUN_1824f2370): it queues a command for the render thread
// (FUN_181de6270, the values copied into the command, nothing returned) and then flushes and waits (its tail jump to
// FUN_1807a2138). The render thread is then busy parsing shader files (FUN_180883200) and Main waits for all of it:
// boot 124, 27 samples (0.54 s) at 6.6-9.8 s after the plugin. The command does only set the quality byte and the
// shaders' "changed" flags (FUN_1824fa9fc).
// Here that one flush is skipped until the first frame: the command stays queued and runs at the next flush, in its
// order, before any later command; Main goes on with the game init meanwhile. After the first frame the game's own
// behaviour (flush and wait) is back.
// Also (mode 1), the shader manager's init (FUN_180da002c, at ~3 s): it loads the 36 system shaders on Main
// (FUN_180a5c398), flushes and waits for the render thread (0.7 s of Main in the 18:45 boot, 2026-10-25), then closes
// %ENGINE%/Shaders.pak and shadercache.pak, which the render thread reads while it works. Here that wait is skipped
// and the two closes are held (ICryPak::ClosePack, vtable +0x88, only those two call sites on Main) until Main's next
// real flush has returned, when everything queued before it is done; with no such flush, at the first frame after a
// wait. Main meanwhile does the audio and input init (no shader reads). The log line at the first frame gives Main's
// total real-flush wait and the first one after the skip, to see whether the wait only moved.
// "rtsync:" lines. fastload_rtsync.txt: 2 the shader-quality settings only (default), 1 both (not run in game yet), 0 off.
void Rtsync_Install(uintptr_t gameBase, void* selfModule);   // Init, before the renderer starts
void Rtsync_Frame();   // the frame tick: off from the first frame, its counts logged once
