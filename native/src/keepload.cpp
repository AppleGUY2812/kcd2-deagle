// keepload: the map (and later the inventory) stays loaded after the menu closes (fastload.asi).
//
// WHGame 1.5.6 FUN_18056259c(panel, next screen, own screen) decides, for every Apse panel listener, whether the panel's
// Flash movie unloads (wh_ui_ApseUnloadMode, DAT_1855e2b24). In the game's mode 2 the map (screen 6) stays loaded only
// while you switch tabs: closing the menu (next screen 0) unloads it, and the next open reloads the movie and re-sends
// every marker (FUN_180dc95d4 map fill: 100-125 ms per open, menutime 2026-10-26). The hook answers "keep" for the
// screens in the mask when the menu closes; the panel then skips the reload and the fill on the next open. Screens: 1, 2 inventory,
// 4 character, 6 map, 7. "keepload <mask>" (fastload_keepload.txt), default 64 = the map only; 0 = the game's choice.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"

namespace {
constexpr uintptr_t kRva = 0x56259c;
constexpr uint8_t kBytes[] = { 0x8b, 0x05, 0x82, 0x05, 0x08, 0x05, 0x85, 0xc0, 0x74, 0x3c, 0x84, 0xd2, 0x74, 0x51 };
typedef uint64_t(__fastcall* UnloadFn)(void* panel, char next, uint8_t own);
UnloadFn o_unload = nullptr;
unsigned g_mask = 1u << 6;
std::string g_file;
unsigned g_kept = 0, g_passed = 0;

uint64_t __fastcall HkUnload(void* panel, char next, uint8_t own) {
	uint64_t r = o_unload(panel, next, own);
	if (!(r & 0xff) || own >= 32 || !((g_mask >> own) & 1)) return r;
	// Only the menu closing (next screen 0) is overridden. Opening a kept screen in another mode (pickpocket, trade,
	// loot: the panel listener asks with the new screen) must reload it: overriding that left the inventory blank after
	// a pickpocket (2026-10-26, keepload 84), and after a sale with wh_ui_ApseUnloadMode 0 (2026-10-23).
	if (next != 0) {
		if (++g_passed <= 10 || g_passed % 50 == 0) Log("keepload: screen %u reloads for screen %d (the game's choice), %u times so far", own, next, g_passed);
		return r;
	}
	if (++g_kept <= 10 || g_kept % 50 == 0) Log("keepload: screen %u kept loaded (next screen %d), %u times so far", own, next, g_kept);
	return r & ~(uint64_t)0xff;
}
}  // namespace

void Keepload_Install(uintptr_t base, HMODULE self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	g_file = path;
	g_file = g_file.substr(0, g_file.find_last_of('\\') + 1) + "fastload_keepload.txt";
	if (FILE* fp = fopen(g_file.c_str(), "r")) {
		unsigned m;
		if (fscanf(fp, "%u", &m) == 1) g_mask = m;
		fclose(fp);
	}
	void* p = (void*)(base + kRva);
	if (memcmp(p, kBytes, sizeof kBytes)) { Log("keepload: code check failed - not installed (other game version?)"); return; }
	const bool h = MH_CreateHook(p, (void*)&HkUnload, (void**)&o_unload) == MH_OK && MH_EnableHook(p) == MH_OK;
	Log("keepload: %s, mask %u (64 = the map stays loaded after closing; 0 = the game's choice)", h ? "on" : "hook FAILED", g_mask);
}

void Keepload_Command(const char* args) {
	unsigned m;
	if (args && sscanf_s(args, "%u", &m) == 1) {
		g_mask = m;
		if (FILE* fp = fopen(g_file.c_str(), "w")) { fprintf(fp, "%u\n", m); fclose(fp); }
	}
	Log("keepload: mask %u (64 = map, 4 = inventory, 16 = character; 0 = the game's choice)", g_mask);
}
