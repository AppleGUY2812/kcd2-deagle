// mapvd: no octree rebuild when the map or the inventory opens or closes (fastload.asi).
//
// The 3D engine re-files every object in its octree when the sum of 23 e_ settings changes (WHGame 1.5.6
// FUN_180a3bf48: the sum against C3DEngine+0x6a8, then FUN_18076a7fc ReRegisterEntitiesInArea; hitch.cpp OctWatch).
// The full-UI screens (wh::guimodule::C_UIFullUIModeHelper: map, inventory) lower e_ViewDistRatio 140 -> 100 on open
// and put it back on close, so every open costs a 450-530 ms frame and every close 260-360 ms (the user's 2026-10-26
// 16:03-16:11 session; "there is a black screen when opening the inventory or map screen and i want that to be instant").
//
// The fix: while e_ViewDistRatio is below the highest value seen so far, the rebuild check runs with that value written
// back for the length of the call, so the sum doesn't move and nothing is re-filed. Objects keep the view distances
// they were filed with (the world behind the menu draws as far as in play). A raise goes through as usual (a real
// settings change). A lowered setting from the options menu also stays masked until the next launch.
// "mapvd 0" (fastload_mapvd.txt) turns it off.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"

namespace {
constexpr uintptr_t kRvaOctCheck = 0xa3bf48;   // FUN_180a3bf48(C3DEngine*)
constexpr uint8_t kOctBytes[] = { 0x4c, 0x8b, 0xdc, 0x49, 0x89, 0x5b, 0x08, 0x49, 0x89, 0x6b, 0x10, 0x49, 0x89, 0x73, 0x18,
                                  0x57, 0x48, 0x83, 0xec, 0x60 };
constexpr uintptr_t kRvaCVars = 0x5496fd0;      // DAT_185496fd0: the engine's CVars block (pointer)
constexpr int kSlotViewDistRatio = 0x1c1;       // float e_ViewDistRatio

typedef void(__fastcall* OctFn)(void*);
OctFn g_orig = nullptr;
uintptr_t g_base = 0;
int g_mode = 1;
std::string g_file;
float g_high = 0;          // the highest e_ViewDistRatio seen: what the rebuild check is shown
bool g_have = false, g_masking = false;
unsigned g_masks = 0;

float* Slot() {
	__try {
		float* cv = *(float**)(g_base + kRvaCVars);
		return cv ? cv + kSlotViewDistRatio : nullptr;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void __fastcall HkOct(void* eng) {
	float* p = g_mode ? Slot() : nullptr;
	if (!p) { g_orig(eng); return; }
	const float v = *p;
	if (!g_have || v >= g_high || !(v > 0)) {
		if (g_masking) Log("mapvd: e_ViewDistRatio back to %g: no rebuild (masked %u times so far)", v, g_masks);
		g_masking = false;
		if (g_have && v > g_high) Log("mapvd: e_ViewDistRatio raised %g -> %g: the engine re-files as usual", g_high, v);
		if (v > 0) { g_high = v; g_have = true; }
		g_orig(eng);
		return;
	}
	if (!g_masking) { g_masking = true; g_masks++; Log("mapvd: e_ViewDistRatio lowered %g -> %g (a menu): the octree keeps %g, no rebuild", g_high, v, g_high); }
	*(volatile float*)p = g_high;
	g_orig(eng);
	*(volatile float*)p = v;
}
}  // namespace

bool Mapvd_Masking() { return g_masking; }
extern bool (*Hitch_MapvdMasking)();   // hitch.cpp: its log says "masked" instead of "rebuilds"

void Mapvd_Install(uintptr_t base, HMODULE self) {
	g_base = base;
	Hitch_MapvdMasking = &Mapvd_Masking;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	g_file = path;
	g_file = g_file.substr(0, g_file.find_last_of('\\') + 1) + "fastload_mapvd.txt";
	if (FILE* f = fopen(g_file.c_str(), "r")) {
		int m = 1;
		if (fscanf(f, "%d", &m) == 1 && (m == 0 || m == 1)) g_mode = m;
		fclose(f);
	}
	void* fn = (void*)(base + kRvaOctCheck);
	if (memcmp(fn, kOctBytes, sizeof kOctBytes)) { Log("mapvd: code check failed - not installed (other game version?)"); return; }
	const bool h = MH_CreateHook(fn, (void*)&HkOct, (void**)&g_orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
	Log("mapvd: %s, mode %d (%s)", h ? "on" : "hook FAILED", g_mode,
	    g_mode ? "map / inventory: no octree rebuild" : "off: the game's own rebuilds");
}

void Mapvd_Command(const char* args) {
	int m = -1;
	if (args && sscanf_s(args, "%d", &m) == 1 && (m == 0 || m == 1)) {
		g_mode = m;
		if (FILE* f = fopen(g_file.c_str(), "w")) { fprintf(f, "%d\n", m); fclose(f); }
	}
	Log("mapvd: mode %d (mapvd 1 = no octree rebuild when a menu lowers the view distance, 0 = off)", g_mode);
}
