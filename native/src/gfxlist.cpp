// gfxlist: Scaleform's clip-stop no longer walks the movie's advance list (fastload.asi).
//
// GFx keeps the clips that play every frame in a singly linked "optimized advance list" (movie +0x2b70, next at clip
// +0xc0). When a clip leaves it, WHGame 1.5.6 FUN_181c997c0 finds the clip's predecessor by walking back over the
// clips before it (+0xb8) until one is still listed. The map's open adds a one-frame clip per marker that stops at
// once, each walk passes every marker removed before it: quadratic in the marker count, ~67 ms of every map open and
// ~8 ms of an inventory open (2026-10-26, agent decode + 148 samples: menu_freeze_B.md).
//
// GFx already has the cheap path: with the movie flag +0x25e8 bit 19 ("list invalid") set, the stop skips the walk and
// Advance (FUN_181ca4d90) rebuilds the list once from the full playlist (+0x2b68) and clears the bit. Every reader of
// +0x2b70 checks that bit first, and the add path skips inserting while it is set. Two bytes make every stop take it:
//   0x181c997db  75 49 (JNZ) -> EB 49 (JMP): always skip the walk;
//   0x181c99842  OR [movie+0x25e8], 0x400 -> 0x80400: mark the list invalid as well.
// Cost: a clip after the stopped one can miss one frame's advance in that frame. "gfxlist 0" (fastload_gfxlist.txt)
// keeps the game's code (read at start).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "log.h"

namespace {
constexpr uintptr_t kRvaJnz = 0x1c997d9, kRvaOr = 0x1c9983c;
constexpr uint8_t kJnzOld[] = { 0xa8, 0x01, 0x75, 0x49 }, kJnzNew[] = { 0xa8, 0x01, 0xeb, 0x49 };
constexpr uint8_t kOrOld[] = { 0x81, 0x8a, 0xe8, 0x25, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00 };
constexpr uint8_t kOrNew[] = { 0x81, 0x8a, 0xe8, 0x25, 0x00, 0x00, 0x00, 0x04, 0x08, 0x00 };

bool Write(void* at, const uint8_t* b, size_t n) {
	DWORD old = 0;
	if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
	memcpy(at, b, n);
	VirtualProtect(at, n, old, &old);
	FlushInstructionCache(GetCurrentProcess(), at, n);
	return true;
}
}  // namespace

void Gfxlist_Install(uintptr_t base, HMODULE self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string file(path);
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_gfxlist.txt";
	int mode = 1;
	if (FILE* f = fopen(file.c_str(), "r")) { int m = 1; if (fscanf(f, "%d", &m) == 1 && (m == 0 || m == 1)) mode = m; fclose(f); }
	if (!mode) { Log("gfxlist: off (fastload_gfxlist.txt 0): the game's clip-stop walk"); return; }
	void* j = (void*)(base + kRvaJnz);
	void* o = (void*)(base + kRvaOr);
	if (memcmp(j, kJnzOld, sizeof kJnzOld) || memcmp(o, kOrOld, sizeof kOrOld)) {
		Log("gfxlist: code check failed - not patched (other game version?)");
		return;
	}
	// the OR first: a stop that runs between the two writes then only marks the list invalid while still walking
	const bool ok = Write(o, kOrNew, sizeof kOrNew) && Write(j, kJnzNew, sizeof kJnzNew);
	Log("gfxlist: %s (a stopped clip marks the advance list for one rebuild instead of walking it)", ok ? "on" : "patch FAILED");
}
