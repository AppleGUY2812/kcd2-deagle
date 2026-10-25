// Ready at the menu: see readymenu.h.
#include "readymenu.h"
#include "animahead.h"
#include "log.h"
#include "questmenu.h"
#include "terrainfast.h"
#include "xmlahead.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// ---- WHGame.dll 1.5.6 (decompile 2026-10-24; bytes read from the installed DLL)
constexpr uintptr_t kRvaStartup = 0xda1d54;     // C_UISaveLoad start-up: intro / auto-load / main menu
const unsigned char kStartupBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x48,0x8b,0x0d };
constexpr uintptr_t kRvaLoadLast = 0x2ba7c10;   // "Loading last saved game...": the newest save, queued; true = started
const unsigned char kLoadLastBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x30,0x48,0x8b,0xf9,0x48,0x8d,0x15 };
constexpr uintptr_t kBusy = 0x72;    // Continue's lambda FUN_181f90aa0: owner+0x122 with C_UISaveLoad = owner+0xb0
constexpr uintptr_t kMenu = 0x120;   // C_UIMenu; its +0x58 controller's vt[1](page): 1 main menu, 3 pause menu

uintptr_t g_base = 0;
std::string g_dir;
std::atomic<int> g_mode{0}, g_idleS{5};
using StartupFn = void(__fastcall*)(uintptr_t);
using LoadLastFn = bool(__fastcall*)(uintptr_t);
StartupFn g_origStartup = nullptr;
std::atomic<uintptr_t> g_ui{0};   // the C_UISaveLoad
ULONGLONG g_start = 0, g_startupAt = 0, g_readyAt = 0, g_firedAt = 0;
bool g_fired = false, g_other = false, g_ours = false, g_paused = false, g_waitLogged = false;
int g_lastState = 0;

void __fastcall HkStartup(uintptr_t ui) {
	g_ui = ui;
	g_startupAt = GetTickCount64();
	uintptr_t menu = 0;
	__try { menu = *(uintptr_t*)(ui + kMenu); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	Log("readymenu: C_UISaveLoad %p (C_UIMenu %p, owner+0xb0 check %s), start-up %.1f s after the plugin", (void*)ui, (void*)menu,
	    menu == ui - 0xb0 ? "same object" : "another object", (g_startupAt - g_start) / 1000.0);
	g_origStartup(ui);
}

ULONGLONG IdleMs() {
	LASTINPUTINFO li{ sizeof li, 0 };
	return GetLastInputInfo(&li) ? GetTickCount() - li.dwTime : 0;
}

bool Busy(uintptr_t ui) {
	__try { return *(volatile uint8_t*)(ui + kBusy) != 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}

bool OpenPause(uintptr_t ui) {
	__try {
		uintptr_t ctrl = *(uintptr_t*)(ui + kMenu) + 0x58;
		void** vt = *(void***)ctrl;
		((void(__fastcall*)(uintptr_t, int))vt[1])(ctrl, 3);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Load() {
	if (FILE* f = fopen((g_dir + "fastload_readymenu.txt").c_str(), "r")) {
		int m = 0, s = 5;
		const int n = fscanf_s(f, "%d %d", &m, &s);
		if (n >= 1 && m >= 0 && m <= 1) g_mode = m;
		if (n >= 2 && s >= 2 && s <= 120) g_idleS = s;
		fclose(f);
	}
}

void Save() {
	if (FILE* f = fopen((g_dir + "fastload_readymenu.txt").c_str(), "w")) { fprintf(f, "%d %d\n", g_mode.load(), g_idleS.load()); fclose(f); }
}

}  // namespace

void Readymenu_Install(uintptr_t base, void* self) {
	g_base = base;
	g_start = GetTickCount64();
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path;
	g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	Load();
	void* st = (void*)(base + kRvaStartup);
	if (memcmp(st, kStartupBytes, sizeof kStartupBytes) || memcmp((void*)(base + kRvaLoadLast), kLoadLastBytes, sizeof kLoadLastBytes)) {
		Log("readymenu: code check failed - off (other game version?)");
		return;
	}
	if (MH_CreateHook(st, (void*)&HkStartup, (void**)&g_origStartup) != MH_OK || MH_EnableHook(st) != MH_OK) {
		Log("readymenu: start-up hook FAILED - off");
		return;
	}
	Log("readymenu: mode %d (%s), %d s without input", g_mode.load(), g_mode ? "the newest save loads by itself at the menu" : "off",
	    g_idleS.load());
}

void Readymenu_Frame(int s) {
	const uintptr_t ui = g_ui.load();
	if (!ui) return;
	if (s != 0 && !g_fired && !g_other) {   // a load started some other way (Continue, a save picked, the game's auto-load)
		g_other = true;
		if (g_mode) Log("readymenu: a load started before ours (level state %d): nothing to do this session", s);
	}
	if (g_ours && !g_paused && s == 0 && g_lastState != 0) {   // our load reached gameplay: the pause menu
		g_paused = true;
		const ULONGLONG now = GetTickCount64();
		const bool ok = OpenPause(ui);
		Log("readymenu: our load is in game after %.1f s (%.1f s after the plugin): pause menu %s", (now - g_firedAt) / 1000.0,
		    (now - g_start) / 1000.0, ok ? "opened" : "FAILED");
	}
	g_lastState = s;
	if (!g_mode || g_fired || g_other || s != 0 || Xmlahead_LevelSeen()) return;
	if (!Questmenu_SaveLevel() || !Animahead_MenuSettled()) return;
	// The terrain preload waits for RAM (the 1.5 GB rule); blocked for 1 s, the load reads the rest of its meshes itself
	// (boot 77: 1,267 frames of RAM waits, 14.4 s instead of 6.3 s, and the load started 8 s later for it).
	const int ramMs = Terrainfast_MenuRamBlockedMs();
	if (!Terrainfast_MenuPreloadDone() && ramMs < 1000) return;
	const ULONGLONG now = GetTickCount64();
	if (!g_readyAt) {
		g_readyAt = now;
		Log("readymenu: menu work done %.1f s after the plugin (%.1f s after the menu's start-up)%s", (now - g_start) / 1000.0,
		    (now - g_startupAt) / 1000.0, Terrainfast_MenuPreloadDone() ? "" : "; the terrain preload is waiting for RAM, the load does the rest");
	}
	if (now - g_readyAt < 1000) return;   // the last preloads' requests come back
	const ULONGLONG idle = IdleMs();
	if (idle < (ULONGLONG)g_idleS * 1000 || Busy(ui)) {
		if (!g_waitLogged) { g_waitLogged = true; Log("readymenu: waiting (input %.1f s ago, menu busy %d)", idle / 1000.0, Busy(ui) ? 1 : 0); }
		return;
	}
	g_fired = true;
	g_firedAt = now;
	const bool ok = ((LoadLastFn)(g_base + kRvaLoadLast))(ui);
	g_ours = ok;
	Log("readymenu: the newest save's load %s %.1f s after the plugin (menu work done %.1f s before, no input for %.1f s)",
	    ok ? "started" : "NOT started (the game said no)", (now - g_start) / 1000.0, (now - g_readyAt) / 1000.0, idle / 1000.0);
}

void Readymenu_Command(const char* args) {
	int m = -1, s = -1;
	const int n = args && *args ? sscanf_s(args, "%d %d", &m, &s) : 0;
	if (n >= 1 && m >= 0 && m <= 1) g_mode = m;
	if (n >= 2 && s >= 2 && s <= 120) g_idleS = s;
	if (n >= 1) Save();
	Log("readymenu: mode %d, %d s without input; C_UISaveLoad %p, %s", g_mode.load(), g_idleS.load(), (void*)g_ui.load(),
	    g_ours ? (g_paused ? "our load ran, pause menu opened" : "our load runs") : g_fired ? "the game refused our load"
	    : g_other ? "a load was started another way" : "not started yet");
}
