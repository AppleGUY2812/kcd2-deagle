// perkcopy: the perk-set copy behind the inventory / map open without its cubic re-checks (fastload.asi).
//
// Opening the "Apse" (inventory, map) copies the player's perk set into another perk manager: WHGame 1.5.6
// FUN_18046c02c(dst, src) clears dst, then adds src's perks one by one through FUN_18046b4a4 (add perk), and every add
// ends in FUN_1804688c0(dst): a pass over every perk in the game (DAT_18532e180, 0x78 bytes each) that unlocks the ones
// whose skill / stat level and parent perk are met (FUN_180468978), and the parent check is a linear search of dst's
// perks (FUN_1804686e8). Adds x perks x perks: 56 % of the 180-250 ms frame before the menu appears is that search
// (the user's 2026-10-26 17:22-17:25 opens, 81 samples).
//
// The fix: while FUN_18046c02c runs, FUN_1804688c0 on its dst returns "nothing unlocked" at once; when the copy is done
// the pass runs on dst until it unlocks nothing more (at most 16 passes). Unlocks only add perks and src is already
// a complete set, so dst ends with the same perks; the listeners still hear every add. "perkcopy 0"
// (fastload_perkcopy.txt) turns it off.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"

namespace {
constexpr uintptr_t kRvaCopy = 0x46c02c, kRvaPass = 0x4688c0;
constexpr uint8_t kCopyBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x6c, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57, 0x48 };
constexpr uint8_t kPassBytes[] = { 0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18,
                                   0x48, 0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83 };
typedef void(__fastcall* CopyFn)(void* dst, void* src);
typedef uint8_t(__fastcall* PassFn)(void* mgr);
CopyFn g_copy = nullptr;
PassFn g_pass = nullptr;
int g_mode = 1;
std::string g_file;
thread_local void* t_dst = nullptr;
thread_local unsigned t_skipped = 0;
unsigned g_copies = 0;
double g_freq = 0;

uint8_t __fastcall HkPass(void* mgr) {
	if (t_dst && mgr == t_dst) { t_skipped++; return 0; }
	return g_pass(mgr);
}

void __fastcall HkCopy(void* dst, void* src) {
	if (!g_mode || t_dst) { g_copy(dst, src); return; }
	LARGE_INTEGER a, b;
	QueryPerformanceCounter(&a);
	t_dst = dst; t_skipped = 0;
	g_copy(dst, src);
	t_dst = nullptr;
	int passes = 0;
	while (passes < 16) { passes++; if (!g_pass(dst)) break; }
	QueryPerformanceCounter(&b);
	if (++g_copies <= 20 || g_copies % 100 == 0)
		Log("perkcopy: copy #%u in %.1f ms: %u re-checks skipped, %d pass%s at the end", g_copies,
		    (b.QuadPart - a.QuadPart) * 1000.0 / g_freq, t_skipped, passes, passes == 1 ? "" : "es");
}
}  // namespace

void Perkcopy_Install(uintptr_t base, HMODULE self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	g_file = path;
	g_file = g_file.substr(0, g_file.find_last_of('\\') + 1) + "fastload_perkcopy.txt";
	if (FILE* fp = fopen(g_file.c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1 && (m == 0 || m == 1)) g_mode = m;
		fclose(fp);
	}
	void* c = (void*)(base + kRvaCopy);
	void* p = (void*)(base + kRvaPass);
	if (memcmp(c, kCopyBytes, sizeof kCopyBytes) || memcmp(p, kPassBytes, sizeof kPassBytes)) {
		Log("perkcopy: code check failed - not installed (other game version?)");
		return;
	}
	const bool h = MH_CreateHook(p, (void*)&HkPass, (void**)&g_pass) == MH_OK && MH_CreateHook(c, (void*)&HkCopy, (void**)&g_copy) == MH_OK &&
	               MH_EnableHook(p) == MH_OK && MH_EnableHook(c) == MH_OK;
	Log("perkcopy: %s, mode %d (%s)", h ? "on" : "hook FAILED", g_mode, g_mode ? "one unlock check after the copy" : "off: a check after every perk");
}

void Perkcopy_Command(const char* args) {
	int m = -1;
	if (args && sscanf_s(args, "%d", &m) == 1 && (m == 0 || m == 1)) {
		g_mode = m;
		if (FILE* fp = fopen(g_file.c_str(), "w")) { fprintf(fp, "%d\n", m); fclose(fp); }
	}
	Log("perkcopy: mode %d (perkcopy 1 = one unlock check after the inventory's perk copy, 0 = off)", g_mode);
}
