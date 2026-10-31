// Fast-travel preload: see ftpreload.h. Addresses: WHGame.dll 1.5.6 (docs/whgame_re/fast_travel.md).
#include "ftpreload.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <string>
#include "MinHook.h"

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

// "fttex MB JOBS" (fastload_fttex.txt; 0 0 = off, the default): the texture streamer's outstanding-request caps,
// r_TexturesStreamingMaxRequestedMB (float, 4.0) and ...Jobs (int, 32), raised while the arrival wait runs (the
// "AfterFastTravel" readiness observer, +0x110 +0x68, after the walk), put back when it ends. The scheduler
// FUN_18085f70c reads both every rendered frame (wh.c 1659560-1659600: MB * 1024 * 1024 against the outstanding bytes
// DAT_1855940e0, jobs against DAT_1855940e8), so a write takes effect the next frame. Measured 2026-10-26 (the user's
// trips): in the wait's first second 1-2 requests out at exactly 4.0 MB while the disk reader idled 34-40 %.
constexpr uintptr_t kRvaTexMB = 0x50dc1b8, kRvaTexJobs = 0x50dc1b4;
// Third number "fttex MB JOBS NPC": the MonsterLOD switch budget (NPCs switched in or out of MonsterLOD a frame) raised
// to NPC for the same wait. The arrival's "LODManager" readiness task (C_LODManager, IsReady FUN_18209c850) waits for
// the destination's NPCs to leave MonsterLOD; the switcher = *(*(game+0xe0)+0x1a0) drains its queues (+8 enter, +0x20
// leave) in FUN_181e7dda0 with the budget struct at +0x50 {current, min, max, up, down, u8 flush}; FUN_181e1bf50 starts
// it at min and grows it by 1 a frame while the queue is long (wh_actor_MonsterLODSwitchBudget*). The game's loading
// (PrecacheMode) path flushes the whole queue in one frame; min = max = NPC does the same in batches of NPC.
constexpr size_t kNpcSwitcher = 0x1a0, kNpcBudget = 0x50;

using AddPrecacheFn = void* (__fastcall*)(void* engine, void* outFuture, const float* pos, const float* dir, float timeout,
                                          float importance);

constexpr float kTimeout = 4.0f;       // s a point lives
constexpr DWORD kRefreshMs = 2500;     // re-added while the walk runs, so it lapses ~4 s after the walk stops

uintptr_t g_env = 0, g_base = 0, g_end = 0;
int g_mode = 2;
bool g_ok = false, g_tried = false;    // the code checks passed / ran
AddPrecacheFn g_add = nullptr;
uintptr_t g_engine = 0;

struct State { bool valid; uint8_t active, active2; float dest[3]; size_t route; float last[3], prev[3]; float speed, left; };
State g_was{};
DWORD g_lastAdd = 0, g_walkStart = 0;
int g_adds = 0;
// Walk speed (user 2026-10-30: the map marker seemed much faster in Trosky than in Kuttenberg): the route's next point
// steps along as the walk eats the route, so the sum of its steps is the distance walked; frames counted beside it
// tell a speed in metres a second from one in metres a frame (tied to the frame rate).
int g_walkFrames = 0;
double g_walkDist = 0;
float g_walkLast[3] = {};
// From the game's own counter: the game time each frame's step used (step / speed), against the real frame time.
float g_walkSpeed = 0, g_walkLen = 0, g_walkPrevLeft = 0;
double g_walkGameT = 0, g_walkMaxDt = 0, g_walkMinDt = 1e9;
long long g_walkQpcStart = 0;

// "ftinstant S" (fastload_ftinstant.txt; 0 = off, the default): S seconds after the arrival wait starts, the
// "AfterFastTravel" observer (ft+0x110: +0x18 mask 0x3f91, +0x20 expiry, +0x68 armed) is let in through the game's own
// timeout path (expiry 0, as wh_sys_ReadinessObserverTimeoutDuration running out; readiness.cpp LetIn does the same for
// its "early" gates): the fade-in starts while NPC detail (LODManager) and texture mips still arrive. User 2026-10-26:
// "why cant it be instant".
std::string g_instFile;
float g_instS = 0;
DWORD g_waitStart = 0;
bool g_instDone = false;

int LetInFt(uintptr_t obs) {   // 0 done, 1 not armed, 2 layout mismatch, 3 unreadable
	__try {
		uint8_t* p = (uint8_t*)obs;
		if (p[0x68] != 1) return 1;
		if (*(uint32_t*)(p + 0x18) != 0x3f91) return 2;
		const long long e = *(long long*)(p + 0x20);
		if (e < 0 && e != (long long)0xfffffffffffe7960ull) return 2;
		*(volatile long long*)(p + 0x20) = 0;
		return 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return 3; }
}

uintptr_t FtObserver() {
	__try {
		uintptr_t game = *(uintptr_t*)(g_base + kRvaGame);
		uintptr_t sub = game ? *(uintptr_t*)(game + 0x128) : 0;
		uintptr_t m = sub ? *(uintptr_t*)(sub + 0x28) : 0;
		return m ? *(uintptr_t*)(m + 0x110) : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

std::string g_texFile;
float g_texMB = 0;            // 0 = off
int g_texJobs = 0;
int g_npc = 0;                // 0 = off
uint32_t* g_npcAt = nullptr;  // the budget struct we raised
uint32_t g_npcOrig[2] = {}, g_npcSet = 0;
bool g_gateBoost = false;
unsigned g_tripSeq = 0;
DWORD g_tripBusyAt = 0;     // last frame a walk or arrival wait was on (keepload waits for the trip to be long over)   // fast-travel walks started (keepload: a map that saw one reloads)   // raised for a gate in play (NpcGate), not the arrival
bool g_texRaised = false;
float g_texOrigMB = 0, g_texSetMB = 0;
int g_texOrigJobs = 0, g_texSetJobs = 0;
DWORD g_texSince = 0;

uint32_t* NpcBudget() {
	__try {
		uintptr_t game = *(uintptr_t*)(g_base + kRvaGame);
		uintptr_t mod = game ? *(uintptr_t*)(game + 0xe0) : 0;
		uintptr_t sw = mod ? *(uintptr_t*)(mod + kNpcSwitcher) : 0;
		if (!sw) return nullptr;
		uint32_t* b = (uint32_t*)(sw + kNpcBudget);
		if (b[1] > 100000 || b[2] > 100000) return nullptr;   // min / max not plausible: not this struct
		return b;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

void NpcRaise() {
	if (g_npcAt || g_npc <= 0 || !g_base) return;
	uint32_t* b = NpcBudget();
	if (!b) { Log("fttex: the MonsterLOD switcher isn't there: NPC budget off"); g_npc = 0; return; }
	__try {
		g_npcOrig[0] = b[1]; g_npcOrig[1] = b[2];
		g_npcSet = (uint32_t)g_npc;
		b[1] = g_npcSet; b[2] = g_npcSet;   // FUN_181e1bf50 then sets current = max the next frame
		g_npcAt = b;
		Log("fttex: MonsterLOD switch budget min %u / max %u (current %u) -> %u", g_npcOrig[0], g_npcOrig[1],
		    b[0], g_npcSet);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { g_npc = 0; }
}

void NpcRestore() {
	if (!g_npcAt) return;
	__try {
		if (g_npcAt[1] == g_npcSet) g_npcAt[1] = g_npcOrig[0];
		if (g_npcAt[2] == g_npcSet) g_npcAt[2] = g_npcOrig[1];
		Log("fttex: MonsterLOD switch budget back to min %u / max %u", g_npcAt[1], g_npcAt[2]);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
	g_npcAt = nullptr;
}

void TexRaise() {
	if (g_gateBoost) { NpcRestore(); g_gateBoost = false; }   // the arrival's own numbers take over
	NpcRaise();
	if (g_texRaised || g_texMB <= 0 || !g_base) return;
	__try {
		float* mb = (float*)(g_base + kRvaTexMB);
		int* jobs = (int*)(g_base + kRvaTexJobs);
		if (!(*mb > 0.0f && *mb <= 4096.0f) || *jobs < 1 || *jobs > 1 << 20) {
			Log("fttex: the caps read %.2f MB / %d jobs: not the 1.5.6 variables, off", *mb, *jobs);
			g_texMB = 0;
			return;
		}
		g_texOrigMB = *mb; g_texOrigJobs = *jobs;
		g_texSetMB = g_texMB > g_texOrigMB ? g_texMB : g_texOrigMB;
		g_texSetJobs = g_texJobs > g_texOrigJobs ? g_texJobs : g_texOrigJobs;
		*mb = g_texSetMB; *jobs = g_texSetJobs;
		g_texRaised = true;
		g_texSince = GetTickCount();
		Log("fttex: arrival wait: texture caps %.1f MB / %d jobs -> %.1f MB / %d jobs", g_texOrigMB, g_texOrigJobs,
		    g_texSetMB, g_texSetJobs);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { g_texMB = 0; }
}

// The same MonsterLOD switch budget while any 0x3f91 gate holds the screen in play (readiness.cpp): sleep / wait
// (+fc367f) held 1.3-1.9 s for "LODManager" alone at 70-90 fps (2026-10-27 14:42, the user: "when the clock stops
// spinning, it then does the loading circle and its a brief wait"); the NPCs moved by the time skip leave MonsterLOD
// a few a frame. kGateNpc a frame, or the "fttex" NPC number if that is higher; only what we wrote is put back.
constexpr int kGateNpc = 64;
void NpcGate(bool on) {
	if (on == g_gateBoost) return;
	if (on) {
		if (g_npcAt) return;   // the arrival wait already raised it
		const int keep = g_npc;
		g_npc = keep > kGateNpc ? keep : kGateNpc;
		NpcRaise();
		g_npc = keep;
		g_gateBoost = g_npcAt != nullptr;
		if (g_gateBoost) Log("fttex: (a gate in play holds the screen)");
	} else {
		NpcRestore();
		g_gateBoost = false;
	}
}

void TexRestore(const char* why) {
	if (!g_gateBoost) NpcRestore();   // a gate in play owns the raise (NpcGate): it puts it back itself (212 lost it a frame later)
	if (!g_texRaised) return;
	g_texRaised = false;
	__try {
		float* mb = (float*)(g_base + kRvaTexMB);
		int* jobs = (int*)(g_base + kRvaTexJobs);
		// only what we wrote (someone may have set the cvar meanwhile: theirs stays)
		if (*mb == g_texSetMB) *mb = g_texOrigMB;
		if (*jobs == g_texSetJobs) *jobs = g_texOrigJobs;
		Log("fttex: caps back to %.1f MB / %d jobs after %.1f s (%s)", *mb, *jobs, (GetTickCount() - g_texSince) / 1000.0, why);
	}
	__except (EXCEPTION_EXECUTE_HANDLER) {}
}

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
		// the walk step FUN_182de3148(mgr, dt): step = dt * speed (+0x50, the rider's movement speed, FUN_182dde52c),
		// +0x4c -= step (+0x4c = the route's length when it is built, FUN_182dde2a4)
		s.speed = *(float*)(m + 0x50);
		s.left = *(float*)(m + 0x4c);
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


// ---- ftspeed: the fast-travel piece at the same speed at any frame rate ----
// The walk step FUN_182de3148(mgr, dt) moves the piece dt * rider speed (+0x50) along the route, once a frame, and the
// game hands it at most 0.5 s of game time a frame. So the piece moves 0.5 s * 3.84 m/s = 1.91 m a frame and its speed
// is the frame rate: 2026-10-30 log, 160-170 m/s at 81-89 fps, 87 m/s at 46, 69 m/s at 36 fps (Kuttenberg runs lower).
// With ftspeed R, a frame at the full step (dt >= 0.45 s) gets max(dt, real frame time * R) game seconds, at most 2 s:
// R = 44 is the 88 fps speed everywhere. Smaller steps (the walk's start, a random event's slow-down) stay the game's.
using WalkStepFn = char(__fastcall*)(void* mgr, float dt);
WalkStepFn o_walkStep = nullptr;
float g_ftspeed = 44.f;            // game seconds per real second while walking; 0 = the game's own step
std::string g_spdFile;
long long g_spdPrev = 0;           // QPC of the last walk step
double g_qpcF = 0;
const uint8_t kWalkStep[] = {0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x57};

char __fastcall HkWalkStep(void* mgr, float dt) {
	LARGE_INTEGER q; QueryPerformanceCounter(&q);
	const double real = g_spdPrev ? (q.QuadPart - g_spdPrev) / g_qpcF : 0;
	g_spdPrev = q.QuadPart;
	if (g_ftspeed > 0 && dt >= 0.45f && real > 0 && real < 0.25) {
		const double want = real * g_ftspeed;
		if (want > dt) dt = (float)(want < 2.0 ? want : 2.0);
	}
	return o_walkStep(mgr, dt);
}

bool BytesMatch(const void* fn) {
	__try { return memcmp(fn, kWalkStep, sizeof kWalkStep) == 0; } __except (1) { return false; }
}

void SaveSpeed() {
	if (g_spdFile.empty()) return;
	if (FILE* f = fopen(g_spdFile.c_str(), "w")) { fprintf(f, "%g\n", g_ftspeed); fclose(f); }
}
}  // namespace

void Ftpre_Install(uintptr_t env, uintptr_t) { g_env = env; }   // Setup waits for the first drawn frame (the 3D engine is up)

void Fttex_Install(HMODULE self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string p(path);
	g_texFile = p.substr(0, p.find_last_of('\\') + 1) + "fastload_fttex.txt";
	if (FILE* f = fopen(g_texFile.c_str(), "r")) {
		float mb = 0; int jobs = 0, npc = 0;
		if (fscanf(f, "%f %d %d", &mb, &jobs, &npc) >= 1 && mb >= 0 && mb <= 1024) {
			g_texMB = mb; g_texJobs = jobs < 0 ? 0 : jobs; g_npc = npc < 0 ? 0 : npc > 10000 ? 10000 : npc;
		}
		fclose(f);
	}
	if (g_texMB > 0 || g_npc > 0)
		Log("fttex: arrival wait texture caps %.1f MB / %d jobs, NPC switch budget %d (fastload_fttex.txt)", g_texMB, g_texJobs, g_npc);
}

void Ftinstant_Install(HMODULE self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string p(path);
	g_instFile = p.substr(0, p.find_last_of('\\') + 1) + "fastload_ftinstant.txt";
	if (FILE* f = fopen(g_instFile.c_str(), "r")) {
		float v = 0;
		if (fscanf(f, "%f", &v) == 1 && v >= 0 && v <= 30) g_instS = v;
		fclose(f);
	}
	if (g_instS > 0) Log("ftinstant: the fast-travel arrival wait is let in after %.2f s (fastload_ftinstant.txt)", g_instS);
}

void Ftinstant_Command(const char* args) {
	float v = 0;
	if (args && sscanf_s(args, "%f", &v) == 1 && v >= 0 && v <= 30) {
		g_instS = v;
		if (!g_instFile.empty()) {
			if (v <= 0) remove(g_instFile.c_str());
			else if (FILE* f = fopen(g_instFile.c_str(), "w")) { fprintf(f, "%g\n", v); fclose(f); }
		}
	}
	Log("ftinstant: %s  (ftinstant S: let the fast-travel arrival in S seconds after it starts waiting; 0 = off)",
	    g_instS > 0 ? "on" : "off");
}

void Fttex_Command(const char* args) {
	float mb = 0; int jobs = 0, npc = 0;
	if (args && sscanf_s(args, "%f %d %d", &mb, &jobs, &npc) >= 1 && mb >= 0 && mb <= 1024) {
		g_texMB = mb; g_texJobs = jobs < 0 ? 0 : jobs; g_npc = npc < 0 ? 0 : npc > 10000 ? 10000 : npc;
		if (!g_texFile.empty()) if (FILE* f = fopen(g_texFile.c_str(), "w")) { fprintf(f, "%g %d %d\n", g_texMB, g_texJobs, g_npc); fclose(f); }
		if (g_texMB <= 0 && g_npc <= 0) TexRestore("turned off");
	}
	Log("fttex: texture caps %.1f MB / %d jobs, NPC switch budget %d while the fast-travel arrival waits  "
	    "(fttex MB JOBS NPC; fttex 0 = off)", g_texMB, g_texJobs, g_npc);
}

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
	if (!ReadState(s)) {
		if (g_was.valid) Log("ftpre: the fast-travel manager went away");
		g_was = State{};
		TexRestore("the fast-travel manager went away");
		return;
	}
	DWORD now = GetTickCount();
	bool walking = s.active || s.active2;
	bool wasWalking = g_was.active || g_was.active2;
	if (s.active != g_was.active || s.active2 != g_was.active2 || (walking && memcmp(s.dest, g_was.dest, 12))) {
		Log("ftpre: travelling %d/%d (was %d/%d) dest %.1f %.1f %.1f route %zu points, last %.1f %.1f %.1f%s",
			s.active, s.active2, g_was.active, g_was.active2, s.dest[0], s.dest[1], s.dest[2], s.route,
			s.last[0], s.last[1], s.last[2], walking && !wasWalking ? "  << walk starts" : "");
		if (walking && !wasWalking) {
			g_walkStart = now; g_adds = 0; g_lastAdd = 0; g_tripSeq++;
			g_walkFrames = 0; g_walkDist = 0; memcpy(g_walkLast, s.last, sizeof g_walkLast);
			g_walkSpeed = s.speed; g_walkLen = s.left; g_walkPrevLeft = s.left; g_walkGameT = 0; g_walkMaxDt = 0; g_walkMinDt = 1e9;
			LARGE_INTEGER q; QueryPerformanceCounter(&q); g_walkQpcStart = q.QuadPart;
		}
		if (!walking && wasWalking) {
			const double sec = (now - g_walkStart) / 1000.0;
			Log("ftpre: walk ended after %.1f s, %d precache requests", sec, g_adds);
			if (g_walkFrames > 0 && sec > 0)
				Log("ftpre: walk speed: %.0f m of route in %.1f s = %.0f m/s; %d frames (%.0f fps) = %.2f m a frame",
				    g_walkDist, sec, g_walkDist / sec, g_walkFrames, g_walkFrames / sec, g_walkDist / g_walkFrames);
			if (g_walkFrames > 0 && g_walkSpeed > 0) {
				LARGE_INTEGER q, f; QueryPerformanceCounter(&q); QueryPerformanceFrequency(&f);
				const double real = (double)(q.QuadPart - g_walkQpcStart) / f.QuadPart;
				Log("ftpre: walk speed (game's counter): rider speed %.2f m/s, route %.0f m; game time walked %.2f s in %.2f s "
				    "real (x%.1f); game time a frame %.4f s average, %.4f min, %.4f max; real frame %.4f s average",
				    g_walkSpeed, g_walkLen, g_walkGameT, real, real > 0 ? g_walkGameT / real : 0, g_walkGameT / g_walkFrames,
				    g_walkMinDt < 1e9 ? g_walkMinDt : 0, g_walkMaxDt, real / g_walkFrames);
			}
		}
	}
	if (s.active && wasWalking && s.route) {
		g_walkFrames++;
		const float dx = s.last[0] - g_walkLast[0], dy = s.last[1] - g_walkLast[1], dz = s.last[2] - g_walkLast[2];
		g_walkDist += sqrt((double)dx * dx + (double)dy * dy + (double)dz * dz);
		memcpy(g_walkLast, s.last, sizeof g_walkLast);
		if (g_walkSpeed <= 0) g_walkSpeed = s.speed;   // set at the start; read again in case it came a frame late
		if (g_walkLen <= 0) g_walkLen = s.left;
		const float step = g_walkPrevLeft - s.left;   // this frame's step (0 when the walk didn't tick)
		if (g_walkSpeed > 0 && step > 0) {
			const double dt = step / g_walkSpeed;
			g_walkGameT += dt;
			if (dt > g_walkMaxDt) g_walkMaxDt = dt;
			if (dt < g_walkMinDt) g_walkMinDt = dt;
		}
		g_walkPrevLeft = s.left;
	}
	g_was = s;
	if (s.active || s.active2) g_tripBusyAt = now;
	if (s.active2 && !s.active) {   // the arrival wait: the walk is over, the readiness observer is armed
		TexRaise();
		if (!g_waitStart) { g_waitStart = now; g_instDone = false; }
		if (g_instS > 0 && !g_instDone && now - g_waitStart >= (DWORD)(g_instS * 1000)) {
			g_instDone = true;
			int r = LetInFt(FtObserver());
			Log("ftinstant: arrival wait let in after %.2f s (%s)", (now - g_waitStart) / 1000.0,
			    r == 0 ? "the fade-in starts on the next update" : r == 1 ? "already fired" : r == 2 ? "the observer's layout didn't match: left alone" : "unreadable");
		}
	} else {
		TexRestore(s.active ? "a new walk" : "the wait ended");
		g_waitStart = 0;
	}
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

void Ftpre_GateNpc(bool on) { NpcGate(on); }
unsigned Ftpre_TripSeq() { return g_tripSeq; }
DWORD Ftpre_TripQuietMs() { return g_tripBusyAt ? GetTickCount() - g_tripBusyAt : 0xffffffffu; }

void Ftspeed_Install(uintptr_t base, HMODULE self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string p(path);
	g_spdFile = p.substr(0, p.find_last_of('\\') + 1) + "fastload_ftspeed.txt";
	if (FILE* f = fopen(g_spdFile.c_str(), "r")) {
		float v = 0;
		if (fscanf(f, "%f", &v) == 1 && v >= 0 && v <= 200) g_ftspeed = v;
		fclose(f);
	}
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_qpcF = (double)f.QuadPart;
	void* fn = (void*)(base + 0x2de3148);
	if (!BytesMatch(fn)) { Log("ftspeed: the walk step's bytes differ (another game version?): not hooked"); return; }
	if (MH_CreateHook(fn, (void*)&HkWalkStep, (void**)&o_walkStep) != MH_OK || MH_EnableHook(fn) != MH_OK) {
		Log("ftspeed: hook FAILED"); return;
	}
	Log("ftspeed: %g game seconds per real second while the fast-travel piece walks (%s; fastload_ftspeed.txt, 0 = the game's 0.5 s a frame)",
	    g_ftspeed, g_ftspeed > 0 ? "the 88 fps speed at any frame rate" : "off");
}

void Ftspeed_Command(const char* args) {
	float v = 0;
	if (args && sscanf_s(args, "%f", &v) == 1 && v >= 0 && v <= 200) { g_ftspeed = v; SaveSpeed(); }
	Log("ftspeed: %g  (ftspeed R: the fast-travel piece walks R game seconds per real second, at most 2 s a frame; "
	    "44 = the 88 fps speed everywhere (default); 0 = the game's own 0.5 s a frame)", g_ftspeed);
}
