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
// the muzzle flash rides on the gun: a small entity carrying the emitter, put on the muzzle in the same step as the
// viewmodel (Lua sends the muzzle's position in the viewmodel's own axes every frame while the flash burns)
static uint32_t g_flashId = 0;
static float g_flashLocal[3] = {};
static bool g_flashOn = false;
static bool g_follow = false;
static float g_off[3] = { 0, 0, 0 };            // right, forward, up (m)
static uint64_t g_placed = 0;
static std::string g_pendingConsole;           // console command to run next frame (native -> Lua)

// ---- procedural motion (camera space: x right, y forward, z up), from the camera's own movement
// Springs step implicitly (backward Euler): stable at any frame time. The explicit step it replaced blew up once
// dt > 2 / sqrt(k) (a 70 ms hitch for the stiff shot kick, k = 784): the gun's transform went NaN and it vanished.
struct Spring {
	float x = 0, v = 0;
	void step(float target, float k, float c, float dt) {
		v = (v - dt * k * (x - target)) / (1.0f + dt * c + dt * dt * k);
		x += v * dt;
	}
};
static struct {
	bool enabled = true, aiming = false;
	float scale = 1.0f;                 // global multiplier
	LARGE_INTEGER lastT{}; double freq = 0;
	float lastPos[3]{}, lastFwd[3]{}; bool have = false;
	float speed = 0, vz = 0, phase = 0, sprint = 0, airTime = 0;
	Spring yaw, pitch, land, aimK;
	Spring kickBack, kickPitch, kickYaw;   // the shot's push on the gun model (Lua sends it from the cartridge physics)
	float off[3]{}, rot[3]{};           // results: offset (m), rotation (deg: pitch about x, roll about y, yaw about z)
} mo;

static void UpdateMotion(const float* cam) {
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	if (!mo.freq) { LARGE_INTEGER f; QueryPerformanceFrequency(&f); mo.freq = (double)f.QuadPart; }
	float pos[3] = { cam[3], cam[7], cam[11] }, fwd[3] = { cam[1], cam[5], cam[9] };
	float dt = mo.lastT.QuadPart ? (float)((now.QuadPart - mo.lastT.QuadPart) / mo.freq) : 0.016f;
	mo.lastT = now;
	if (!mo.have || dt <= 0 || dt > 0.25f) { memcpy(mo.lastPos, pos, sizeof(pos)); memcpy(mo.lastFwd, fwd, sizeof(fwd)); mo.have = true; return; }
	float dx = pos[0] - mo.lastPos[0], dy = pos[1] - mo.lastPos[1], dz = pos[2] - mo.lastPos[2];
	float hs = sqrtf(dx * dx + dy * dy) / dt, vz = dz / dt;
	if (hs > 15) hs = 0;                                         // teleports / cutscene cuts
	mo.speed += (hs - mo.speed) * (1 - expf(-dt / 0.15f));
	// turn rates (deg/s) from the forward vector change
	float yawNow = atan2f(fwd[1], fwd[0]), yawLast = atan2f(mo.lastFwd[1], mo.lastFwd[0]);
	float dyaw = yawNow - yawLast; if (dyaw > 3.14159f) dyaw -= 6.28318f; if (dyaw < -3.14159f) dyaw += 6.28318f;
	float dpitch = asinf(fmaxf(-1, fminf(1, fwd[2]))) - asinf(fmaxf(-1, fminf(1, mo.lastFwd[2])));
	float yawRate = dyaw / dt * 57.3f, pitchRate = dpitch / dt * 57.3f;
	memcpy(mo.lastPos, pos, sizeof(pos)); memcpy(mo.lastFwd, fwd, sizeof(fwd));
	mo.aimK.step(mo.aiming ? 1.0f : 0.0f, 200, 28, dt);
	float hip = 1.0f - fminf(1.0f, fmaxf(0.0f, mo.aimK.x)) * 0.85f;   // most motion fades while aiming
	// sway: the gun lags behind turns (spring toward an offset proportional to turn rate)
	mo.yaw.step(fmaxf(-2.5f, fminf(2.5f, -yawRate * 0.012f)), 120, 18, dt);
	mo.pitch.step(fmaxf(-2.0f, fminf(2.0f, -pitchRate * 0.010f)), 120, 18, dt);
	// bob: phase advances with distance walked (one step ~0.75 m)
	mo.phase += hs * dt / 0.75f * 3.14159f;
	float a = fminf(1.0f, mo.speed / 5.0f);
	float bobX = sinf(mo.phase) * 0.006f * a, bobZ = -fabsf(cosf(mo.phase)) * 0.005f * a;
	// sprint: lowered + tilted when moving fast
	float sprintT = mo.speed > 4.6f && !mo.aiming ? 1.0f : 0.0f;
	mo.sprint += (sprintT - mo.sprint) * (1 - expf(-dt / 0.2f));
	// landing: falling then stopping kicks the gun down a little
	if (vz < -2.5f) mo.airTime += dt; else { if (mo.airTime > 0.25f) mo.land.v -= fminf(0.25f, -mo.vz * 0.03f); mo.airTime = 0; }
	mo.vz = vz;
	mo.land.step(0, 160, 16, dt);
	// shot kick: slightly underdamped springs, so the gun snaps back and settles
	mo.kickBack.step(0, 784, 50, dt); mo.kickPitch.step(0, 784, 50, dt); mo.kickYaw.step(0, 784, 50, dt);
	float k = mo.enabled ? mo.scale : 0.0f;
	mo.off[0] = k * (bobX * hip + 0.015f * mo.sprint);
	mo.off[1] = k * (-0.01f * mo.sprint) - mo.kickBack.x;
	mo.off[2] = k * (bobZ * hip - 0.03f * mo.sprint + mo.land.x * 0.15f);
	mo.rot[0] = k * (mo.pitch.x * hip - 14.0f * mo.sprint) + mo.kickPitch.x;  // pitch (x)
	mo.rot[1] = k * (mo.yaw.x * 0.5f * hip + 12.0f * mo.sprint + sinf(mo.phase) * 0.6f * a * hip);   // roll (y)
	mo.rot[2] = k * (mo.yaw.x * hip + 6.0f * mo.sprint) + mo.kickYaw.x;     // yaw (z)
}

static void MulRot(const float* a, const float* b, float* out) {   // 3x3, row-major, out = a*b
	for (int r = 0; r < 3; r++) for (int c = 0; c < 3; c++) out[r * 3 + c] = a[r * 3] * b[c] + a[r * 3 + 1] * b[3 + c] + a[r * 3 + 2] * b[6 + c];
}
static void LocalRot(const float* deg, float* R) {   // R = Rz(yaw) * Rx(pitch) * Ry(roll), camera-local axes
	float p = deg[0] / 57.3f, r = deg[1] / 57.3f, y = deg[2] / 57.3f;
	float Rz[9] = { cosf(y), -sinf(y), 0, sinf(y), cosf(y), 0, 0, 0, 1 };
	float Rx[9] = { 1, 0, 0, 0, cosf(p), -sinf(p), 0, sinf(p), cosf(p) };
	float Ry[9] = { cosf(r), 0, sinf(r), 0, 1, 0, -sinf(r), 0, cosf(r) };
	float t[9]; MulRot(Rz, Rx, t); MulRot(t, Ry, R);
}

static bool Finite(const float* f, int n) { for (int i = 0; i < n; i++) if (!std::isfinite(f[i])) return false; return true; }
static void ResetMotion() { mo.yaw = mo.pitch = mo.land = mo.aimK = mo.kickBack = mo.kickPitch = mo.kickYaw = Spring{}; memset(mo.off, 0, sizeof(mo.off)); memset(mo.rot, 0, sizeof(mo.rot)); }

static bool PlaceViewmodel(const float* cam) {
	uintptr_t es; if (!ReadPtr(g_env + 0xA0, es) || !es) return false;
	auto getEntity = (GetEntityFn)VFn(es, 0x70);
	if (!getEntity) return false;
	uintptr_t e = getEntity(es, g_vmId);
	if (!e) return false;
	auto setTM = (SetWorldTMFn)VFn(e, 0xF0);
	if (!setTM) return false;
	float C[9] = { cam[0], cam[1], cam[2], cam[4], cam[5], cam[6], cam[8], cam[9], cam[10] }, L[9], R[9];
	LocalRot(mo.rot, L);
	MulRot(C, L, R);
	float o[3] = { g_off[0] + mo.off[0], g_off[1] + mo.off[1], g_off[2] + mo.off[2] };
	float tm[12];
	for (int r = 0; r < 3; r++) {
		tm[r * 4 + 0] = R[r * 3 + 0]; tm[r * 4 + 1] = R[r * 3 + 1]; tm[r * 4 + 2] = R[r * 3 + 2];
		tm[r * 4 + 3] = cam[r * 4 + 3] + C[r * 3 + 0] * o[0] + C[r * 3 + 1] * o[1] + C[r * 3 + 2] * o[2];
	}
	if (!Finite(tm, 12)) {          // never hand the engine a broken transform (the gun would vanish for good)
		static bool said = false; if (!said) { Log("motion went non-finite: reset"); said = true; }
		ResetMotion();
		return false;
	}
	setTM(e, tm, 0);
	if (g_flashOn && g_flashId) {
		uintptr_t f = getEntity(es, g_flashId);
		auto fset = f ? (SetWorldTMFn)VFn(f, 0xF0) : nullptr;
		if (fset) {
			float ft[12];
			for (int r = 0; r < 3; r++) {
				ft[r * 4 + 0] = tm[r * 4 + 0]; ft[r * 4 + 1] = tm[r * 4 + 1]; ft[r * 4 + 2] = tm[r * 4 + 2];
				ft[r * 4 + 3] = tm[r * 4 + 3] + tm[r * 4 + 0] * g_flashLocal[0] + tm[r * 4 + 1] * g_flashLocal[1] + tm[r * 4 + 2] * g_flashLocal[2];
			}
			fset(f, ft, 0);
		}
	}
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
	if (!strcmp(cmd, "hello")) {   // every save load / script start: forget the old viewmodel (its id is gone)
		g_follow = false; g_vmId = 0; mo.aiming = false; g_flashOn = false; g_flashId = 0;
		g_pendingConsole = "deagle_native_ok"; Log("hello from Lua");
	}
	else if (!strcmp(cmd, "vm")) {
		unsigned long long id = 0;
		const char* hex = strstr(rest, "0x"); hex = hex ? hex + 2 : rest;
		if (sscanf_s(hex, "%llx", &id) == 1) { g_vmId = (uint32_t)id; Log("viewmodel entity %08X", g_vmId); }
	}
	else if (!strcmp(cmd, "follow")) { g_follow = atoi(rest) != 0; Log("follow %d", (int)g_follow); }
	else if (!strcmp(cmd, "flash")) {    // flash 0x<entity> <x> <y> <z>: the flash entity, the muzzle in viewmodel axes
		unsigned long long id = 0;
		const char* hex = strstr(rest, "0x"); hex = hex ? hex + 2 : rest;
		const char* xyz = strchr(hex, ' ');
		if (sscanf_s(hex, "%llx", &id) == 1 && xyz && sscanf_s(xyz, "%f %f %f", &g_flashLocal[0], &g_flashLocal[1], &g_flashLocal[2]) == 3) {
			g_flashId = (uint32_t)id; g_flashOn = true;
		}
	}
	else if (!strcmp(cmd, "kick")) {     // kick <back m> <pitch deg> <yaw deg>: peaks of the shot's push on the gun
		float b = 0, pt = 0, yw = 0;
		if (sscanf_s(rest, "%f %f %f", &b, &pt, &yw) == 3) {
			const float w = 28.0f * 2.71828f;   // velocity that peaks at the given displacement (spring w = 28 rad/s)
			mo.kickBack.v += b * w; mo.kickPitch.v += pt * w; mo.kickYaw.v += yw * w;
		}
	}
	else if (!strcmp(cmd, "flashpos")) sscanf_s(rest, "%f %f %f", &g_flashLocal[0], &g_flashLocal[1], &g_flashLocal[2]);
	else if (!strcmp(cmd, "flashoff")) g_flashOn = false;
	else if (!strcmp(cmd, "off")) sscanf_s(rest, "%f %f %f", &g_off[0], &g_off[1], &g_off[2]);
	else if (!strcmp(cmd, "snd")) {
		char file[260] = {}; float v = 1.0f;
		if (sscanf_s(rest, "%259s %f", file, (unsigned)sizeof(file), &v) >= 1) PlaySoundFile(file, v);
	}
	else if (!strcmp(cmd, "vol")) g_masterVol = (float)atof(rest);
	else if (!strcmp(cmd, "aim")) mo.aiming = atoi(rest) != 0;
	else if (!strcmp(cmd, "motion")) { float v = (float)atof(rest); mo.enabled = v > 0; if (v > 0) mo.scale = v; Log("motion %.2f", v); }
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
	UpdateMotion(g_cam);
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
	Log("deagle_native v1.1 loaded");
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
