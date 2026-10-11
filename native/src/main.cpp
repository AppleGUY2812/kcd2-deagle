// deagle_native: native companion of the Deagle mod for KCD2 (Steam 1.5.6).
// v1: gEnv + camera frustum hook (final first-person camera, before rendering); places the Lua-spawned viewmodel
// entity on that camera every frame (no lag); Lua -> native commands through System.LogAlways("[DN] ...");
// sounds through the game's fmod.dll (own FMOD core system). RE notes: MODLOG.md / scratchpad re/ (WHGame 1.5.6).
// Patterns and offsets credit: tkhquang/KCD2Tools (TPVCamera, MIT).
#include <windows.h>
#include <atomic>
#include <cmath>
#include "MinHook.h"
#include "log.h"
#include "scan.h"
#include <string>
#include <unordered_map>
#include <cstdio>
#include <cstring>

static HMODULE g_self;
static Module g_game;
static uintptr_t g_env = 0;                  // SSystemGlobalEnvironment*

// ---- camera: CCamera::UpdateFrustumPlanes(camera), called ~50x/frame; the game view's camera sits at CView+0xE8
using FrustumFn = uintptr_t(__fastcall*)(uintptr_t camera);
static FrustumFn g_origFrustum = nullptr;
static uintptr_t g_cviewVtbl = 0;
static std::atomic<uint64_t> g_viewFrames{ 0 };
static float g_cam[12];                      // Matrix34 rows (right/forward/up columns, translation in [3],[7],[11])

// guarded pointer read (no C++ objects in here: MSVC forbids __try next to unwinding)
static bool ReadPtr(uintptr_t addr, uintptr_t& out) {
	__try { out = *(uintptr_t*)addr; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}

// ---- engine calls (vtable byte offsets, WHGame 1.5.6)
static uintptr_t VFn(uintptr_t obj, int off) { uintptr_t vt; return ReadPtr(obj, vt) && ReadPtr(vt + off, vt) ? vt : 0; }
using GetEntityFn = uintptr_t(__fastcall*)(uintptr_t es, uint32_t id);              // IEntitySystem +0x70
using SetWorldTMFn = void(__fastcall*)(uintptr_t e, const float* tm34, int flags);  // IEntity +0xF0
using ExecFn = void(__fastcall*)(uintptr_t con, const char* cmd, bool silent, bool deferred);  // IConsole +0x118

// ---- viewmodel follow
static uint32_t g_vmId = 0;
static bool g_follow = false;
static float g_off[3] = { 0, 0, 0 };            // right, forward, up (m)
static uint64_t g_placed = 0;
static std::string g_pendingConsole;           // console command to run next frame (native -> Lua)

static bool PlaceViewmodel(const float* cam) {
	uintptr_t es; if (!ReadPtr(g_env + 0xA0, es) || !es) return false;
	auto getEntity = (GetEntityFn)VFn(es, 0x70);
	if (!getEntity) return false;
	uintptr_t e = getEntity(es, g_vmId);
	if (!e) return false;
	auto setTM = (SetWorldTMFn)VFn(e, 0xF0);
	if (!setTM) return false;
	float tm[12];
	memcpy(tm, cam, sizeof(tm));
	for (int r = 0; r < 3; r++)   // translation += right*o0 + fwd*o1 + up*o2 (columns 0,1,2 of row r)
		tm[r * 4 + 3] += cam[r * 4 + 0] * g_off[0] + cam[r * 4 + 1] * g_off[1] + cam[r * 4 + 2] * g_off[2];
	setTM(e, tm, 0);
	return true;
}

static bool SafePlace(const float* cam) {
	__try { return PlaceViewmodel(cam); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void RunConsole(const char* cmd) {
	uintptr_t con; if (!ReadPtr(g_env + 0xA8, con) || !con) return;
	if (auto exec = (ExecFn)VFn(con, 0x118)) exec(con, cmd, true, false);
}
static bool SafeConsole(const char* cmd) {
	__try { RunConsole(cmd); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// ---- sounds: a separate FMOD core system from the game's own fmod.dll (same 2.02.21 build)
typedef int (__stdcall* F_SysCreate)(void** sys, unsigned int ver);
typedef int (__stdcall* F_SysInit)(void* sys, int ch, unsigned int flags, void* extra);
typedef int (__stdcall* F_CreateSound)(void* sys, const char* name, unsigned int mode, void* exinfo, void** snd);
typedef int (__stdcall* F_PlaySound)(void* sys, void* snd, void* group, int paused, void** chan);
typedef int (__stdcall* F_Update)(void* sys);
typedef int (__stdcall* F_ChanVol)(void* chan, float vol);
static struct { F_SysCreate create; F_SysInit init; F_CreateSound createSound; F_PlaySound play; F_Update update; F_ChanVol vol; } fm{};
static void* g_fmod = nullptr;
static std::unordered_map<std::string, void*> g_sounds;
static std::string g_soundRoot;
static float g_masterVol = 1.0f;

static bool FmodInit() {
	if (g_fmod) return true;
	HMODULE h = GetModuleHandleA("fmod.dll");
	if (!h) { Log("fmod.dll not loaded"); return false; }
	fm.create = (F_SysCreate)GetProcAddress(h, "FMOD_System_Create");
	fm.init = (F_SysInit)GetProcAddress(h, "FMOD_System_Init");
	fm.createSound = (F_CreateSound)GetProcAddress(h, "FMOD_System_CreateSound");
	fm.play = (F_PlaySound)GetProcAddress(h, "FMOD_System_PlaySound");
	fm.update = (F_Update)GetProcAddress(h, "FMOD_System_Update");
	fm.vol = (F_ChanVol)GetProcAddress(h, "FMOD_Channel_SetVolume");
	if (!fm.create || !fm.init || !fm.createSound || !fm.play || !fm.update || !fm.vol) { Log("fmod exports missing"); return false; }
	void* sys = nullptr;
	int r1 = fm.create(&sys, 0x00020221);
	int r2 = sys ? fm.init(sys, 64, 0, nullptr) : -1;
	Log("FMOD system create %d init %d", r1, r2);
	if (r1 != 0 || r2 != 0) return false;
	g_fmod = sys;
	return true;
}

static void PlaySoundFile(const std::string& rel, float vol) {
	if (!FmodInit()) return;
	void*& snd = g_sounds[rel];
	if (!snd) {
		std::string path = g_soundRoot + rel;
		int r = fm.createSound(g_fmod, path.c_str(), 0x00000001 | 0x00000008 | 0x00000100, nullptr, &snd);   // LOOP_OFF|2D|CREATESAMPLE
		if (r != 0) { Log("sound load failed %d: %s", r, path.c_str()); snd = nullptr; g_sounds.erase(rel); return; }
	}
	void* ch = nullptr;
	if (fm.play(g_fmod, snd, nullptr, 0, &ch) == 0 && ch) fm.vol(ch, vol * g_masterVol);
}

// ---- commands from Lua: System.LogAlways("[DN] <cmd> ...")
static void Command(const char* line) {
	char cmd[32] = {};
	if (sscanf_s(line, "%31s", cmd, (unsigned)sizeof(cmd)) != 1) return;
	const char* rest = line + strlen(cmd);
	while (*rest == ' ') rest++;
	if (!strcmp(cmd, "hello")) { g_pendingConsole = "deagle_native_ok"; Log("hello from Lua"); }
	else if (!strcmp(cmd, "vm")) {
		unsigned long long id = 0;
		const char* hex = strstr(rest, "0x"); hex = hex ? hex + 2 : rest;
		if (sscanf_s(hex, "%llx", &id) == 1) { g_vmId = (uint32_t)id; Log("viewmodel entity %08X", g_vmId); }
	}
	else if (!strcmp(cmd, "follow")) { g_follow = atoi(rest) != 0; Log("follow %d", (int)g_follow); }
	else if (!strcmp(cmd, "off")) sscanf_s(rest, "%f %f %f", &g_off[0], &g_off[1], &g_off[2]);
	else if (!strcmp(cmd, "snd")) {
		char file[260] = {}; float v = 1.0f;
		if (sscanf_s(rest, "%259s %f", file, (unsigned)sizeof(file), &v) >= 1) PlaySoundFile(file, v);
	}
	else if (!strcmp(cmd, "vol")) g_masterVol = (float)atof(rest);
}

// LogAlways Lua handler: int (this, IFunctionHandler* pH); string via GetParam<const char*>(pH, 1, &s)
using LogHandlerFn = int(__fastcall*)(void* self, uintptr_t pH);
using GetParamStrFn = bool(__fastcall*)(uintptr_t pH, int idx, const char** out);
static LogHandlerFn g_origLog = nullptr;
static GetParamStrFn g_getParamStr = nullptr;
static uint64_t g_cmds = 0;

static const char* LuaString(uintptr_t pH) {
	__try { const char* s = nullptr; return g_getParamStr && g_getParamStr(pH, 1, &s) ? s : nullptr; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

static int __fastcall HkLog(void* self, uintptr_t pH) {
	const char* s = LuaString(pH);
	if (s && s[0] == '[' && s[1] == 'D' && s[2] == 'N' && s[3] == ']') {
		g_cmds++;
		Command(s + 4 + (s[4] == ' ' ? 1 : 0));
		if (auto end = (int(__fastcall*)(uintptr_t))VFn(pH, 0x58)) return end(pH);   // pH->EndFunction()
	}
	return g_origLog(self, pH);
}

static bool IsCView(uintptr_t vt) {
	if (g_cviewVtbl) return vt == g_cviewVtbl;
	if (RttiName(g_game, vt) == ".?AVCView@@") { g_cviewVtbl = vt; Log("CView vtable %p", (void*)vt); return true; }
	return false;
}

static bool CopyCamera(uintptr_t camera, float* out12, float* fov) {
	__try { memcpy(out12, (void*)camera, 12 * sizeof(float)); *fov = *(float*)(camera + 0x30); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

static void OnCamera(uintptr_t camera) {
	uintptr_t vt;
	if (!ReadPtr(camera - 0xE8, vt) || !IsCView(vt)) return;
	float fov = 0;
	if (!CopyCamera(camera, g_cam, &fov)) return;
	uint64_t n = ++g_viewFrames;
	if (g_follow && g_vmId && g_env && SafePlace(g_cam)) g_placed++;
	if (g_fmod) fm.update(g_fmod);
	if (!g_pendingConsole.empty() && g_env) { std::string c; c.swap(g_pendingConsole); SafeConsole(c.c_str()); }
	if (n == 1 || n % 3000 == 0)
		Log("frames %llu placed %llu cmds %llu sounds %zu", n, g_placed, g_cmds, g_sounds.size());
	if (n == 1 || n % 600 == 0)
		Log("view frame %llu cam pos %.3f %.3f %.3f fwd %.3f %.3f %.3f fov %.3f", n, g_cam[3], g_cam[7], g_cam[11],
			g_cam[1], g_cam[5], g_cam[9], fov);
}

static uintptr_t __fastcall HkFrustum(uintptr_t camera) {
	uintptr_t r = g_origFrustum(camera);
	OnCamera(camera);
	return r;
}

static uintptr_t FindFrustum() {
	struct { const char* pat; int entry; } pats[] = {
		{ "48 8B C4 55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 ?? 48 81 EC ?? ?? 00 00 F3 0F 10 09 48 8B D9", 0 },
		{ "55 53 56 57 41 54 41 55 41 56 41 57 48 8D 68 ?? 48 81 EC ?? ?? 00 00 F3 0F 10 09 48 8B D9 F3 0F 10 59 08", -3 },
		{ "F3 0F 10 09 48 8B D9 F3 0F 10 59 08 F3 0F 10 51 10 F3 0F 10 41 24 F3 0F 10 61 28", -0x1A },
	};
	for (auto& p : pats) {
		if (uintptr_t a = FindUnique(g_game, p.pat)) return a + p.entry;
	}
	return 0;
}

// ---- gEnv: two RIP-relative patterns; accept the candidate whose +0x108 renderer has the D3D renderer RTTI
static bool ValidEnv(uintptr_t env) {
	uintptr_t rnd, vt;
	if (!ReadPtr(env + 0x108, rnd) || !rnd || !ReadPtr(rnd, vt)) return false;
	return RttiName(g_game, vt).find("Renderer") != std::string::npos;
}

static uintptr_t FindEnv() {
	uintptr_t cands[4] = {};
	if (uintptr_t a = FindUnique(g_game, "48 8D 05 ?? ?? ?? ?? 48 89 0F 4C 8D 67 28 48 8D 0D ?? ?? ?? ?? 48 89 47 20 48 89 4F 08"))
		cands[0] = RipTarget(a, 3, 7);
	if (uintptr_t b = FindUnique(g_game, "48 8B 51 08 48 8B C8 FF D2 48 8B 0D ?? ?? ?? ?? 48 85 C9"))
		cands[1] = RipTarget(b + 9, 3, 7);
	for (uintptr_t c : cands) {
		if (!c) continue;
		if (ValidEnv(c)) return c;                                   // the struct itself
		uintptr_t d = 0;
		if (ReadPtr(c, d) && d && ValidEnv(d)) return d;                              // a pointer to it
	}
	return 0;
}

static DWORD WINAPI Init(LPVOID) {
	Log("deagle_native v1 loaded");
	for (int i = 0; i < 600 && !GetModuleHandleA("WHGame.dll"); i++) Sleep(100);
	g_game = GetModule("WHGame.dll");
	if (!g_game.base) { Log("WHGame.dll not found"); return 0; }
	Log("WHGame.dll at %p size %zu", (void*)g_game.base, g_game.size);
	if (MH_Initialize() != MH_OK) { Log("MinHook init failed"); return 0; }
	uintptr_t fr = FindFrustum();
	Log("UpdateFrustumPlanes %p", (void*)fr);
	if (fr && MH_CreateHook((void*)fr, (void*)&HkFrustum, (void**)&g_origFrustum) == MH_OK && MH_EnableHook((void*)fr) == MH_OK)
		Log("camera hook installed");
	else
		Log("camera hook FAILED");
	{   // sound root: <game>\Mods\deagle\sounds\  (exe is in <game>\Bin\Win64MasterMasterSteamPGO)
		char exe[MAX_PATH] = {}; GetModuleFileNameA(nullptr, exe, MAX_PATH);
		std::string p(exe); for (int i = 0; i < 3; i++) p = p.substr(0, p.find_last_of('\\'));
		g_soundRoot = p + "\\Mods\\deagle\\sounds\\";
		Log("sound root %s", g_soundRoot.c_str());
	}
	uintptr_t gp = FindUnique(g_game, "48 89 5C 24 08 57 48 83 EC ?? 48 8B 01 49 8B F8 4C 8D 44 24 20 C7 44 24 20 05 00 00 00 FF 50 38 32 DB 84 C0 74 11");
	uintptr_t lh = FindUnique(g_game, "40 53 48 83 EC ?? 48 8B 02 48 8B CA 48 8B DA FF 50 28 BA 01 00 00 00 48 8B CB 3B C2 74 27 48 8B 03 FF 50 28 BA 02 00 00 00 4C 8D 0D ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 89 44 24 20 8D 4A 05 E8 ?? ?? ?? ?? EB 35");
	g_getParamStr = (GetParamStrFn)gp;
	Log("GetParam<str> %p  LogAlways handler %p", (void*)gp, (void*)lh);
	if (gp && lh && MH_CreateHook((void*)lh, (void*)&HkLog, (void**)&g_origLog) == MH_OK && MH_EnableHook((void*)lh) == MH_OK)
		Log("Lua channel hook installed");
	else
		Log("Lua channel hook FAILED");
	for (int i = 0; i < 240 && !g_env; i++) { g_env = FindEnv(); if (!g_env) Sleep(500); }
	Log("gEnv %p", (void*)g_env);
	if (g_env) {
		static const struct { int off; const char* what; } members[] = {
			{ 0x08, "3DEngine" }, { 0x30, "PhysicalWorld" }, { 0x68, "ParticleManager" }, { 0x80, "Timer" },
			{ 0x90, "Game" }, { 0xA0, "EntitySystem" }, { 0xC0, "System" }, { 0x108, "Renderer" } };
		for (auto& mb : members) {
			uintptr_t p = 0, vt = 0;
			ReadPtr(g_env + mb.off, p);
			if (p) ReadPtr(p, vt);
			Log("  gEnv+0x%03X %-15s %p %s", mb.off, mb.what, (void*)p, vt ? RttiName(g_game, vt).c_str() : "");
		}
	}
	return 0;
}

BOOL APIENTRY DllMain(HMODULE h, DWORD reason, LPVOID) {
	if (reason == DLL_PROCESS_ATTACH) {
		g_self = h;
		DisableThreadLibraryCalls(h);
		LogInit(h);
		CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
	}
	return TRUE;
}
