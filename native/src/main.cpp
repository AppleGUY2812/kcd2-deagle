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
#include "motion.h"
#include "gore.h"
#include "settings.h"
#include "freecam.h"
#include "ron.h"
#include <deque>
#include <mutex>
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
// The muzzle flash: the plugin spawns the emitter itself, the way Particle.SpawnEffect does (FindEffect + Spawn: a free
// emitter, the only kind that drew every time in game), holds a reference to it, and moves it onto the muzzle right
// after placing the gun in every camera step. Lua sends the muzzle (position + barrel direction in the viewmodel's
// axes) at the shot and every frame while the flash burns, then "flashoff". Effects on entities (bone attachments,
// viewmodel slots, a helper entity) drew on some shots only (tested in game 2026-10-12).
// WHGame 1.5.6: IParticleManager (gEnv+0x68) FindEffect +0x30; IParticleEffect Spawn(const QuatTS*, SpawnParams*) +0x30
// (from ScriptBind_Particle::SpawnEffect); IParticleEmitter SetLocation(const QuatTS*) +0x2B0 (stores at +0x70);
// reference count at emitter+0x58 (Spawn's lock inc), its CMultiThreadRefCount base at +0x50, DeleteThis = vtable +0x08.
using FindEffectFn = uintptr_t(__fastcall*)(uintptr_t pm, const char* name, const char* source, bool load);
using SpawnFn = uintptr_t(__fastcall*)(uintptr_t effect, const float* loc, const void* spawnParams);
using SetLocationFn = void(__fastcall*)(uintptr_t emitter, const float* loc);
using DeleteThisFn = void(__fastcall*)(uintptr_t refBase);
struct Flash { uintptr_t emitter; float scale; LARGE_INTEGER born; uint64_t firstFrame; };
// A real muzzle flash is over in a few milliseconds; games show it for a few frames. Its particles live >= 0.08 s
// (a new emitter is drawn a frame or two after it's made, MODLOG), so at a high frame rate they were seen growing for
// 4-6 frames: too slow (user 2026-10-12: "the flash can grow, it just needed to be snappier"). The plugin sets its size
// for each frame it's drawn, counted from the first one (bounding box valid at the camera step = drawn this frame):
// g_flashGrow[i] x its scale on drawn frame i, then nothing. The same quick bloom at any frame rate. Lua sends the
// sizes (cfg.flashGrow); none = the particles' own life.
static float g_flashGrow[8] = {};
static int g_flashGrowN = 0;   // off until Lua sends sizes (counting from the first valid box hid the flash before it was drawn)
static Flash g_flashes[8];
static int g_nFlashes = 0;
static float g_flashPos[3] = {}, g_flashDir[3] = { 0, 1, 0 };
static float g_vmTM[12]; static bool g_haveVmTM = false;
// Moving an emitter empties its bounding box (SetLocation resets the AABB at +0x1A8..+0x1BC to +-1e15) until the
// particle update rebuilds it. KCD2 updates particles BEFORE this camera step (in game 2026-10-12: Lua -> particle
// update -> camera step x2 -> render; logged per frame), so a flash moved here was drawn with an empty box and culled
// every frame. The box it had is put back, moved along with the emitter: drawn 5 / 5 shots and stayed on the muzzle
// while turning (recorded); never moved = left behind in the world, moved without the box = 1 / 5 drawn.
static bool BoxValid(uintptr_t em) { float x = 0; __try { x = *(float*)(em + 0x1A8); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } return x < 1e14f; }
static bool ReadBox(uintptr_t em, float* box) { __try { memcpy(box, (void*)(em + 0x1A8), 24); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
static void WriteBox(uintptr_t em, const float* box) { __try { memcpy((void*)(em + 0x1A8), box, 24); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static bool g_follow = false;
static float g_off[3] = { 0, 0, 0 };            // right, forward, up (m)
static uint64_t g_placed = 0, g_repeats = 0;
static std::deque<std::string> g_console;      // console commands to run next frame (native -> Lua), in order
static std::mutex g_consoleMu;

// ---- procedural motion: motion.cpp / motion.h (mo, UpdateMotion, ResetMotion)

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

// world location of the muzzle on the gun placed last (vm), as CryEngine's QuatTS {q.xyz, q.w, t.xyz, s}: forward (y)
// down the barrel, no roll (x = y x the gun's up), like Matrix33::CreateRotationVDir
static bool MuzzleLoc(const float* vm, float scale, float* loc) {
	float y[3] = { g_flashDir[0], g_flashDir[1], g_flashDir[2] };
	float ly = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	if (ly < 1e-6f) return false;
	for (float& v : y) v /= ly;
	float x[3] = { y[1], -y[0], 0 };
	float lx = sqrtf(x[0] * x[0] + x[1] * x[1]);
	if (lx < 1e-6f) { x[0] = 1; x[1] = 0; lx = 1; }
	x[0] /= lx; x[1] /= lx;
	float z[3] = { x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0] };
	float m[3][3], t[3];
	for (int r = 0; r < 3; r++) {
		const float* row = vm + r * 4;
		m[r][0] = row[0] * x[0] + row[1] * x[1] + row[2] * x[2];
		m[r][1] = row[0] * y[0] + row[1] * y[1] + row[2] * y[2];
		m[r][2] = row[0] * z[0] + row[1] * z[1] + row[2] * z[2];
		t[r] = row[3] + row[0] * g_flashPos[0] + row[1] * g_flashPos[1] + row[2] * g_flashPos[2];
	}
	float qw, qx, qy, qz, tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0) { float k = sqrtf(tr + 1) * 2; qw = 0.25f * k; qx = (m[2][1] - m[1][2]) / k; qy = (m[0][2] - m[2][0]) / k; qz = (m[1][0] - m[0][1]) / k; }
	else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) { float k = sqrtf(1 + m[0][0] - m[1][1] - m[2][2]) * 2; qw = (m[2][1] - m[1][2]) / k; qx = 0.25f * k; qy = (m[0][1] + m[1][0]) / k; qz = (m[0][2] + m[2][0]) / k; }
	else if (m[1][1] > m[2][2]) { float k = sqrtf(1 + m[1][1] - m[0][0] - m[2][2]) * 2; qw = (m[0][2] - m[2][0]) / k; qx = (m[0][1] + m[1][0]) / k; qy = 0.25f * k; qz = (m[1][2] + m[2][1]) / k; }
	else { float k = sqrtf(1 + m[2][2] - m[0][0] - m[1][1]) * 2; qw = (m[1][0] - m[0][1]) / k; qx = (m[0][2] + m[2][0]) / k; qy = (m[1][2] + m[2][1]) / k; qz = 0.25f * k; }
	float out[8] = { qx, qy, qz, qw, t[0], t[1], t[2], scale };
	if (!Finite(out, 8)) return false;
	memcpy(loc, out, sizeof(out));
	return true;
}

static uintptr_t g_lastEffect = 0; static LONG g_lastRefs = 0;
static uintptr_t SpawnFlash(const char* effect, const float* loc) {
	uintptr_t pm; if (!ReadPtr(g_env + 0x68, pm) || !pm) return 0;
	auto find = (FindEffectFn)VFn(pm, 0x30);
	uintptr_t eff = find ? find(pm, effect, "deagle_native", true) : 0;
	g_lastEffect = eff;
	if (!eff) return (uintptr_t)-1;
	auto spawn = (SpawnFn)VFn(eff, 0x30);
	uintptr_t em = spawn ? spawn(eff, loc, nullptr) : 0;
	if (em) g_lastRefs = InterlockedIncrement((volatile LONG*)(em + 0x58));   // ours until flashoff: the pointer stays valid
	return em;
}
static uintptr_t SafeSpawnFlash(const char* effect, const float* loc) {
	__try { return SpawnFlash(effect, loc); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static void ReleaseEmitter(uintptr_t em) {
	if (InterlockedDecrement((volatile LONG*)(em + 0x58)) == 0)
		if (auto del = (DeleteThisFn)VFn(em + 0x50, 0x08)) del(em + 0x50);
}
static void SafeRelease(uintptr_t em) { __try { ReleaseEmitter(em); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
static void SafeSetLocation(uintptr_t em, const float* loc) {
	__try { if (auto set = (SetLocationFn)VFn(em, 0x2B0)) set(em, loc); } __except (EXCEPTION_EXECUTE_HANDLER) {}
}
// a world location as CryEngine's QuatTS {q.xyz, q.w, t.xyz, s} with +y along dir (no roll), like MuzzleLoc
static bool LocFromDir(const float* pos, const float* dir, float scale, float* loc) {
	float y[3] = { dir[0], dir[1], dir[2] };
	float ly = sqrtf(y[0] * y[0] + y[1] * y[1] + y[2] * y[2]);
	if (ly < 1e-6f) return false;
	for (float& v : y) v /= ly;
	float x[3] = { y[1], -y[0], 0 };
	float lx = sqrtf(x[0] * x[0] + x[1] * x[1]);
	if (lx < 1e-6f) { x[0] = 1; x[1] = 0; lx = 1; }
	x[0] /= lx; x[1] /= lx;
	float z[3] = { x[1] * y[2] - x[2] * y[1], x[2] * y[0] - x[0] * y[2], x[0] * y[1] - x[1] * y[0] };
	float m[3][3] = { { x[0], y[0], z[0] }, { x[1], y[1], z[1] }, { x[2], y[2], z[2] } };
	float qw, qx, qy, qz, tr = m[0][0] + m[1][1] + m[2][2];
	if (tr > 0) { float k = sqrtf(tr + 1) * 2; qw = 0.25f * k; qx = (m[2][1] - m[1][2]) / k; qy = (m[0][2] - m[2][0]) / k; qz = (m[1][0] - m[0][1]) / k; }
	else if (m[0][0] > m[1][1] && m[0][0] > m[2][2]) { float k = sqrtf(1 + m[0][0] - m[1][1] - m[2][2]) * 2; qw = (m[2][1] - m[1][2]) / k; qx = 0.25f * k; qy = (m[0][1] + m[1][0]) / k; qz = (m[0][2] + m[2][0]) / k; }
	else if (m[1][1] > m[2][2]) { float k = sqrtf(1 + m[1][1] - m[0][0] - m[2][2]) * 2; qw = (m[0][2] - m[2][0]) / k; qx = (m[0][1] + m[1][0]) / k; qy = 0.25f * k; qz = (m[1][2] + m[2][1]) / k; }
	else { float k = sqrtf(1 + m[2][2] - m[0][0] - m[1][1]) * 2; qw = (m[1][0] - m[0][1]) / k; qx = (m[0][2] + m[2][0]) / k; qy = (m[1][2] + m[2][1]) / k; qz = 0.25f * k; }
	float out[8] = { qx, qy, qz, qw, pos[0], pos[1], pos[2], scale };
	if (!Finite(out, 8)) return false;
	memcpy(loc, out, sizeof(out));
	return true;
}

static bool g_fxsSeed = false;   // fxsseed 1: write the box at spawn too (with it, 0.4 s pieces didn't show either)
// pieces spawned by fxs (tracers, impacts): held 2 s of game time, then let go (oldest first; a full ring lets the
// oldest go early). Game time so F9 slow motion (x0.1) keeps them 20 s: 2 s real cut a 0.75 s splash at 0.2 s.
struct HeldPiece { uintptr_t em; double born; float loc[8]; int touches; };
static HeldPiece g_held[1024]; static int g_heldHead = 0, g_heldN = 0;
static float g_gameSpeed = 1.0f;   // speed <x>: the game's speed (t_Scale)
static double g_pieceClock = 0;    // game seconds: wall time x g_gameSpeed, advanced every camera step (steps cut at 0.1 s)
static void HoldPiece(uintptr_t em, const float* loc) {
	if (g_heldN == 1024) { SafeRelease(g_held[g_heldHead].em); g_heldHead = (g_heldHead + 1) % 1024; g_heldN--; }
	int i = (g_heldHead + g_heldN) % 1024;
	g_held[i].em = em; g_held[i].born = g_pieceClock; memcpy(g_held[i].loc, loc, sizeof(g_held[i].loc));
	g_held[i].touches = 3; g_heldN++;
}
// every camera step: set each new piece's location again (the same place) for its first frames, the way a flash is
// moved onto the muzzle every frame; a moved emitter is drawn a frame after its spawn (the flash), a new one left alone
// isn't drawn for ~0.3 s (pieces living under 0.4 s never showed, in game 2026-10-12)
static void TouchPieces() {
	for (int j = 0; j < g_heldN; j++) {
		HeldPiece& h = g_held[(g_heldHead + j) % 1024];
		if (h.touches <= 0) continue;
		h.touches--;
		float box[6]; bool had = BoxValid(h.em) && ReadBox(h.em, box);
		SafeSetLocation(h.em, h.loc);
		if (had) WriteBox(h.em, box);
	}
}
static void ReleaseOldPieces(bool all) {
	static LARGE_INTEGER last{};
	LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
	if (last.QuadPart) { double dt = double(now.QuadPart - last.QuadPart) / f.QuadPart; g_pieceClock += (dt < 0.1 ? dt : 0.1) * g_gameSpeed; }
	last = now;
	while (g_heldN > 0 && (all || g_pieceClock - g_held[g_heldHead].born > 2.0)) {
		SafeRelease(g_held[g_heldHead].em);
		g_heldHead = (g_heldHead + 1) % 1024; g_heldN--;
	}
}

static void ReleaseFlashes() {
	for (int i = 0; i < g_nFlashes; i++) SafeRelease(g_flashes[i].emitter);
	g_nFlashes = 0;
}

// every camera step, after the gun: the burning flashes onto its muzzle. One held over 5 s (Lua never said flashoff)
// is let go.
static void MoveFlashes() {
	LARGE_INTEGER now, f; QueryPerformanceCounter(&now); QueryPerformanceFrequency(&f);
	int keep = 0;
	for (int i = 0; i < g_nFlashes; i++) {
		Flash& fl = g_flashes[i];
		if ((now.QuadPart - fl.born.QuadPart) > 5 * f.QuadPart) { SafeRelease(fl.emitter); continue; }
		if (!fl.firstFrame && BoxValid(fl.emitter)) fl.firstFrame = g_viewFrames;
		float size = fl.scale;
		if (g_flashGrowN > 0 && fl.firstFrame) {
			uint64_t i = g_viewFrames - fl.firstFrame;
			size = i < (uint64_t)g_flashGrowN ? fl.scale * g_flashGrow[i] : 1e-4f;
		}
		float loc[8];
		if (MuzzleLoc(g_vmTM, size, loc)) {
			float box[6]; bool had = BoxValid(fl.emitter) && ReadBox(fl.emitter, box);
			float old[3] = {}; __try { memcpy(old, (void*)(fl.emitter + 0x80), 12); } __except (EXCEPTION_EXECUTE_HANDLER) {}
			SafeSetLocation(fl.emitter, loc);
			if (had) {
				for (int k = 0; k < 3; k++) { box[k] += loc[4 + k] - old[k]; box[3 + k] += loc[4 + k] - old[k]; }
				WriteBox(fl.emitter, box);
			}
		}
		g_flashes[keep++] = fl;
	}
	g_nFlashes = keep;
}

// ---- the live flash (flashMode "live"): one continuous emitter per drawn gun that never stops; its particles are
// always alive (a random tile and turn each). It sits on the muzzle at a size of nothing, and a shot scales it up for
// exactly the frames it should be seen (g_liveSizes: its size on each of them), then back to nothing. KCD2 draws a NEW
// emitter a frame or two late and pre-ages it (MODLOG), so a flash spawned per shot is seen late and for 4-6 frames at
// a high frame rate; this one is already drawn, and resizing it is instant: flashshot runs inside Lua, before the
// particle update and the render of the frame it's fired in. The light stays on a short per-shot emitter (a particle
// light's radius doesn't follow the emitter's size).
// flashseed <m>: a spawned flash gets a box this big around the muzzle at once (0 = off). A new emitter starts with
// an empty box and is culled until its first update computes one, which runs after the frame's culling
// (docs/flash_no_delay_research.md): measured in game 2026-10-12 one game frame late. To test against that.
static float g_flashSeed = 0;
static uintptr_t g_live = 0;
static float g_liveScale = 1, g_liveSizes[8] = {};
static int g_liveN = 0, g_liveFrame = -1;          // frame of the flash being shown, -1 = none
static void LivePlace(float size) {
	if (!g_live || !g_haveVmTM) return;
	float loc[8];
	if (!MuzzleLoc(g_vmTM, size > 0 ? g_liveScale * size : 1e-4f, loc)) return;
	float box[6]; bool had = BoxValid(g_live) && ReadBox(g_live, box);
	float old[3] = {}; __try { memcpy(old, (void*)(g_live + 0x80), 12); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	SafeSetLocation(g_live, loc);
	if (had) {
		for (int k = 0; k < 3; k++) { box[k] += loc[4 + k] - old[k]; box[3 + k] += loc[4 + k] - old[k]; }
		WriteBox(g_live, box);
	}
}
// every camera step, after the gun: the frame's size, then the next frame
static void LiveStep() {
	if (!g_live) return;
	if (g_liveFrame >= 0 && g_liveFrame < g_liveN) { LivePlace(g_liveSizes[g_liveFrame]); g_liveFrame++; }
	else { g_liveFrame = -1; LivePlace(0); }
}

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
	memcpy(g_vmTM, tm, sizeof(tm)); g_haveVmTM = true;
	if (g_nFlashes) MoveFlashes();
	LiveStep();
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
struct FVec { float x, y, z; };
typedef int (__stdcall* F_Listener)(void* sys, int listener, const FVec* pos, const FVec* vel, const FVec* fwd, const FVec* up);
typedef int (__stdcall* F_Chan3D)(void* chan, const FVec* pos, const FVec* vel);
typedef int (__stdcall* F_ChanMinMax)(void* chan, float mn, float mx);
typedef int (__stdcall* F_ChanPaused)(void* chan, int paused);
typedef int (__stdcall* F_MasterGroup)(void* sys, void** group);
typedef int (__stdcall* F_GroupPitch)(void* group, float pitch);
static struct { F_SysCreate create; F_SysInit init; F_CreateSound createSound; F_PlaySound play; F_Update update; F_ChanVol vol;
	F_Listener listener; F_Chan3D chan3d; F_ChanMinMax minmax; F_ChanPaused paused; F_MasterGroup master; F_GroupPitch pitch; } fm{};
static float g_soundSpeed = 1.0f;   // game speed (F9 slow motion): our sounds play at it, like the game's own
static void* g_fmod = nullptr;
static std::unordered_map<std::string, void*> g_sounds;
static std::string g_soundRoot;
static std::string g_ronDir;     // <game>\Mods\deagle\ron\: Ready or Not's FMOD + banks (ron.cpp)
static float g_masterVol = 1.0f;

// every sound of ours, playing or not, at the game's speed (the master channel group's pitch: 0.1 = ten times slower
// and lower, like the game's own sounds in slow motion)
static void SetSoundSpeed(float speed) {
	g_soundSpeed = speed > 0.01f ? speed : 0.01f;
	void* group = nullptr;
	if (g_fmod && fm.master(g_fmod, &group) == 0 && group) fm.pitch(group, g_soundSpeed);
}

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
	fm.listener = (F_Listener)GetProcAddress(h, "FMOD_System_Set3DListenerAttributes");
	fm.chan3d = (F_Chan3D)GetProcAddress(h, "FMOD_Channel_Set3DAttributes");
	fm.minmax = (F_ChanMinMax)GetProcAddress(h, "FMOD_Channel_Set3DMinMaxDistance");
	fm.paused = (F_ChanPaused)GetProcAddress(h, "FMOD_Channel_SetPaused");
	fm.master = (F_MasterGroup)GetProcAddress(h, "FMOD_System_GetMasterChannelGroup");
	fm.pitch = (F_GroupPitch)GetProcAddress(h, "FMOD_ChannelGroup_SetPitch");
	if (!fm.create || !fm.init || !fm.createSound || !fm.play || !fm.update || !fm.vol || !fm.listener || !fm.chan3d || !fm.minmax || !fm.paused
		|| !fm.master || !fm.pitch) {
		Log("fmod exports missing"); return false;
	}
	void* sys = nullptr;
	int r1 = fm.create(&sys, 0x00020221);
	int r2 = sys ? fm.init(sys, 64, 0, nullptr) : -1;
	Log("FMOD system create %d init %d", r1, r2);
	if (r1 != 0 || r2 != 0) return false;
	g_fmod = sys;
	SetSoundSpeed(g_soundSpeed);
	return true;
}

// 3D sounds (bullet impacts): FMOD's listener is the game camera, moved every camera step. CryEngine is right-handed
// with Z up; FMOD defaults to left-handed with Y up and Z forward: swapping y and z converts one into the other.
static FVec ToFmod(float x, float y, float z) { return { x, z, y }; }
static void UpdateListener(const float* cam) {
	if (!g_fmod) return;
	FVec pos = ToFmod(cam[3], cam[7], cam[11]), vel = { 0, 0, 0 };
	FVec fwd = ToFmod(cam[1], cam[5], cam[9]), up = ToFmod(cam[2], cam[6], cam[10]);
	fm.listener(g_fmod, 0, &pos, &vel, &fwd, &up);
}
static void PlaySound3D(const std::string& rel, float vol, const float* at, float minDist, float maxDist) {
	if (!FmodInit()) return;
	void*& snd = g_sounds[rel + "|3d"];
	if (!snd) {
		std::string path = g_soundRoot + rel;
		int r = fm.createSound(g_fmod, path.c_str(), 0x00000001 | 0x00000010 | 0x00000100, nullptr, &snd);   // LOOP_OFF|3D|CREATESAMPLE
		if (r != 0) { Log("sound load failed %d: %s", r, path.c_str()); snd = nullptr; g_sounds.erase(rel + "|3d"); return; }
	}
	{ float fc[12]; UpdateListener(Freecam_Matrix(fc) ? fc : g_cam); }
	void* ch = nullptr;
	if (fm.play(g_fmod, snd, nullptr, 1, &ch) != 0 || !ch) return;    // paused: placed before it makes a sound
	FVec pos = ToFmod(at[0], at[1], at[2]), vel = { 0, 0, 0 };
	fm.chan3d(ch, &pos, &vel);
	fm.minmax(ch, minDist, maxDist);
	fm.vol(ch, vol * g_masterVol);
	fm.paused(ch, 0);
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

// Ready or Not's own engine (ron.cpp), brought up on the first gun sound. Its final fader is 0.5 x the mod's volume:
// RoN's indoor shots go over full scale at 1.0 (measured on the renders), and the mod's gunshot levels were set
// against renders made at 0.5. ReverbMaster runs for good: its 12 snapshots follow the Reverb* globals Lua sets.
static void RonState(bool ok) {
	std::lock_guard<std::mutex> lk(g_consoleMu);
	g_console.push_back(ok ? "deagle_ron_state 1" : "deagle_ron_state 0");
}
static bool RonEnsure() {
	static bool tried = false;
	if (RonReady()) return true;
	if (tried) return false;
	tried = true;
	bool ok = RonInit(g_ronDir);
	if (ok) {
		RonVolume(0.5f * g_masterVol);
		RonSpeed(g_soundSpeed);
		ok = RonHold("event:/Levels/Reverbs/ReverbMaster", true);
		if (!ok) Log("ron: ReverbMaster didn't start");
	}
	RonState(ok);
	return ok;
}
// "<event path>|<name=value ...>": event paths have spaces
static void RonSplit(const char* s, std::string& ev, std::string& params) {
	const char* bar = strchr(s, '|');
	ev = bar ? std::string(s, bar - s) : std::string(s);
	params = bar ? std::string(bar + 1) : std::string();
}

// ---- hit reactions: KCD2's own flinch / stagger on a living actor (RE: docs/kcd2_hit_reaction_re.md, WHGame 1.5.6)
// entity id -> CCryAction::m_pThis +0x518 IActorSystem -> vt+0x18 GetActor(id) = C_Actor* -> +0x240 HitReactionComponent;
// S_HitReactionEvent built by its ctor (keeps the +0x8C default that would otherwise force the death-only hit type 7),
// then HitReactionComponent::ProcessHit(comp, &event) -> bool (true = a reaction action started). Main thread only.
using HitEvtCtorFn = void* (__fastcall*)(void* evt);
using ProcessHitFn = bool(__fastcall*)(uintptr_t comp, const void* evt);
using GetActorFn = uintptr_t(__fastcall*)(uintptr_t actorSystem, uint32_t id);
using GetWorldTMFn = const float* (__fastcall*)(uintptr_t entity);                  // IEntity +0x100 (returns this+0x58)
static uintptr_t g_cryActionThis = 0;            // &CCryAction::m_pThis
static HitEvtCtorFn g_hitEvtCtor = nullptr;
static ProcessHitFn g_processHit = nullptr;
static uint64_t g_hitReacts = 0;

static void FindHitReact() {
	if (uintptr_t a = FindUnique(g_game, "48 89 0D ?? ?? ?? ?? 88 81 90 00 00 00 88 81 90 02 00 00")) g_cryActionThis = RipTarget(a, 3, 7);
	g_hitEvtCtor = (HitEvtCtorFn)FindUnique(g_game, "48 83 EC 18 45 33 C0 C6 41 24 02 83 CA FF 4C 89 04 24");
	g_processHit = (ProcessHitFn)FindUnique(g_game, "48 89 5C 24 18 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC B0 00 00 00 F6 42 78 04");
	Log("hitreact: CCryAction::m_pThis %p  event ctor %p  ProcessHit %p", (void*)g_cryActionThis, (void*)g_hitEvtCtor, (void*)g_processHit);
}

static uintptr_t SafeGetActor(uintptr_t as, uint32_t id) {
	__try { auto get = (GetActorFn)VFn(as, 0x18); return get ? get(as, id) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uintptr_t SafeGetEntity(uintptr_t es, uint32_t id) {
	__try { auto get = (GetEntityFn)VFn(es, 0x70); return get ? get(es, id) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// A rigid body's spin cap: CRigidEntity m_maxw (+0x24C), 20 rad/s by default, so a thrown casing barely turned. Only
// IPhysicalEntity::SetParams(pe_simulation_params.maxRotVel) changes it; the Lua binding never fills that field.
// WHGame 1.5.6: IEntity +0x278 GetPhysics; IPhysicalEntity +0x20 SetParams(pe_params*, int threadSafe);
// pe_simulation_params = type 10, 0x4C bytes, maxRotVel at +0x30, everything else marked unused (floats 0xFFBFFFFF,
// ints 0x80000000, a Vec3 by its x). The set-velocity action clamps w to the cap, so Lua sets it before the throw.
static bool SafeSetMaxSpin(uintptr_t entity, float maxw) {
	__try {
		auto getPhys = (uintptr_t(__fastcall*)(uintptr_t))VFn(entity, 0x278);
		uintptr_t pe = getPhys ? getPhys(entity) : 0;
		if (!pe) return false;
		const uint32_t UF = 0xFFBFFFFF, UI = 0x80000000;
		uint32_t p[19] = { 10, UI, UF, UF, UF, UF, 0, 0, UF, UF, 0, 0, 0, UF, UF, UI, UI, UF, UI };
		memcpy(&p[12], &maxw, 4);
		auto setParams = (int(__fastcall*)(uintptr_t, void*, int))VFn(pe, 0x20);
		return setParams && setParams(pe, p, 0) != 0;   // threadSafe 0, as the Lua binding passes
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// Live particle library reload (fxreload): the engine never re-reads a loaded library nor replaces an existing effect
// (LoadLibrary returns early, LoadEffect returns the old one), so each <Particles> node of the file is serialized into
// its existing effect. WHGame 1.5.6: ISystem (gEnv+0xC0) +0x410 LoadXmlFromBuffer(XmlNodeRef* ret, buf, len, false,
// true); IXmlNode +0x18 AddRef, +0x20 Release, +0x40 isTag, +0x108 getAttr, +0x160 getChildCount, +0x168
// getChild(XmlNodeRef* ret, i); IParticleManager (gEnv+0x68) +0x38 LoadEffect(name, XmlNodeRef& node, loadRes, src);
// IParticleEffect +0xB8 LoadResources, +0xC8 Serialize(XmlNodeRef by value = pointer to an AddRef'd temporary it
// releases, loading, children). Serialize rebuilds params in place with no lock: only while no emitter of the library
// is alive (Lua waits 3 s after a shot). Children are matched by Name; removed ones stay (Enabled="0" detaches one).
using XmlFn = void(__fastcall*)(uintptr_t node);
static int SafeFxReload(uintptr_t sys, uintptr_t pm, const char* buf, size_t len, const char* lib) {
	__try {
		auto loadBuf = (void*(__fastcall*)(uintptr_t, void**, const char*, size_t, bool, bool))VFn(sys, 0x410);
		void* root = nullptr;
		if (!loadBuf) return -2;
		loadBuf(sys, &root, buf, len, false, true);
		if (!root) return -1;
		uintptr_t r = (uintptr_t)root;
		auto childCount = (int(__fastcall*)(uintptr_t))VFn(r, 0x160);
		auto getChild = (void*(__fastcall*)(uintptr_t, void**, int))VFn(r, 0x168);
		auto isTag = (bool(__fastcall*)(uintptr_t, const char*))VFn(r, 0x40);
		auto getAttr = (const char*(__fastcall*)(uintptr_t, const char*))VFn(r, 0x108);
		auto addRef = (XmlFn)VFn(r, 0x18), release = (XmlFn)VFn(r, 0x20);
		auto loadEffect = (uintptr_t(__fastcall*)(uintptr_t, const char*, void**, bool, const char*))VFn(pm, 0x38);
		int done = 0, n = childCount(r);
		for (int i = 0; i < n; i++) {
			void* c = nullptr;
			getChild(r, &c, i);
			if (!c) continue;
			uintptr_t cn = (uintptr_t)c;
			const char* name = isTag(cn, "Particles") ? getAttr(cn, "Name") : nullptr;
			if (name && *name) {
				char full[256];
				snprintf(full, sizeof(full), "%s.%s", lib, name);
				uintptr_t eff = loadEffect(pm, full, &c, false, nullptr);
				if (eff) {
					addRef(cn);
					void* tmp = c;
					((void(__fastcall*)(uintptr_t, void**, bool, bool))VFn(eff, 0xC8))(eff, &tmp, true, true);
					((bool(__fastcall*)(uintptr_t))VFn(eff, 0xB8))(eff);
					done++;
				}
			}
			release(cn);
		}
		release(r);
		return done;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -3; }
}
static void FxReload(const char* rest) {
	std::string all = rest ? rest : "";
	size_t sp = all.find_last_of(' ');
	if (sp == std::string::npos) { Log("fxreload: usage fxreload <xml path> <library>"); return; }
	std::string path = all.substr(0, sp), lib = all.substr(sp + 1);
	HANDLE f = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (f == INVALID_HANDLE_VALUE) { Log("fxreload: can't open %s", path.c_str()); return; }
	DWORD size = GetFileSize(f, nullptr), got = 0;
	std::string buf(size, '\0');
	BOOL ok = size && ReadFile(f, &buf[0], size, &got, nullptr);
	CloseHandle(f);
	uintptr_t sys = 0, pm = 0;
	if (!ok || got != size || !g_env || !ReadPtr(g_env + 0xC0, sys) || !sys || !ReadPtr(g_env + 0x68, pm) || !pm) {
		Log("fxreload: read %s failed or no engine", path.c_str()); return;
	}
	int n = SafeFxReload(sys, pm, buf.c_str(), buf.size(), lib.c_str());
	if (n >= 0) Log("fxreload %s: %d effects re-read from %s", lib.c_str(), n, path.c_str());
	else Log("fxreload %s: failed (%s)", lib.c_str(), n == -1 ? "XML didn't parse: see kcd.log" : n == -3 ? "crashed (caught)" : "no XML loader");
}
static bool SafeWorldTM(uintptr_t entity, float* tm12) {
	__try {
		auto get = (GetWorldTMFn)VFn(entity, 0x100); const float* m = get ? get(entity) : nullptr;
		if (!m) return false; memcpy(tm12, m, 12 * sizeof(float)); return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static bool SafeEventCtor(uint8_t* e) { __try { g_hitEvtCtor(e); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }
// 1 = a reaction started, 0 = the game declined (gates / no row), -1 = crashed (caught)
static int SafeProcessHit(uintptr_t comp, const uint8_t* e) {
	__try { return g_processHit(comp, e) ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

// body_part_name -> body_part_id (Libs/Tables/item/body_part.xml, human rows); the event takes id - 1 (the reaction
// query looks rows up with index + 1), -1 = no body part
static int BodyPartId(const char* name) {
	static const char* names[] = { "head", "torso", "arm_left", "arm_right", "leg_left", "leg_right" };
	for (int i = 0; i < 6; i++) if (!_stricmp(name, names[i])) return i + 1;
	return -1;
}

// hitreact <victimHex> <attackerHex> <hitType> <strength> <bodyPart> <px> <py> <pz> <dx> <dy> <dz> <flags>
//   dir = world-space travel direction of the bullet. flags low byte = event flags (0x3F = all; 0x04 game impact FX,
//   0x20 AI / crime stimulus). Plugin bits: 0x100 send dir as given (world, no conversion), 0x200 don't negate
//   (default: the event gets the actor-local direction towards the shooter, i.e. -travel, like hit_direction 0;1;0 =
//   "so_forward"), 0x400 send body_part_id itself instead of id - 1.
static void HitReact(const char* rest) {
	char vic[24] = {}, att[24] = {}, part[32] = {};
	int hitType = 0, strength = 0; unsigned flags = 0x3F; float pos[3] = {}, dir[3] = {};
	int n = sscanf_s(rest, "%23s %23s %d %d %31s %f %f %f %f %f %f %x", vic, (unsigned)sizeof(vic), att, (unsigned)sizeof(att),
		&hitType, &strength, part, (unsigned)sizeof(part), &pos[0], &pos[1], &pos[2], &dir[0], &dir[1], &dir[2], &flags);
	if (n < 11) { Log("hitreact: bad command (%d fields): %s", n, rest); return; }
	unsigned long long v = 0, a = 0;
	sscanf_s(strncmp(vic, "0x", 2) ? vic : vic + 2, "%llx", &v);
	sscanf_s(strncmp(att, "0x", 2) ? att : att + 2, "%llx", &a);
	uint32_t victim = (uint32_t)v, attacker = (uint32_t)a;
	if (strength > 6) strength = 6; if (strength < 0) strength = 0;    // 7 = death reaction: never on the living
	if (!g_cryActionThis || !g_hitEvtCtor || !g_processHit) { Log("hitreact: patterns missing"); return; }
	if (!Finite(pos, 3) || !Finite(dir, 3)) { Log("hitreact: non-finite pos/dir"); return; }

	uintptr_t fw = 0, as = 0, actor = 0, comp = 0, es = 0, ent = 0;
	if (!ReadPtr(g_cryActionThis, fw) || !fw) { Log("hitreact %08X: no CCryAction", victim); return; }
	if (!ReadPtr(fw + 0x518, as) || !as) { Log("hitreact %08X: no actor system", victim); return; }
	actor = SafeGetActor(as, victim);
	if (!actor) { Log("hitreact %08X: not an actor", victim); return; }
	if (!ReadPtr(actor + 0x240, comp) || !comp) { Log("hitreact %08X: actor %p has no hit reaction component", victim, (void*)actor); return; }

	// direction for the event
	float d[3] = { dir[0], dir[1], dir[2] };
	float len = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (len > 1e-4f) { d[0] /= len; d[1] /= len; d[2] /= len; } else { d[0] = 0; d[1] = 1; d[2] = 0; }
	if (!(flags & 0x200)) { d[0] = -d[0]; d[1] = -d[1]; d[2] = -d[2]; }
	float ld[3] = { d[0], d[1], d[2] };
	bool local = false;
	if (!(flags & 0x100) && g_env && ReadPtr(g_env + 0xA0, es) && es) {
		float tm[12];
		if ((ent = SafeGetEntity(es, victim)) && SafeWorldTM(ent, tm)) {
			// Matrix34 row-major: column j = local axis j in world; local = R^T * world (axes normalised: entity scale)
			for (int j = 0; j < 3; j++) {
				float ax[3] = { tm[j], tm[4 + j], tm[8 + j] };
				float l = sqrtf(ax[0] * ax[0] + ax[1] * ax[1] + ax[2] * ax[2]);
				ld[j] = l > 1e-6f ? (ax[0] * d[0] + ax[1] * d[1] + ax[2] * d[2]) / l : 0;
			}
			local = true;
		}
	}

	int partId = BodyPartId(part);
	int partField = partId < 0 ? -1 : ((flags & 0x400) ? partId : partId - 1);

	alignas(16) uint8_t e[0xA0] = {};
	if (!SafeEventCtor(e)) { Log("hitreact %08X: event ctor crashed", victim); return; }
	*(uint32_t*)(e + 0x00) = attacker;
	*(uint32_t*)(e + 0x18) = (uint32_t)hitType;
	*(uint32_t*)(e + 0x28) = victim;
	*(int32_t*)(e + 0x34) = partField;
	*(uint32_t*)(e + 0x44) = (uint32_t)strength;
	memcpy(e + 0x50, pos, 12);
	memcpy(e + 0x5C, ld, 12);
	*(uint32_t*)(e + 0x78) = flags & 0xFF;
	int r = SafeProcessHit(comp, e);
	g_hitReacts++;
	Log("hitreact #%llu victim %08X attacker %08X actor %p comp %p type %d strength %d part %s (id %d -> %d) pos %.2f %.2f %.2f "
		"dir world %.2f %.2f %.2f -> %s %.2f %.2f %.2f flags %02X -> %s",
		g_hitReacts, victim, attacker, (void*)actor, (void*)comp, hitType, strength, part, partId, partField, pos[0], pos[1], pos[2],
		dir[0], dir[1], dir[2], local ? "local" : "as given", ld[0], ld[1], ld[2], flags & 0xFF,
		r > 0 ? "reaction started" : r == 0 ? "declined (gates / no row / busy / dead)" : "CRASHED (caught)");
}

// ---- commands from Lua: System.LogAlways("[DN] <cmd> ...")
// ---- KCD2's own action filters: "no_torch" while the gun is out (R reloads; the torch must stay put) and "no_move"
// while the free camera flies (WASD move the camera, not Henry). Lua's ActionMapManager can't enable filters, so the
// plugin calls CActionMapManager directly (docs/kcd2_action_filter.md): CCryAction::m_pThis (found for hit reactions)
// -> vfn +0xE0 GetIActionMapManager; +0xF0 EnableFilter(name, key) adds a reference under a key, +0xF8 (name, key,
// all) takes one away. KCD2 counts references per key (a quest flowgraph uses no_torch too), so ours go under our own
// key, strictly balanced, never "all". Applied in the frame hook, outside Lua (an on/off edge notifies listeners).
// (Scanning gEnv for CCryAction found nothing in game 2026-10-12: it isn't a gEnv member in KCD2.)
static const uint8_t kFilterKey = 0xDE;
struct OurFilter { const char* name; int want; bool on; };
static OurFilter g_filters[] = { { "no_torch", -1, false }, { "no_move", -1, false }, { "no_camera", -1, false } };
enum { F_NO_TORCH = 0, F_NO_MOVE = 1, F_NO_CAMERA = 2 };
static uintptr_t g_amm = 0;
static uintptr_t SafeGetter(uintptr_t obj, int off) {   // no C++ objects here: MSVC forbids __try next to unwinding
	auto get = (uintptr_t(__fastcall*)(uintptr_t))VFn(obj, off);
	__try { return get ? get(obj) : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
static uintptr_t FindActionMapManager() {
	if (g_amm) return g_amm;
	static bool said = false;
	uintptr_t fw = 0, vt = 0, amm = 0, avt = 0;
	if (!g_cryActionThis || !ReadPtr(g_cryActionThis, fw) || !fw || !ReadPtr(fw, vt)) {
		if (!said) { said = true; Log("filters: no CCryAction (m_pThis %p)", (void*)g_cryActionThis); }
		return 0;
	}
	std::string fwName = RttiName(g_game, vt);
	amm = SafeGetter(fw, 0xE0);
	if (amm && ReadPtr(amm, avt) && RttiName(g_game, avt) == ".?AVCActionMapManager@@") {
		g_amm = amm;
		Log("filters: action map manager %p (from %s %p)", (void*)amm, fwName.c_str(), (void*)fw);
		return amm;
	}
	if (!said) { said = true; Log("filters: %s %p vfn+0xE0 gave %p (%s): not the action map manager", fwName.c_str(), (void*)fw,
		(void*)amm, avt ? RttiName(g_game, avt).c_str() : "?"); }
	return 0;
}
static bool CallFilter(uintptr_t amm, const char* name, bool on) {
	__try {
		if (on) ((void(__fastcall*)(uintptr_t, const char*, uint8_t))VFn(amm, 0xF0))(amm, name, kFilterKey);
		else ((void(__fastcall*)(uintptr_t, const char*, uint8_t, bool))VFn(amm, 0xF8))(amm, name, kFilterKey, false);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
static void ApplyFilters() {
	if (g_filters[F_NO_MOVE].on != Freecam_Active()) g_filters[F_NO_MOVE].want = Freecam_Active() ? 1 : 0;
	for (auto& f : g_filters) {
		if (f.want < 0) continue;
		bool on = f.want == 1;
		f.want = -1;
		if (on == f.on) continue;
		uintptr_t amm = FindActionMapManager();
		if (!amm) return;
		if (CallFilter(amm, f.name, on)) { f.on = on; Log("filter %s %s", f.name, on ? "on" : "off"); }
		else Log("filter %s: the call faulted", f.name);
	}
}

static void Command(const char* line) {
	char cmd[32] = {};
	if (sscanf_s(line, "%31s", cmd, (unsigned)sizeof(cmd)) != 1) return;
	const char* rest = line + strlen(cmd);
	while (*rest == ' ') rest++;
	if (!strcmp(cmd, "hello")) {   // every save load / script start: forget the old viewmodel (its id is gone)
		Freecam_Reset("hello");
		for (auto& f : g_filters) f.want = 0;   // a load holsters the gun and ends the freecam: let our references go
		// a load may have freed the old level's emitters: forget them (at most a few, only if a flash was burning)
		g_follow = false; g_vmId = 0; mo.aiming = false; g_nFlashes = 0; g_haveVmTM = false; g_live = 0; g_liveFrame = -1;
		// the handshake, then the player's saved settings (settings.cpp)
		std::vector<std::string> saved = SettingsReplay();
		{
			std::lock_guard<std::mutex> lk(g_consoleMu);
			g_console.clear(); g_console.push_back("deagle_native_ok");
			for (auto& c : saved) g_console.push_back(c);
		}
		Log("hello from Lua (%zu settings to replay)", saved.size());
	}
	else if (!strcmp(cmd, "vm")) {
		unsigned long long id = 0;
		const char* hex = strstr(rest, "0x"); hex = hex ? hex + 2 : rest;
		if (sscanf_s(hex, "%llx", &id) == 1) { g_vmId = (uint32_t)id; Log("viewmodel entity %08X", g_vmId); }
	}
	else if (!strcmp(cmd, "follow")) { g_follow = atoi(rest) != 0; Log("follow %d", (int)g_follow); }
	else if (!strcmp(cmd, "flashfx")) {   // flashfx <effect> <scale> <pos xyz> <dir xyz>: a flash on the muzzle (viewmodel axes)
		char effect[96] = {}; float scale = 1;
		int n = sscanf_s(rest, "%95s %f %f %f %f %f %f %f", effect, (unsigned)sizeof(effect), &scale, &g_flashPos[0], &g_flashPos[1],
			&g_flashPos[2], &g_flashDir[0], &g_flashDir[1], &g_flashDir[2]);
		float loc[8];
		if (n != 8) Log("bad flashfx command: %s", rest);
		else if (!g_haveVmTM || !MuzzleLoc(g_vmTM, scale, loc)) Log("flashfx: the gun isn't placed yet");
		else {
			static bool told = false;
			if (!told) {
				told = true;
				uintptr_t pm = 0, vt = 0;
				ReadPtr(g_env + 0x68, pm); if (pm) ReadPtr(pm, vt);
				Log("flashfx: particle manager %p %s", (void*)pm, vt ? RttiName(g_game, vt).c_str() : "?");
			}
			uintptr_t em = SafeSpawnFlash(effect, loc);
			if (em == (uintptr_t)-1) Log("flashfx: no effect %s", effect);
			else if (!em) Log("flashfx: %s didn't spawn", effect);
			else {
				if (g_nFlashes == (int)(sizeof(g_flashes) / sizeof(g_flashes[0]))) {   // full: the oldest goes
					SafeRelease(g_flashes[0].emitter);
					memmove(g_flashes, g_flashes + 1, sizeof(Flash) * (g_nFlashes - 1)); g_nFlashes--;
				}
				Flash& fl = g_flashes[g_nFlashes++];
				fl.emitter = em; fl.scale = scale; fl.firstFrame = 0; QueryPerformanceCounter(&fl.born);
				if (g_flashSeed > 0) {
					float r = g_flashSeed * (scale > 0 ? scale : 1), box[6];
					for (int k = 0; k < 3; k++) { box[k] = loc[4 + k] - r; box[3 + k] = loc[4 + k] + r; }
					WriteBox(em, box);
				}
				static int said = 0;
				if (said++ < 40) Log("flashfx %s effect %p emitter %p refs %ld at %.3f %.3f %.3f q %.3f %.3f %.3f %.3f s %.2f (gun at %.3f %.3f %.3f)",
					effect, (void*)g_lastEffect, (void*)em, g_lastRefs, loc[4], loc[5], loc[6], loc[0], loc[1], loc[2], loc[3], loc[7],
					g_vmTM[3], g_vmTM[7], g_vmTM[11]);
			}
		}
	}
	else if (!strcmp(cmd, "fxsseed")) g_fxsSeed = atoi(rest) != 0;
	else if (!strcmp(cmd, "fxs")) {   // fxs <effect> <scale> <len m> <pos xyz> <dir xyz>: a world effect (a tracer piece) seen
		// from its first frame: a new emitter starts with an empty bounding box and is skipped until an update gives it
		// one, which a 0.12 s piece may never get (Lua-spawned single pieces never showed, 2026-10-12): it gets the box
		// from pos to pos + dir x len (+ 0.15 m) at once. Our reference is kept 2 s of game time (HoldPiece): letting it go at once
		// showed one tracer, then none (the manager holds none of its own: the release deleted them).
		char effect[96] = {}; float scale = 1, len = 1, p[3] = {}, d[3] = {}, loc[8];
		int n = sscanf_s(rest, "%95s %f %f %f %f %f %f %f %f", effect, (unsigned)sizeof(effect), &scale, &len, &p[0], &p[1], &p[2], &d[0], &d[1], &d[2]);
		if (n == 9 && Finite(p, 3) && Finite(d, 3) && Finite(&len, 1) && LocFromDir(p, d, scale, loc)) {
			uintptr_t em = SafeSpawnFlash(effect, loc);
			if (em && em != (uintptr_t)-1) {
				float dl = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]), box[6];
				for (int k = 0; k < 3; k++) {
					float e = p[k] + (dl > 0 ? d[k] / dl : 0) * len;
					box[k] = (p[k] < e ? p[k] : e) - 0.15f; box[3 + k] = (p[k] > e ? p[k] : e) + 0.15f;
				}
				if (g_fxsSeed) WriteBox(em, box);
				HoldPiece(em, loc);
			}
			else { static int said = 0; if (said++ < 5) Log("fxs: %s didn't spawn", effect); }
		}
	}
	else if (!strcmp(cmd, "flashseed")) { float v = (float)atof(rest); g_flashSeed = Finite(&v, 1) && v > 0 ? v : 0; Log("flash seed box %.2f m", g_flashSeed); }
	else if (!strcmp(cmd, "flashpos"))
		sscanf_s(rest, "%f %f %f %f %f %f", &g_flashPos[0], &g_flashPos[1], &g_flashPos[2], &g_flashDir[0], &g_flashDir[1], &g_flashDir[2]);
	else if (!strcmp(cmd, "flashoff")) {
		static int said = 0; if (said++ < 40) Log("flashoff: releasing %d", g_nFlashes);
		ReleaseFlashes();
	}
	else if (!strcmp(cmd, "kick")) {     // kick <back m> <pitch deg> <yaw deg> [<roll deg>]: peaks of the shot's push
		float b = 0, pt = 0, yw = 0, rl = 0;
		if (sscanf_s(rest, "%f %f %f %f", &b, &pt, &yw, &rl) >= 3) {
			const float w = 28.0f * 2.71828f;   // velocity that peaks at the given displacement (spring w = 28 rad/s)
			mo.kickBack.v += b * w; mo.kickPitch.v += pt * w; mo.kickYaw.v += yw * w; mo.kickRoll.v += rl * w;
		}
	}
	else if (!strcmp(cmd, "off")) sscanf_s(rest, "%f %f %f", &g_off[0], &g_off[1], &g_off[2]);
	else if (!strcmp(cmd, "snd")) {
		char file[260] = {}; float v = 1.0f;
		if (sscanf_s(rest, "%259s %f", file, (unsigned)sizeof(file), &v) >= 1) PlaySoundFile(file, v);
	}
	else if (!strcmp(cmd, "snd3")) {   // snd3 <file> <vol> <x> <y> <z> <minDist> <maxDist>
		char file[260] = {}; float v = 1.0f, at[3] = {}, mn = 1.0f, mx = 100.0f;
		if (sscanf_s(rest, "%259s %f %f %f %f %f %f", file, (unsigned)sizeof(file), &v, &at[0], &at[1], &at[2], &mn, &mx) >= 5 && Finite(at, 3))
			PlaySound3D(file, v, at, mn, mx);
	}
	else if (!strcmp(cmd, "speed")) {
		float v = (float)atof(rest);
		if (Finite(&v, 1)) { SetSoundSpeed(v); if (RonReady()) RonSpeed(g_soundSpeed); g_gameSpeed = g_soundSpeed; }
		Log("sound speed %.2f", g_soundSpeed);
	}
	else if (!strcmp(cmd, "ron")) {    // ron <vol> <event path>|<name=value ...>: a Ready or Not event at the listener
		float v = 1.0f; int used = 0;
		if (sscanf_s(rest, "%f %n", &v, &used) >= 1 && used > 0 && Finite(&v, 1) && RonEnsure()) {
			std::string ev, params; RonSplit(rest + used, ev, params);
			if (!RonPlay(ev.c_str(), params.c_str(), nullptr, v)) Log("ron: %s didn't play", ev.c_str());
		}
	}
	else if (!strcmp(cmd, "ron3")) {   // ron3 <vol> <x> <y> <z> <event path>|<name=value ...>: at a world point, heard after
	                                     // its travel time (RonPlayAt; PropagateDistance=@ = the distance to the listener)
		float v = 1.0f, at[3] = {}; int used = 0;
		if (sscanf_s(rest, "%f %f %f %f %n", &v, &at[0], &at[1], &at[2], &used) >= 4 && used > 0 && Finite(&v, 1) && Finite(at, 3) && RonEnsure()) {
			std::string ev, params; RonSplit(rest + used, ev, params);
			if (!RonPlayAt(ev.c_str(), params.c_str(), at, v)) Log("ron: %s didn't play", ev.c_str());
		}
	}
	else if (!strcmp(cmd, "ronload")) {   // ronload <event path>: the gun's events, at draw (no load delay on the first shot)
		if (RonEnsure() && !RonLoad(rest)) Log("ron: no event %s", rest);
	}
	else if (!strcmp(cmd, "rong")) {   // rong <global> <value>: a Ready or Not global parameter (room / material weights)
		char name[64] = {}; float v = 0;
		if (sscanf_s(rest, "%63s %f", name, (unsigned)sizeof(name), &v) == 2 && Finite(&v, 1) && RonEnsure()) RonGlobal(name, v);
	}
	else if (!strcmp(cmd, "flasharm")) {   // flasharm <effect> <scale> <pos xyz> <dir xyz>: the live flash for the drawn gun
		char effect[96] = {}; float scale = 1;
		int n = sscanf_s(rest, "%95s %f %f %f %f %f %f %f", effect, (unsigned)sizeof(effect), &scale, &g_flashPos[0], &g_flashPos[1],
			&g_flashPos[2], &g_flashDir[0], &g_flashDir[1], &g_flashDir[2]);
		float loc[8];
		if (g_live) { SafeRelease(g_live); g_live = 0; }
		if (n != 8 || !Finite(&scale, 1)) Log("bad flasharm command: %s", rest);
		else if (!g_haveVmTM || !MuzzleLoc(g_vmTM, 1e-4f, loc)) Log("flasharm: the gun isn't placed yet");
		else {
			uintptr_t em = SafeSpawnFlash(effect, loc);
			if (em == (uintptr_t)-1 || !em) Log("flasharm: %s didn't spawn", effect);
			else { g_live = em; g_liveScale = scale; g_liveFrame = -1; Log("flasharm %s emitter %p scale %.2f", effect, (void*)em, scale); }
		}
	}
	else if (!strcmp(cmd, "flashshot")) {  // flashshot <size on frame 1> [<frame 2> ...]: show the live flash now
		float v[8] = {};
		int n = sscanf_s(rest, "%f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
		if (n > 0 && g_live) {
			g_liveN = 0;
			for (int i = 0; i < n; i++) if (Finite(&v[i], 1) && v[i] > 0) g_liveSizes[g_liveN++] = v[i];
			g_liveFrame = 0;
			if (g_liveN) LivePlace(g_liveSizes[0]);   // in this frame, before its particle update and render
		}
	}
	else if (!strcmp(cmd, "flashdisarm")) { if (g_live) { SafeRelease(g_live); g_live = 0; } g_liveFrame = -1; }
	else if (!strcmp(cmd, "flashgrow")) {   // flashgrow <size on drawn frame 1> <2> ... (up to 8; none = the particles' own life)
		float v[8] = {};
		int n = sscanf_s(rest, "%f %f %f %f %f %f %f %f", &v[0], &v[1], &v[2], &v[3], &v[4], &v[5], &v[6], &v[7]);
		g_flashGrowN = n > 0 ? n : 0;
		for (int i = 0; i < g_flashGrowN; i++) g_flashGrow[i] = Finite(&v[i], 1) && v[i] >= 0 ? v[i] : 1.0f;
		Log("flash grow %d frames", g_flashGrowN);
	}
	else if (!strcmp(cmd, "cfgsave")) {   // cfgsave <key> <value>: a deagle_set setting to keep (settings.cpp)
		char key[96] = {};
		if (sscanf_s(rest, "%95s", key, (unsigned)sizeof(key)) == 1) {
			const char* v = rest + strlen(key);
			while (*v == ' ') v++;
			SettingsSave(key, v);
		}
	}
	else if (!strcmp(cmd, "cfgreset")) SettingsReset();
	else if (!strcmp(cmd, "freecam")) Freecam_Command(rest);
	else if (!strcmp(cmd, "notorch")) g_filters[F_NO_TORCH].want = atoi(rest) != 0 ? 1 : 0;
	else if (!strcmp(cmd, "nocamera")) g_filters[F_NO_CAMERA].want = atoi(rest) != 0 ? 1 : 0;   // the freecam's mouse look
	else if (!strcmp(cmd, "vol")) { g_masterVol = (float)atof(rest); if (RonReady()) RonVolume(0.5f * g_masterVol); }
	else if (!strcmp(cmd, "aim")) mo.aiming = atoi(rest) != 0;
	else if (!strcmp(cmd, "motion")) { float v = (float)atof(rest); mo.enabled = v > 0; if (v > 0) mo.scale = v; Log("motion %.2f", v); }
	else if (!strcmp(cmd, "vel")) { float r = 0, f = 0, u = 0; if (sscanf_s(rest, "%f %f %f", &r, &f, &u) == 3) MotionVel(r, f, u); }   // world m/s (player:GetVelocity), every frame
	else if (!strcmp(cmd, "air")) MotionAir(atoi(rest) != 0);
	else if (!strcmp(cmd, "stance")) MotionStance(atoi(rest) != 0);        // 1 = crouched
	else if (!strcmp(cmd, "dodge")) { float sd = 0, k = 1; if (sscanf_s(rest, "%f %f", &sd, &k) >= 1) MotionDodge(sd, k); }
	else if (!strcmp(cmd, "mparams")) {   // mparams <key> <value>: per-gun motion tunables (motion.h MotionParams)
		char key[48] = {}; float v = 0;
		if (sscanf_s(rest, "%47s %f", key, (unsigned)sizeof(key), &v) >= 1 && !MotionParam(key, v)) Log("mparams: unknown key %s", key);
	}
	else if (!strcmp(cmd, "hitreact")) HitReact(rest);
	else if (!strcmp(cmd, "fxreload")) FxReload(rest);   // fxreload <xml path> <library>: live particle library reload
	else if (!strcmp(cmd, "casespin")) {   // casespin <entity id hex> <rad/s>: a casing's spin cap (SafeSetMaxSpin)
		static int logged = 0;
		char id[32] = {}; float w = 0; unsigned long long v = 0; uintptr_t es = 0, e = 0;
		if (sscanf_s(rest, "%31s %f", id, (unsigned)sizeof(id), &w) != 2 || !Finite(&w, 1) || w <= 0) Log("casespin: bad command: %s", rest);
		else if (sscanf_s(strncmp(id, "0x", 2) ? id : id + 2, "%llx", &v) != 1 || !g_env || !ReadPtr(g_env + 0xA0, es) || !es
			|| !(e = SafeGetEntity(es, (uint32_t)v)) || !SafeSetMaxSpin(e, w)) Log("casespin %s: no physical entity", id);
		else if (logged++ < 3) Log("casespin %s: cap %.0f rad/s", id, w);
	}
	else if (GoreCommand(cmd, rest, g_env)) {}   // listatt / hideatt (gore.cpp)
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

// The game asks for the main view's camera 2-3 times per drawn frame (~176 calls/s at 60-80 fps, deagle_native.log).
// A call with the same camera under 1 ms after the last one is a repeat within the frame: skip the per-frame work
// (placing the gun, motion springs, FMOD update) instead of doing it 2-3 times. A different camera is always processed.
static void OnCamera(uintptr_t camera) {
	uintptr_t vt;
	if (!ReadPtr(camera - 0xE8, vt) || !IsCView(vt)) return;
	float fov = 0, cam[12];
	if (!CopyCamera(camera, cam, &fov)) return;
	static LARGE_INTEGER last{}, freq{};
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
	double ms = last.QuadPart ? (now.QuadPart - last.QuadPart) * 1000.0 / (double)freq.QuadPart : 1e9;
	if (ms < 1.0 && memcmp(cam, g_cam, sizeof(cam)) == 0) { g_repeats++; return; }
	last = now;
	memcpy(g_cam, cam, sizeof(cam));
	uint64_t n = ++g_viewFrames;
	UpdateMotion(g_cam);
	if (g_follow && g_vmId && g_env && SafePlace(g_cam)) g_placed++;
	{   // our sounds are heard from where the view is: the free camera while it's on, else Henry's eye
		float fc[12];
		const float* ear = Freecam_Matrix(fc) ? fc : g_cam;
		if (g_fmod) { UpdateListener(ear); fm.update(g_fmod); }
		if (RonReady()) RonUpdate(ear);
	}
	ApplyFilters();
	TouchPieces();
	ReleaseOldPieces(false);
	{   // tell Lua when the free camera comes and goes (its look mode, the aim line)
		static bool fcWas = false;
		bool fc = Freecam_Active();
		if (fc != fcWas) {
			fcWas = fc;
			std::lock_guard<std::mutex> lk(g_consoleMu);
			g_console.push_back(fc ? "deagle_freecam_state 1" : "deagle_freecam_state 0");
		}
	}
	if (g_env) {
		std::deque<std::string> run;
		{ std::lock_guard<std::mutex> lk(g_consoleMu); run.swap(g_console); }
		for (auto& c : run) SafeConsole(c.c_str());
	}
	if (n == 1 || n % 3000 == 0)
		Log("frames %llu placed %llu repeats skipped %llu cmds %llu sounds %zu", n, g_placed, g_repeats, g_cmds, g_sounds.size());
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
		g_ronDir = p + "\\Mods\\deagle\\ron\\";
		g_ronDir = p + "\\Mods\\deagle\\ron\\";
		Log("sound root %s", g_soundRoot.c_str());
	}
	uintptr_t gp = FindUnique(g_game, "48 89 5C 24 08 57 48 83 EC ?? 48 8B 01 49 8B F8 4C 8D 44 24 20 C7 44 24 20 05 00 00 00 FF 50 38 32 DB 84 C0 74 11");
	uintptr_t lh = FindUnique(g_game, "40 53 48 83 EC ?? 48 8B 02 48 8B CA 48 8B DA FF 50 28 BA 01 00 00 00 48 8B CB 3B C2 74 27 48 8B 03 FF 50 28 BA 02 00 00 00 4C 8D 0D ?? ?? ?? ?? 4C 8D 05 ?? ?? ?? ?? 89 44 24 20 8D 4A 05 E8 ?? ?? ?? ?? EB 35");
	g_getParamStr = (GetParamStrFn)gp;
	Log("GetParam<str> %p  LogAlways handler %p", (void*)gp, (void*)lh);
	FindHitReact();
	if (gp && lh && MH_CreateHook((void*)lh, (void*)&HkLog, (void**)&g_origLog) == MH_OK && MH_EnableHook((void*)lh) == MH_OK)
		Log("Lua channel hook installed");
	else
		Log("Lua channel hook FAILED");
	for (int i = 0; i < 240 && !g_env; i++) { g_env = FindEnv(); if (!g_env) Sleep(500); }
	Log("gEnv %p", (void*)g_env);
	if (g_env) Freecam_Install(g_env, (void*)g_origFrustum);   // free camera (freecam.cpp, johnpork-18)
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
		SettingsInit(h);
		CreateThread(nullptr, 0, Init, nullptr, 0, nullptr);
	}
	return TRUE;
}
