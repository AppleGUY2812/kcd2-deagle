// inventory.asi: the native companion of the Inventory Tools mod (inventory/), a plugin of its own:
// Bin/Win64MasterMasterSteamPGO/inventory.asi, loaded by the ASI loader (dinput8.dll) with or without the Deagle mod.
// It does the two things KCD2's menus can't do in Lua:
//   - reads the fast-transfer key (G by default) every frame while KCD2 has focus, so it works in the inventory, chest
//     and shop screens (their action maps pass Lua no keys): "inv_fastkey 1" on the press, again every 150 ms while it
//     stays down (the first repeat after 400 ms, like a key repeat), "inv_fastkey 0" on the release; and Shift / Ctrl
//     for the weight map's multi-select, as "inv_mods <1 shift + 2 ctrl>" on change only (Scaleform's Key.isDown never
//     sees them there);
//   - the inventory / map screen's speed (invtime.cpp): "invtime 2" skips the clothing gate before the fade.
// Lua -> native commands through System.LogAlways("[INV] ..."); the console queue goes the other way, through KCD2's
// own console. Its settings file is inventory_settings.txt and its log inventory.log, both next to the .asi.
// The frame tick is CSystem::Update's vtable slot (as fastload.asi; it chains another mod's patch), so it shares no
// inline hook with deagle_native.asi. Patterns from WHGame 1.5.6 (main.cpp / fastload_main.cpp, docs/LOADTIME.md).
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <vector>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include "keys.h"
#include "invtime.h"
#include "settings.h"

namespace {

HMODULE g_self = nullptr;
Module g_game;
uintptr_t g_env = 0;

static std::deque<std::string> g_console;   // console commands to run next frame (native -> Lua), in order
static std::mutex g_consoleMu;

// the fast-transfer key: "fastkey <key>|off" (inventory_main.lua); 0 = off
static int g_fastVk = 0;

static bool ReadPtr(uintptr_t addr, uintptr_t& out) {
	__try { out = *(const uintptr_t*)addr; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}

// ---- KCD2's input / console through its vtable slots (main.cpp; WHGame 1.5.6)
static uintptr_t VFn(uintptr_t obj, int off) { uintptr_t vt; return ReadPtr(obj, vt) && ReadPtr(vt + off, vt) ? vt : 0; }
using ExecFn = void(__fastcall*)(uintptr_t con, const char* cmd, bool silent, bool deferred);   // IConsole +0x118
static void RunConsole(const char* cmd) {
	uintptr_t con; if (!ReadPtr(g_env + 0xA8, con) || !con) return;
	if (auto exec = (ExecFn)VFn(con, 0x118)) exec(con, cmd, true, false);
}
static bool SafeConsole(const char* cmd) {
	__try { RunConsole(cmd); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static int VkOfKey(const char* k) {   // CryEngine's key name -> the Windows virtual key GetAsyncKeyState reads (0: none)
	if (!k[1] && k[0] >= 'a' && k[0] <= 'z') return 'A' + (k[0] - 'a');
	if (!k[1] && k[0] >= '0' && k[0] <= '9') return k[0];
	if (k[0] == 'f' && k[1] >= '1' && k[1] <= '9') { int n = atoi(k + 1); if (n >= 1 && n <= 24) return VK_F1 + n - 1; }
	if (!strncmp(k, "np_", 3) && k[3] >= '0' && k[3] <= '9' && !k[4]) return VK_NUMPAD0 + (k[3] - '0');
	if (!strncmp(k, "mouse", 5) && k[5] >= '1' && k[5] <= '5' && !k[6]) {
		static const int m[] = { VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_XBUTTON1, VK_XBUTTON2 };
		return m[k[5] - '1'];
	}
	static const struct { const char* n; int vk; } t[] = {
		{ "lshift", VK_LSHIFT }, { "rshift", VK_RSHIFT }, { "lctrl", VK_LCONTROL }, { "rctrl", VK_RCONTROL },
		{ "lalt", VK_LMENU }, { "ralt", VK_RMENU }, { "space", VK_SPACE }, { "tab", VK_TAB }, { "enter", VK_RETURN },
		{ "backspace", VK_BACK }, { "capslock", VK_CAPITAL }, { "escape", VK_ESCAPE }, { "up", VK_UP }, { "down", VK_DOWN },
		{ "left", VK_LEFT }, { "right", VK_RIGHT }, { "insert", VK_INSERT }, { "delete", VK_DELETE }, { "home", VK_HOME },
		{ "end", VK_END }, { "pgup", VK_PRIOR }, { "pgdn", VK_NEXT }, { "tilde", VK_OEM_3 }, { "minus", VK_OEM_MINUS },
		{ "equals", VK_OEM_PLUS }, { "lbracket", VK_OEM_4 }, { "rbracket", VK_OEM_6 }, { "backslash", VK_OEM_5 },
		{ "semicolon", VK_OEM_1 }, { "apostrophe", VK_OEM_7 }, { "comma", VK_OEM_COMMA }, { "period", VK_OEM_PERIOD },
		{ "slash", VK_OEM_2 }, { "oem_102", VK_OEM_102 }, { "np_add", VK_ADD }, { "np_subtract", VK_SUBTRACT },
		{ "np_multiply", VK_MULTIPLY }, { "np_divide", VK_DIVIDE }, { "np_period", VK_DECIMAL }, { "np_enter", VK_RETURN },
		{ "numlock", VK_NUMLOCK }, { "scrolllock", VK_SCROLL }, { "pause", VK_PAUSE }, { "print", VK_SNAPSHOT },
		{ "lwin", VK_LWIN }, { "rwin", VK_RWIN }, { "apps", VK_APPS } };
	for (auto& e : t) if (!strcmp(k, e.n)) return e.vk;
	return 0;
}

static bool HasFocus() {
	HWND w = GetForegroundWindow();
	DWORD pid = 0;
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}

// ---- the frame tick: CSystem::Update's vtable slot (fastload_main.cpp's PatchUpdate), which already chains a patch
constexpr uintptr_t kRvaSystemVt = 0x3dc2858, kSlotUpdate = 0x28, kRvaUpdate = 0x6678c4;
const unsigned char kUpdateBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x70,0x18,0x48,0x89,0x78,0x20,0x55 };
using UpdateFn = uintptr_t(__fastcall*)(void* sys, unsigned flags, int pause);
UpdateFn g_origUpdate = nullptr;
std::atomic<DWORD> g_mainTid{ 0 };

static void Tick() {
	{   // Shift / Ctrl for the weight map's multi-select, sent on change only: "inv_mods <1 shift + 2 ctrl>"
		static int wasMods = -1;
		int m = 0;
		if (g_fastVk && HasFocus())
			m = ((GetAsyncKeyState(VK_SHIFT) & 0x8000) ? 1 : 0) | ((GetAsyncKeyState(VK_CONTROL) & 0x8000) ? 2 : 0);
		if (m != wasMods) {
			wasMods = m;
			char b[32];
			snprintf(b, sizeof b, "inv_mods %d", m);
			std::lock_guard<std::mutex> lk(g_consoleMu);
			g_console.push_back(b);
		}
	}
	{   // fastkey: the fast-transfer key (see g_fastVk)
		static bool was = false;
		static DWORD next = 0;
		bool d = g_fastVk && HasFocus() && (GetAsyncKeyState(g_fastVk) & 0x8000) != 0;
		DWORD now = GetTickCount();
		const char* send = nullptr;
		if (d && (!was || (int)(now - next) >= 0)) { send = "inv_fastkey 1"; next = now + (was ? 150 : 400); }
		else if (!d && was) send = "inv_fastkey 0";
		was = d;
		if (send) { std::lock_guard<std::mutex> lk(g_consoleMu); g_console.push_back(send); }
	}
	Keys_Frame();   // key presses Lua asked for (keys.cpp)
	if (g_env) {
		std::deque<std::string> run;
		{ std::lock_guard<std::mutex> lk(g_consoleMu); run.swap(g_console); }
		for (auto& c : run) SafeConsole(c.c_str());
	}
}

uintptr_t __fastcall HkUpdate(void* sys, unsigned flags, int pause) {
	const uintptr_t r = g_origUpdate(sys, flags, pause);
	DWORD want = 0;
	const DWORD me = GetCurrentThreadId();
	if (g_mainTid.compare_exchange_strong(want, me)) Log("inventory: frame tick on thread %lu", me);
	if (want == 0 || want == me) Tick();
	return r;
}

static bool PatchUpdate() {
	void** slot = (void**)(g_game.base + kRvaSystemVt + kSlotUpdate);
	uintptr_t cur = 0;
	unsigned char head[sizeof(kUpdateBytes)] = {};
	if (!ReadPtr((uintptr_t)slot, cur)) { Log("inventory: CSystem vtable unreadable: no frame tick"); return false; }
	const bool ours = cur == g_game.base + kRvaUpdate;
	const bool foreign = cur && (cur < g_game.base || cur >= g_game.base + g_game.size);   // another mod's patch: chain it
	if (!ours && !foreign) { Log("inventory: CSystem::Update slot holds %p, not the expected function: no frame tick", (void*)cur); return false; }
	if (ours) {
		__try { memcpy(head, (const void*)cur, sizeof(head)); } __except (EXCEPTION_EXECUTE_HANDLER) { head[0] = 0; }
		if (memcmp(head, kUpdateBytes, sizeof(head))) {
			Log("inventory: CSystem::Update has other bytes (another game version?): no frame tick");
			return false;
		}
	}
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old)) { Log("inventory: VirtualProtect failed: no frame tick"); return false; }
	g_origUpdate = (UpdateFn)cur;
	InterlockedExchangePointer(slot, (void*)&HkUpdate);
	VirtualProtect(slot, sizeof(void*), old, &old);
	Log("inventory: frame tick on CSystem::Update (vtable +0x%llx%s)", (unsigned long long)kSlotUpdate,
	    foreign ? ", chained to another mod's patch" : "");
	return true;
}

// ---- the Lua channel: System.LogAlways("[INV] <cmd> ..."), hooked as in main.cpp; deagle_native.asi hooks the same
// handler, so FindLogAlwaysHandler accepts a function another plugin already starts with a jump.
using LogHandlerFn = int(__fastcall*)(void* self, uintptr_t pH);
using GetParamStrFn = bool(__fastcall*)(uintptr_t pH, int idx, const char** out);
static LogHandlerFn g_origLog = nullptr;
static GetParamStrFn g_getParamStr = nullptr;

static const char* LuaString(uintptr_t pH) {
	__try { const char* s = nullptr; return g_getParamStr && g_getParamStr(pH, 1, &s) ? s : nullptr; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static void Hello() {   // every save load / script start: the plugin knows nothing about the last one
	Keys_Reset();   // let go of the keys Lua pressed (keys.cpp)
	std::vector<std::string> saved = SettingsReplay();
	{
		std::lock_guard<std::mutex> lk(g_consoleMu);
		g_console.clear();
		for (auto& c : saved) g_console.push_back(c);
		g_console.push_back("inv_native_ok");
	}
	Log("hello from Lua (%zu settings to replay)", saved.size());
}

static void Command(const char* line) {
	char cmd[32] = {};
	if (sscanf_s(line, "%31s", cmd, (unsigned)sizeof(cmd)) != 1) return;
	const char* rest = line + strlen(cmd);
	while (*rest == ' ') rest++;
	if (!strcmp(cmd, "hello")) Hello();
	else if (!strcmp(cmd, "fastkey")) {
		char k[24] = {};
		sscanf_s(rest, "%23s", k, (unsigned)sizeof(k));
		g_fastVk = strcmp(k, "off") ? VkOfKey(k) : 0;
		Log("fastkey %s -> vk %d", k, g_fastVk);
	}
	else if (!strcmp(cmd, "invtime")) Invtime_Command(rest);
	else if (!strcmp(cmd, "set")) {   // a kept setting: "set <key> <value>" -> inventory_settings.txt
		char key[96] = {};
		if (sscanf_s(rest, "%95s", key, (unsigned)sizeof(key)) == 1) {
			const char* v = rest + strlen(key);
			while (*v == ' ') v++;
			SettingsSave(key, v);
		}
	}
	else if (!strcmp(cmd, "key")) Keys_Command(rest);   // key <name> down|up|hold, key allup (keys.h)
}

static int __fastcall HkLog(void* self, uintptr_t pH) {
	const char* s = LuaString(pH);
	if (s && s[0] == '[' && s[1] == 'I' && s[2] == 'N' && s[3] == 'V' && s[4] == ']') {
		Command(s + 5 + (s[5] == ' ' ? 1 : 0));   // after "[INV]" (5 characters)
		if (auto end = (int(__fastcall*)(uintptr_t))VFn(pH, 0x58)) return end(pH);   // pH->EndFunction()
	}
	return g_origLog(self, pH);
}

// ---- gEnv, as main.cpp finds it: two RIP-relative patterns, the one whose +0x108 renderer has the D3D renderer's RTTI
static bool ValidEnv(uintptr_t env) {
	uintptr_t rnd, vt;
	if (!ReadPtr(env + 0x108, rnd) || !rnd || !ReadPtr(rnd, vt)) return false;
	return RttiName(g_game, vt).find("Renderer") != std::string::npos;
}

static uintptr_t FindEnv() {
	uintptr_t cands[2] = {};
	if (uintptr_t a = FindUnique(g_game, "48 8D 05 ?? ?? ?? ?? 48 89 0F 4C 8D 67 28 48 8D 0D ?? ?? ?? ?? 48 89 47 20 48 89 4F 08"))
		cands[0] = RipTarget(a, 3, 7);
	if (uintptr_t b = FindUnique(g_game, "48 8B 51 08 48 8B C8 FF D2 48 8B 0D ?? ?? ?? ?? 48 85 C9"))
		cands[1] = RipTarget(b + 9, 3, 7);
	for (uintptr_t c : cands) {
		if (!c) continue;
		if (ValidEnv(c)) return c;
		uintptr_t d = 0;
		if (ReadPtr(c, d) && d && ValidEnv(d)) return d;
	}
	return 0;
}

static DWORD WINAPI Init(LPVOID) {
	Log("inventory v1 loaded");
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); i++) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("WHGame.dll not found"); return 0; }
	Log("WHGame.dll at %p size %zu", (void*)g_game.base, g_game.size);
	if (MH_Initialize() != MH_OK) { Log("MinHook init failed"); return 0; }
	uintptr_t gp = FindUnique(g_game, "48 89 5C 24 08 57 48 83 EC ?? 48 8B 01 49 8B F8 4C 8D 44 24 20 C7 44 24 20 05 00 00 00 FF 50 38 32 DB 84 C0 74 11");
	uintptr_t lh = FindLogAlwaysHandler(g_game);
	g_getParamStr = (GetParamStrFn)gp;
	Log("GetParam<str> %p  LogAlways handler %p", (void*)gp, (void*)lh);
	if (gp && lh && MH_CreateHook((void*)lh, (void*)&HkLog, (void**)&g_origLog) == MH_OK && MH_EnableHook((void*)lh) == MH_OK)
		Log("Lua channel hook installed");
	else
		Log("Lua channel hook FAILED");
	for (int i = 0; i < 240 && !g_env; i++) { g_env = FindEnv(); if (!g_env) Sleep(500); }
	Log("gEnv %p", (void*)g_env);
	if (g_env) Keys_Install(g_game, g_env);   // key presses through KCD2's input system (keys.cpp)
	if (!PatchUpdate())
		Log("inventory: no frame tick: the fast-transfer key and the console queue are idle this session");
	return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = h;
		DisableThreadLibraryCalls(h);
		LogInit(h);   // inventory.log next to the .asi
		SettingsInit(h, "inventory_settings.txt", "inv_set");
		CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
	}
	return TRUE;
}
