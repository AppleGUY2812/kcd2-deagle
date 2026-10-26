#pragma once
#include <cstdint>
// The game's small memory blocks from mimalloc (2026-10-25; MODLOG "CryMalloc"). WHGame 1.5.6's CryMalloc
// (FUN_1804f7610) serves blocks of up to 512 bytes from shared lock-free lists, 4 per size, refilled under one SRW lock
// (FUN_1804c01a8); bigger blocks come from the CRT heap. Every thread pops and pushes the same list heads, so in the level
// load the allocator was ~10 % of Main (boot 138's exact profile: CryMalloc 0.95 s of 11.5 s, 0.35 s of it in
// RtlInterlockedPopEntrySList, 0.17 s in the refill lock; CryFree 0.25 s), and the start-up's text step waited on the heap.
// With this on, those small blocks come from mimalloc 2.5.2 (third_party/mimalloc): one heap per thread, so no shared
// list heads. Its blocks live in one address range reserved at start (32 GB of address space, committed as used, in an
// arena only these heaps use), so CryFree / CryRealloc tell them from the game's own blocks with one compare; the
// game's blocks (made before the hook, or bigger than 512 bytes) go the game's way. Each block is aligned like the
// game's buckets (to the lowest set bit of its size rounded to 16, at least what the caller asked), and the game's
// allocated-bytes counter (DAT_18547d8e8) gets the same additions and subtractions. Installed from DllMain before
// WHGame.dll's first allocation when the ASI loader loads us there (fastload_main.cpp), else at Init.
// fastload_fastalloc.txt next to the plugin: 1 on, 0 off (the game's allocator; the default until tested).
void Fastalloc_Install(uintptr_t gameBase, void* selfModule);
void Fastalloc_Report(const char* when);   // heaps made, fallbacks (arena full), the process's commit
