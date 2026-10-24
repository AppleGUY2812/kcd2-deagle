// Fast-travel preload: see ftpreload.h. Addresses: WHGame.dll 1.5.6 (docs/whgame_re/fast_travel.md).
#include "ftpreload.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace {

// the game's module singleton (FUN_1809155c8 returns it once its static init ran)
constexpr uintptr_t kRvaGame = 0x549d388;
// fast-travel manager = *(*(game + 0x128) + 0x28)            (FUN_182affe7c / FUN_181f4a750)
//   +0x48 u8   travelling, set by start (FUN_182de28f4), cleared at the end (FUN_182de2d98)
//   +0x68 f32x3 destination, written by the route builder FUN_182de27d0 (map hover and confirm)
//   +0x90/+0x98 route, Vec3 (12 bytes) begin / end
//   +0x110 ptr, its +0x68 u8: the second "is fast travelling" state of FUN_180534e5c
// I3DEngine::AddPrecachePoint(out future, const Vec3& pos, const Vec3& dir, float timeout, float importance),
// found in the 3D engine's vtable by address (FUN_181899654 -> queue -> FUN_1820d1790, max 4 points, oldest evicted)
constexpr uintptr_t kRvaAddPrecache = 0x1899654;
const unsigned char kAddPrecacheBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x50,0x41,0x8b,0x40,0x08,0x48,0x8b,0xda,
                                            0xf3,0x0f,0x10,0x84,0x24,0x80,0x00,0x00,0x00 };
// the route builder's first bytes: "movsd xmm0,[rdx]; mov rdi,r9; movsd [rcx+0x68],xmm0" = destination at +0x68
constexpr uintptr_t kRvaRoute = 0x2de27d0;
const unsigned char kRouteBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0xf2,0x0f,0x10,0x02,0x49,0x8b,
                                      0xf9,0xf2,0x0f,0x11,0x41,0x68 };

using AddPrecacheFn = void* (__fastcall*)(void* engine, void* outFuture, const float* pos, const float* dir, float timeout,
                                          float importance);

constexpr float kTimeout = 4.0f;       // s a point lives
constexpr DWORD kRefreshMs = 2500;     // re-added while the walk runs, so it lapses ~4 s after the walk stops

uintptr_t g_env = 0, g_base = 0, g_end = 0;
int g_mode = 2;
bool g_ok = false, g_tried = false;    // the code checks passed / ran
AddPrecacheFn g_add = nullptr;
uintptr_t g_engine = 0;

struct State { bool valid; uint8_t active, active2; float dest[3]; size_t route; float last[3], prev[3]; };
State g_was{};
DWORD g_lastAdd = 0, g_walkStart = 0;
int g_adds = 0;

bool InImage(uintptr_t p) { return p >= g_base && p < g_end; }

bool ReadState(State& s) {
	s = State{};
	__try {
		uintptr_t game = *(uintptr_t*)(g_base + kRvaGame);
		if (!game) return false;
		uintptr_t sub = *(uintptr_t*)(game + 0x128);
		if (!sub) return false;
		uintptr_t m = *(uintptr_t*)(sub + 0x28);
		if (!m || !InImage(*(uintptr_t*)m)) return false;        // a vtable in WHGame or it isn't the manager
		s.active = *(uint8_t*)(m + 0x48);
		uintptr_t t = *(uintptr_t*)(m + 0x110);
		s.active2 = t ? *(uint8_t*)(t + 0x68) : 0;
		memcpy(s.dest, (void*)(m + 0x68), 12);
		uintptr_t b = *(uintptr_t*)(m + 0x90), e = *(uintptr_t*)(m + 0x98);
		s.route = (b && e > b && (e - b) % 12 == 0) ? (e - b) / 12 : 0;
		if (s.route > 100000) s.route = 0;
		if (s.route >= 1) memcpy(s.last, (void*)(e - 12), 12);
		if (s.route >= 2) memcpy(s.prev, (void*)(e - 24), 12);
		s.valid = true;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Plausible(const float* p) {
	for (int i = 0; i < 3; i++) if (!std::isfinite(p[i])) return false;
	return p[0] > -1.0f && p[0] < 20000.0f && p[1] > -1.0f && p[1] < 20000.0f && p[2] > -500.0f && p[2] < 3000.0f &&
		(p[0] != 0.0f || p[1] != 0.0f);
}

bool AddPoint(const float* pos, const float* dir) {
	alignas(16) unsigned char fut[32] = {};   // the returned future's reference is dropped (a few bytes a call, ~10 a trip)
	__try { g_add((void*)g_engine, fut, pos, dir, kTimeout, 1.0f); return true; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool Setup() {
	g_base = (uintptr_t)GetModuleHandleA("WHGame.dll");
	if (!g_base) { Log("ftpre: no WHGame.dll"); return false; }
	auto dos = (IMAGE_DOS_HEADER*)g_base;
	auto nt = (IMAGE_NT_HEADERS64*)(g_base + dos->e_lfanew);
	g_end = g_base + nt->OptionalHeader.SizeOfImage;
	if (memcmp((void*)(g_base + kRvaRoute), kRouteBytes, sizeof kRouteBytes) ||
		memcmp((void*)(g_base + kRvaAddPrecache), kAddPrecacheBytes, sizeof kAddPrecacheBytes)) {
		Log("ftpre: WHGame.dll isn't the 1.5.6 code: off");
		return false;
	}
	uintptr_t eng = 0, vt = 0;
	__try { eng = *(uintptr_t*)(g_env + 0x08); vt = eng ? *(uintptr_t*)eng : 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { eng = 0; }
	if (!eng || !vt) { Log("ftpre: no I3DEngine"); return false; }
	int slot = -1;
	__try {
		for (int i = 0; i < 0x200; i++)
			if (((uintptr_t*)vt)[i] == g_base + kRvaAddPrecache) { slot = i; break; }
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
	if (slot < 0) { Log("ftpre: AddPrecachePoint isn't in the 3D engine's vtable: log only"); return true; }
	g_engine = eng;
	g_add = (AddPrecacheFn)(g_base + kRvaAddPrecache);
	Log("ftpre: ready, I3DEngine %p AddPrecachePoint vtable +0x%X, mode %d", (void*)eng, slot * 8, g_mode);
	return true;
}

}  // namespace

void Ftpre_Install(uintptr_t env, uintptr_t) { g_env = env; }   // Setup waits for the first drawn frame (the 3D engine is up)

void Ftpre_Command(const char* args) {
	g_mode = args ? atoi(args) : 0;
	Log("ftpre mode %d (%s)%s", g_mode, g_mode == 0 ? "off" : g_mode == 1 ? "log only" : "log + precache the destination",
		g_tried && !g_ok ? " - but the code checks failed, nothing runs" : "");
}

void Ftpre_Frame() {
	if (g_mode <= 0 || !g_env) return;
	if (!g_tried) { g_tried = true; g_ok = Setup(); }
	if (!g_ok) return;
	State s;
	if (!ReadState(s)) { if (g_was.valid) Log("ftpre: the fast-travel manager went away"); g_was = State{}; return; }
	DWORD now = GetTickCount();
	bool walking = s.active || s.active2;
	bool wasWalking = g_was.active || g_was.active2;
	if (s.active != g_was.active || s.active2 != g_was.active2 || (walking && memcmp(s.dest, g_was.dest, 12))) {
		Log("ftpre: travelling %d/%d (was %d/%d) dest %.1f %.1f %.1f route %zu points, last %.1f %.1f %.1f%s",
			s.active, s.active2, g_was.active, g_was.active2, s.dest[0], s.dest[1], s.dest[2], s.route,
			s.last[0], s.last[1], s.last[2], walking && !wasWalking ? "  << walk starts" : "");
		if (walking && !wasWalking) { g_walkStart = now; g_adds = 0; g_lastAdd = 0; }
		if (!walking && wasWalking)
			Log("ftpre: walk ended after %.1f s, %d precache requests", (now - g_walkStart) / 1000.0, g_adds);
	}
	g_was = s;
	// only on the confirmed travel flag: the second state (+0x110 +0x68) is logged until a trip shows what it is
	if (g_mode < 2 || !s.active || !g_add || !Plausible(s.dest)) return;
	if (g_lastAdd && now - g_lastAdd < kRefreshMs) return;
	g_lastAdd = now;
	float pos[3] = { s.dest[0], s.dest[1], s.dest[2] + 1.8f };   // eye height over the route's end
	float dir[3] = { 0, 1, 0 };
	if (s.route >= 2) {   // facing along the last stretch of the route
		float dx = s.last[0] - s.prev[0], dy = s.last[1] - s.prev[1], l = sqrtf(dx * dx + dy * dy);
		if (l > 0.01f) { dir[0] = dx / l; dir[1] = dy / l; }
	}
	bool ok = AddPoint(pos, dir);
	g_adds++;
	if (g_adds <= 2 || !ok) Log("ftpre: precache point %s at %.1f %.1f %.1f (request %d)", ok ? "added" : "FAILED", pos[0], pos[1], pos[2], g_adds);
	if (!ok) g_add = nullptr;   // never again this session
}
