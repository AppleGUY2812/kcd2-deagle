#pragma once
#include <cstdint>
// The render thread's start-up work without Main waiting for it (launch -> menu work, 2026-10-25).
// The shader-quality settings:
// While the game applies the profile's settings (FUN_180c9da44 game init, defaultProfile.xml), each shader-quality
// setting goes through the renderer's SetShaderQuality (FUN_1824f2370): it queues a command for the render thread
// (FUN_181de6270, the values copied into the command, nothing returned) and then flushes and waits (its tail jump to
// FUN_1807a2138). The render thread is then busy parsing shader files (FUN_180883200) and Main waits for all of it:
// boot 124, 27 samples (0.54 s) at 6.6-9.8 s after the plugin. The command does only set the quality byte and the
// shaders' "changed" flags (FUN_1824fa9fc).
// Here that one flush is skipped until the first frame: the command stays queued and runs at the next flush, in its
// order, before any later command; Main goes on with the game init meanwhile. After the first frame the game's own
// behaviour (flush and wait) is back.
// The render thread's commands are double-buffered: Main fills one buffer, and FUN_1809a6190(rt, 0) waits until the
// render thread has finished its last batch, swaps the buffers and wakes it. The flush (FUN_1807a2138) is two of those
// calls, the second one waiting for the batch. A skipped flush leaves its commands for Main's next swap, which then
// waits for them; a hand-over below is one call, so the render thread starts at once and Main goes on.
// Mode 1: the shader manager's init (FUN_180da002c, at ~3 s): it loads the 36 system shaders on Main (FUN_180a5c398),
// flushes and waits for the render thread (0.7 s of Main in the 18:45 boot, 2026-10-25), then closes
// %ENGINE%/Shaders.pak and shadercache.pak, which the render thread reads while it works. Here that work is handed over
// and the two closes are held (ICryPak::ClosePack, vtable +0x88, only those two call sites on Main) until Main's next
// real flush has returned, when everything queued before it is done; with no such flush, at the first frame after a
// wait. Main meanwhile does the audio and input init (no shader reads). (c23fac7 skipped this flush instead: the wait
// only moved to Main's next flush.)
// Mode 3 = mode 1 and the renderer's system-resource setup (InitSystemResources, renderer vtable slot 0x1a0, ~5 s):
// it clears CTexture's precache flag (DAT_18515c3a8), runs its Main part, queues the render thread's part (the system
// render targets and the rest, 0.5 s of the render thread) and waits for it, puts the flag back; slot 0x1a8 right
// after queues a 45 ms lambda and waits too (31 Main samples, ~0.65 s, in the 18:45 boot; the render thread was then
// idle 228 of 230 samples until the menu). Here both lambdas go to the render thread in one hand-over and Main goes
// on. The flag is cleared before the setup, so the game saves and puts back 0, and the render thread puts the real
// value back when its part ends (the lambda's vftable slot); until then Main's texture loads (ForName, which reads the
// flag to postpone or load) wait, and other threads see 0 as during the game's own wait.
// Mode 4 = mode 3 and CSystem::Init's flush right after (renderer slot 59 from +7a191c), where Main waited for the whole
// setup in mode 3 (boots 158rt3au/b: 521 / 434 ms, 0 ms after the hand-over): it is owed instead, and Main pays it (the
// game's own flush) at its next renderer call, texture load or real flush, or at the first frame; the renderer gate
// stays on until then. Mode 5 = mode 4 and the renderer's 58 one-instruction slots ungated (a field getter or a constant):
// in boot 161ar1 the wait only moved to slot 16 (the feature flags, read by the q_Renderer setting's callback 23 ms after
// the hand-over: 475 ms). The setup's call tree (938 functions, direct calls 5 deep) writes none of the 11 fields read.
// The log lines at the first frame give Main's real-flush waits, the first one after each hand-over (to see whether a
// wait only moved), and when the render thread ran the setup's part.
// "rtsync:" lines. fastload_rtsync.txt: 2 the shader-quality settings only (default), 1 and the shader manager, 3 and
// the system-resource setup too (queue 158: 1.0 s sooner to the menu with audiofast 1), 4 and CSystem::Init's flush after
// it (queue 162: with cpuprobe 1, audiofast 2, fastalloc 2 the menu 1.3 s sooner), 5 and the getters (not run yet), 0 off.
void Rtsync_Install(uintptr_t gameBase, void* selfModule);   // Init, before the renderer starts
void Rtsync_Frame();   // the frame tick: off from the first frame, its counts logged once
