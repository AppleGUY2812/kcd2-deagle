// The native round simulation's plugin side (docs/ARCHITECTURE.md step 6, Claude 5). native/sim/sim.cpp moves the
// rounds; this file is its Lua binding (DN.*, the signatures in ARCHITECTURE step 6; ballistics.lua's roundSim native
// path calls them) and KCD2's world behind the sim's callbacks, as the engine's own Lua calls do it:
//   ray     Physics.RayWorldIntersection: IPhysicalWorld (gEnv+0x30) vt+0x118, flags 0x1000F, objtypes 0x11F, the player
//           and the round's skip entity skipped (docs/whgame_re/sim_binding.md section 1-2)
//   water   CryAction.GetWaterInfo: IPhysicalWorld vt+0x270 CheckAreas, the first area with iMedium 0 (section 3)
//   living  FlyBy's Physics.SamplePhysEnvironment(ent_living) + its filter: GetEntitiesInBox vt+0x110, the entity of
//           each, not the player, an actor that isn't dead, its world position (docs/whgame_re/sim_world.md)
//   clock   System.GetCurrTime's timer (gEnv+0x80) vt+0x20: the same game clock in double
//   tracers Claude 4's Fx_TracerRestart (fx.h), ids FX_TRACE_BASE + round
// Everything runs on the Lua thread, inside DN.* calls; nothing per frame of its own. Every engine call is under SEH, and
// each kind logs once, on its first use, whether it worked.
#include "rounds.h"
#include "fx.h"
#include "log.h"
#include "../sim/sim.h"
#include <windows.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace {

uintptr_t g_env = 0;
LARGE_INTEGER g_freq{};

bool Rd(uintptr_t a, uintptr_t& out) {
	__try { out = *(uintptr_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
uintptr_t VF(uintptr_t obj, int off) { uintptr_t vt; return obj && Rd(obj, vt) && Rd(vt + off, vt) ? vt : 0; }
uintptr_t Env(int off) { uintptr_t p = 0; return g_env && Rd(g_env + off, p) ? p : 0; }

// first use of each engine call: logged once, ok or not
enum { U_RAY, U_WATER, U_LIVING, U_ACTOR, U_CLOCK, U_SKIP, U_TRACER, U_INIT, U_COUNT };
const char* const U_NAME[U_COUNT] = { "ray", "water (CheckAreas)", "living (GetEntitiesInBox)", "actor IsDead", "clock",
	"skip entity (GetEntity / GetPhysics)", "tracer (Fx_TracerRestart)", "simInit table" };
bool g_said[U_COUNT][2] = {};
void Once(int u, bool ok, const char* detail = "") {
	if (g_said[u][ok]) return;
	g_said[u][ok] = true;
	Log("rounds: first %s %s%s%s", U_NAME[u], ok ? "ok" : "FAILED", *detail ? ": " : "", detail);
}

// what the world calls cost (rounds stats)
struct Cost { uint64_t n = 0; int64_t ticks = 0; };
Cost g_cost[U_COUNT];
struct Timed {
	int u; LARGE_INTEGER t0;
	explicit Timed(int k) : u(k) { QueryPerformanceCounter(&t0); }
	~Timed() { LARGE_INTEGER t1; QueryPerformanceCounter(&t1); g_cost[u].n++; g_cost[u].ticks += t1.QuadPart - t0.QuadPart; }
};

// ---- Lua values (docs/whgame_re/script_binding.md): ScriptAnyValue 0x18 bytes, type +0, value +8
struct Any {
	int type; int pad;
	union { float n; bool b; const char* s; uintptr_t p; float v[3]; };
};
static_assert(sizeof(Any) == 0x18, "ScriptAnyValue is 0x18 bytes");
enum { T_ANY = 0, T_NIL = 1, T_BOOL = 2, T_HANDLE = 3, T_NUM = 4, T_STR = 5, T_TABLE = 6, T_VEC = 9 };
using GetAnyFn = bool(__fastcall*)(uintptr_t pH, int idx, Any* a);
using GetCountFn = int(__fastcall*)(uintptr_t pH);
using End0Fn = int(__fastcall*)(uintptr_t pH);
using End1Fn = int(__fastcall*)(uintptr_t pH, const Any* a);
using End2Fn = int(__fastcall*)(uintptr_t pH, const Any* a, const Any* b);
using TSetFn = void(__fastcall*)(uintptr_t t, const char* key, const Any* a, bool chain);   // IScriptTable vt+0x38
using TGetFn = bool(__fastcall*)(uintptr_t t, const char* key, Any* a, bool chain);         // vt+0x40
using TSetAtFn = void(__fastcall*)(uintptr_t t, int i, const Any* a);                       // vt+0x68
using TGetAtFn = bool(__fastcall*)(uintptr_t t, int i, Any* a);                             // vt+0x70 (CryEngine order)
using TRefFn = void(__fastcall*)(uintptr_t t);                                              // +0x18 AddRef, +0x20 Release

bool Param(uintptr_t pH, int i, Any& a, int type) {
	a = Any{}; a.type = type;
	auto get = (GetAnyFn)VF(pH, 0x38);
	return get && get(pH, i, &a);
}
bool Num(uintptr_t pH, int i, float& out) { Any a; if (!Param(pH, i, a, T_NUM)) return false; out = a.n; return true; }
// a flag: a boolean, or a number (non-zero)
bool Flag(uintptr_t pH, int i) {
	Any a;
	if (!Param(pH, i, a, T_ANY)) return false;
	return a.type == T_BOOL ? a.b : a.type == T_NUM ? a.n != 0 : false;
}
// an entity id (ScriptHandle): 0 when nil
uint32_t Handle(uintptr_t pH, int i) { Any a; return Param(pH, i, a, T_HANDLE) ? (uint32_t)a.p : 0; }
int Ret0(uintptr_t pH) { auto e = (End0Fn)VF(pH, 0x58); return e ? e(pH) : 0; }
int Ret(uintptr_t pH, const Any& a) { auto e = (End1Fn)VF(pH, 0x50); return e ? e(pH, &a) : 0; }
int RetNum(uintptr_t pH, double x) { Any a{}; a.type = T_NUM; a.n = (float)x; return Ret(pH, a); }
int Fail(uintptr_t pH, const char* msg) {
	Any nil{}; nil.type = T_NIL; Any m{}; m.type = T_STR; m.s = msg;
	auto e = (End2Fn)VF(pH, 0x48);
	return e ? e(pH, &nil, &m) : 0;
}
void TSetNum(uintptr_t t, const char* k, double x) { Any a{}; a.type = T_NUM; a.n = (float)x; ((TSetFn)VF(t, 0x38))(t, k, &a, false); }
void TSetVec(uintptr_t t, const char* k, const sim_vec& v) {
	Any a{}; a.type = T_VEC; a.v[0] = (float)v.x; a.v[1] = (float)v.y; a.v[2] = (float)v.z;
	((TSetFn)VF(t, 0x38))(t, k, &a, false);
}
void TSetStr(uintptr_t t, const char* k, const char* s) { Any a{}; a.type = T_STR; a.s = s; ((TSetFn)VF(t, 0x38))(t, k, &a, false); }
void TSetHandle(uintptr_t t, const char* k, uint32_t id) { Any a{}; a.type = T_HANDLE; a.p = id; ((TSetFn)VF(t, 0x38))(t, k, &a, false); }
bool TGetNum(uintptr_t t, const char* k, double& out) {
	Any a{}; a.type = T_NUM;
	auto get = (TGetFn)VF(t, 0x40);
	if (!get || !get(t, k, &a, false)) return false;
	out = a.n;
	return true;
}
// a new Lua table, referenced (Release it once it's handed to Lua or stored)
uintptr_t NewTable() {
	uintptr_t ss = Env(0x28);
	auto create = (uintptr_t(__fastcall*)(uintptr_t, bool))VF(ss, 0x68);
	uintptr_t t = create ? create(ss, false) : 0;
	if (t) ((TRefFn)VF(t, 0x18))(t);
	return t;
}
void Release(uintptr_t t) { if (t) ((TRefFn)VF(t, 0x20))(t); }

// ---- the world
sim* g_sim = nullptr;
std::vector<std::string> g_fx;           // tracer effect names by the sim's fx id - 1
uint32_t g_player = 0;                   // the player's entity id (simInit)
uintptr_t g_playerPhys = 0;              // its physical entity, resolved at each DN call that moves rounds
double g_now = 0;

uintptr_t Physics(uint32_t entityId) {   // IEntitySystem (gEnv+0xA0) vt+0x70 GetEntity -> IEntity vt+0x278 GetPhysics
	if (!entityId) return 0;
	__try {
		uintptr_t es = Env(0xA0);
		auto get = (uintptr_t(__fastcall*)(uintptr_t, uint32_t))VF(es, 0x70);
		uintptr_t e = get ? get(es, entityId) : 0;
		auto phys = (uintptr_t(__fastcall*)(uintptr_t))VF(e, 0x278);
		uintptr_t p = phys ? phys(e) : 0;
		Once(U_SKIP, p != 0);
		return p;
	} __except (EXCEPTION_EXECUTE_HANDLER) { Once(U_SKIP, false, "exception"); return 0; }
}
uint32_t EntityOf(uintptr_t pe) {        // IPhysicalEntity vt+0x50 GetForeignData(2 = entity) -> IEntity, its id at +0xC
	if (!pe) return 0;
	auto fd = (uintptr_t(__fastcall*)(uintptr_t, int))VF(pe, 0x50);
	uintptr_t e = fd ? fd(pe, 2) : 0;
	return e ? *(uint32_t*)(e + 0xC) : 0;
}

int RayInner(const sim_vec* from, const sim_vec* move, uint64_t skip, sim_ray_hit* out) {
	__try {
		uintptr_t w = Env(0x30);
		auto rwi = (int(__fastcall*)(uintptr_t, const uint8_t*, const char*, int))VF(w, 0x118);
		if (!rwi) { Once(U_RAY, false, "no IPhysicalWorld"); return -1; }
		uintptr_t skips[2]; int ns = 0;
		if (g_playerPhys) skips[ns++] = g_playerPhys;
		if (skip) if (uintptr_t p = Physics((uint32_t)skip)) skips[ns++] = p;   // packed, no NULLs (sim_binding.md: Lua's bug)
		alignas(16) uint8_t p[0x70] = {};
		alignas(16) uint8_t hit[0x50] = {};
		float org[3] = { (float)from->x, (float)from->y, (float)from->z }, dir[3] = { (float)move->x, (float)move->y, (float)move->z };
		memcpy(p + 0x18, org, 12); memcpy(p + 0x24, dir, 12);
		*(int*)(p + 0x30) = 0x11F; *(int*)(p + 0x34) = 0x1000F;
		*(void**)(p + 0x38) = hit; *(int*)(p + 0x40) = 1;
		*(int*)(p + 0x50) = ns; *(uintptr_t**)(p + 0x58) = skips;
		int n = rwi(w, p, "deagle_rounds", 4);
		Once(U_RAY, n >= 0);
		float dist = *(float*)hit;
		uintptr_t coll = *(uintptr_t*)(hit + 0x08);
		if (n <= 0 || dist < 0 || !coll) return 0;   // a hit with no object (some water): nothing, as Lua can't read it either
		out->dist = dist;
		const float* pt = (const float*)(hit + 0x24); const float* nm = (const float*)(hit + 0x30);
		out->pos = { pt[0], pt[1], pt[2] };
		out->normal = { nm[0], nm[1], nm[2] };
		out->surface = *(short*)(hit + 0x18);
		out->partid = *(int*)(hit + 0x14);
		out->entity = EntityOf(coll);
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) { Once(U_RAY, false, "exception"); return -1; }
}

int WaterInner(const sim_vec* at, double* level, sim_vec* normal) {
	__try {
		uintptr_t w = Env(0x30);
		using CheckAreasFn = int(__fastcall*)(uintptr_t, const float*, float*, uint8_t*, int, int, const float*, uintptr_t, int);
		auto check = (CheckAreasFn)VF(w, 0x270);
		if (!check) { Once(U_WATER, false, "no IPhysicalWorld"); return 0; }
		alignas(16) uint8_t pb[8][0x48];
		for (auto& b : pb) {   // pe_params_buoyancy as its constructor leaves it: type 12, floats unused, iMedium 0
			for (int o = 0; o < 0x44; o += 4) *(uint32_t*)(b + o) = 0xFFBFFFFF;
			*(int*)(b + 0x00) = 12;
			*(int*)(b + 0x44) = 0;
		}
		float pt[3] = { (float)at->x, (float)at->y, (float)at->z }, grav[3] = {}, vel[3] = {};
		int n = check(w, pt, grav, &pb[0][0], 8, -1, vel, 0, 4);
		Once(U_WATER, n >= 0);
		for (int i = 0; i < n && i < 8; i++) {
			if (*(int*)(pb[i] + 0x44) != 0) continue;
			const float* nm = (const float*)(pb[i] + 0x28);
			*level = *(const float*)(pb[i] + 0x3C);
			*normal = { nm[0], nm[1], nm[2] };
			return 1;
		}
		return 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { Once(U_WATER, false, "exception"); return 0; }
}

bool Alive(uint32_t id) {   // IsDead: gEnv+0x90 vt+0x80 IGameFramework -> vt+0xC8 actor system -> vt+0x18(id) -> vt+0x130
	__try {
		uintptr_t sys = Env(0x90);
		auto fwOf = (uintptr_t(__fastcall*)(uintptr_t))VF(sys, 0x80);
		uintptr_t fw = fwOf ? fwOf(sys) : 0;
		auto asOf = (uintptr_t(__fastcall*)(uintptr_t))VF(fw, 0xC8);
		uintptr_t as = asOf ? asOf(fw) : 0;
		auto actorOf = (uintptr_t(__fastcall*)(uintptr_t, uint32_t))VF(as, 0x18);
		uintptr_t a = actorOf ? actorOf(as, id) : 0;
		if (!a) return false;   // no actor: not a person (Lua's filter wants e.actor and e.soul)
		auto dead = (bool(__fastcall*)(uintptr_t))VF(a, 0x130);
		Once(U_ACTOR, dead != nullptr);
		return dead && !dead(a);
	} __except (EXCEPTION_EXECUTE_HANDLER) { Once(U_ACTOR, false, "exception"); return false; }
}

int LivingInner(const sim_vec* c, double radius, sim_living* out, int max) {
	__try {
		uintptr_t w = Env(0x30);
		using BoxFn = int(__fastcall*)(uintptr_t, const float*, const float*, uintptr_t**, int, int);
		auto box = (BoxFn)VF(w, 0x110);
		if (!box) { Once(U_LIVING, false, "no IPhysicalWorld"); return 0; }
		float lo[3] = { (float)(c->x - radius), (float)(c->y - radius), (float)(c->z - radius) };
		float hi[3] = { (float)(c->x + radius), (float)(c->y + radius), (float)(c->z + radius) };
		uintptr_t buf[256];
		uintptr_t* list = buf;
		int n = box(w, lo, hi, &list, 8 /* ent_living */, 256);   // past 256 the list is the engine's shared one: read now
		Once(U_LIVING, n >= 0);
		int k = 0;
		for (int i = 0; i < n && k < max; i++) {
			uint32_t id = EntityOf(list[i]);
			if (!id || id == g_player || !Alive(id)) continue;
			uintptr_t es = Env(0xA0);
			auto get = (uintptr_t(__fastcall*)(uintptr_t, uint32_t))VF(es, 0x70);
			uintptr_t e = get ? get(es, id) : 0;
			if (!e) continue;
			const float* m = (const float*)(e + 0x58);   // the world matrix: entity:GetWorldPos() reads m[3], m[7], m[11]
			out[k].entity = id;
			out[k].feet = { m[3], m[7], m[11] };
			k++;
		}
		return k;
	} __except (EXCEPTION_EXECUTE_HANDLER) { Once(U_LIVING, false, "exception"); return 0; }
}

int __cdecl WorldRay(void*, const sim_vec* from, const sim_vec* move, uint64_t skip, sim_ray_hit* out) {
	Timed tm(U_RAY);
	return RayInner(from, move, skip, out);
}
int __cdecl WorldWater(void*, const sim_vec* at, double* level, sim_vec* normal) {
	Timed tm(U_WATER);
	return WaterInner(at, level, normal);
}
int __cdecl WorldLiving(void*, const sim_vec* c, double radius, sim_living* out, int max) {
	Timed tm(U_LIVING);
	return LivingInner(c, radius, out, max);
}

bool TracerInner(int id, const char* fx, float count, const float* p, const float* d) {
	__try { return Fx_TracerRestart(id, fx, count, p, d); }
	__except (EXCEPTION_EXECUTE_HANDLER) { Once(U_TRACER, false, "exception"); return false; }
}
void __cdecl WorldDraw(void*, uint32_t round, int32_t fx, double count, double, const sim_vec* at, const sim_vec* dir) {
	if (fx < 1 || fx > (int32_t)g_fx.size()) return;
	float p[3] = { (float)at->x, (float)at->y, (float)at->z }, d[3] = { (float)dir->x, (float)dir->y, (float)dir->z };
	Timed tm(U_TRACER);
	bool ok = TracerInner(FX_TRACE_BASE + (int)round, g_fx[fx - 1].c_str(), (float)count, p, d);
	Once(U_TRACER, ok);
}

// the game clock (System.GetCurrTime's) in double, and the player's physics for this call's rays
void Tick() {
	__try {
		uintptr_t t = Env(0x80);
		auto now = (double(__fastcall*)(uintptr_t, int))VF(t, 0x20);
		if (now) g_now = now(t, 0);
		Once(U_CLOCK, now != nullptr);
	} __except (EXCEPTION_EXECUTE_HANDLER) { Once(U_CLOCK, false, "exception"); }
	g_playerPhys = Physics(g_player);
}

// ---- DN.* (signatures: docs/ARCHITECTURE.md step 6). Lua numbers are float32: round ids stay small, time never crosses
int SimInitImpl(uintptr_t pH) {
	Any t;
	if (!Param(pH, 1, t, T_TABLE) || !t.p) return Fail(pH, "DN.simInit(t): a table expected");
	uintptr_t tb = t.p;
	sim_config c{};
	double step = 0.05, life = 6, range = 1000, maxHops = 4, water = -1, minOn = 30, nearMiss = 0, chest = 1.25, gap = 0.75,
		aheadK = 0, aheadMin = 0, cap = 65536, gravity = 9.81;
	bool ok = TGetNum(tb, "step", step) & TGetNum(tb, "life", life) & TGetNum(tb, "range", range) & TGetNum(tb, "maxHops", maxHops)
		& TGetNum(tb, "minSpeedOn", minOn);
	TGetNum(tb, "waterSurface", water); TGetNum(tb, "nearMiss", nearMiss); TGetNum(tb, "chest", chest);
	TGetNum(tb, "flybyGap", gap); TGetNum(tb, "aheadK", aheadK); TGetNum(tb, "aheadMinDist", aheadMin);
	TGetNum(tb, "capacity", cap); TGetNum(tb, "gravity", gravity);
	Any pl{}; pl.type = T_HANDLE;
	auto get = (TGetFn)VF(tb, 0x40);
	g_player = get && get(tb, "player", &pl, false) ? (uint32_t)pl.p : 0;
	Once(U_INIT, ok, ok ? "" : "step / life / range / maxHops / minSpeedOn missing");
	if (!ok) { Release(tb); return Fail(pH, "DN.simInit: step, life, range, maxHops, minSpeedOn are needed"); }
	if (!g_player) Log("rounds: simInit without player (its rays won't skip Henry)");
	c.step = step; c.life = life; c.range = range; c.gravity = gravity; c.maxHops = (int32_t)maxHops;
	c.waterSurface = (int32_t)water; c.minSpeedOn = minOn; c.nearMiss = nearMiss; c.chest = chest; c.flybyGap = gap;
	c.aheadK = aheadK; c.aheadMinDist = aheadMin;
	if (g_sim) sim_destroy(g_sim);
	g_sim = sim_create((uint32_t)cap, &c);
	if (!g_sim) return Fail(pH, "DN.simInit: out of memory");
	sim_set_world(g_sim, &WorldRay, &WorldWater, nullptr);
	sim_set_living(g_sim, &WorldLiving);
	sim_set_draw(g_sim, &WorldDraw);
	// pass-through surfaces: t.pass = { ids }
	std::vector<int32_t> pass;
	Any ps{}; ps.type = T_TABLE;
	if (get && get(tb, "pass", &ps, false) && ps.p) {
		auto at = (TGetAtFn)VF(ps.p, 0x70);
		for (int i = 1; at && i <= 4096; i++) {
			Any v{}; v.type = T_NUM;
			if (!at(ps.p, i, &v)) break;
			pass.push_back((int32_t)v.n);
		}
		Release(ps.p);
	}
	sim_set_pass(g_sim, pass.data(), (int32_t)pass.size());
	Release(tb);
	g_fx.clear();
	Log("rounds: simInit: capacity %u, step %.3f s, life %.1f s, range %.0f m, %d pass-through surfaces, player %u, near miss "
		"%.2f m, look-ahead %.3f s from %.0f m", (unsigned)cap, step, life, range, (int)pass.size(), g_player, nearMiss, aheadK, aheadMin);
	Any yes{}; yes.type = T_BOOL; yes.b = true;
	return Ret(pH, yes);
}

int SimPhysImpl(uintptr_t pH) {
	float v, dl, tdl, b;
	if (!g_sim) return Fail(pH, "DN.simPhys: DN.simInit first");
	if (!Num(pH, 1, v) || !Num(pH, 2, dl) || !Num(pH, 3, tdl) || !Num(pH, 4, b)) return Fail(pH, "DN.simPhys(v, dragL, tumbleL, bullet)");
	sim_phys p{ v, dl, tdl, b };
	return RetNum(pH, sim_add_phys(g_sim, &p));
}

int FireImpl(uintptr_t pH) {
	float a[8];
	if (!g_sim) return Fail(pH, "DN.fire: DN.simInit first");
	for (int i = 0; i < 8; i++) if (!Num(pH, i + 1, a[i])) return Fail(pH, "DN.fire(px, py, pz, dx, dy, dz, phys, firstDt)");
	Tick();
	sim_vec pos{ a[0], a[1], a[2] }, dir{ a[3], a[4], a[5] };
	double l = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
	if (l > 0) { dir.x /= l; dir.y /= l; dir.z /= l; }   // a unit direction, in double (float32 from Lua)
	uint32_t id = sim_launch(g_sim, &pos, &dir, (int32_t)a[6], g_now, a[7]);
	if (id == UINT32_MAX) return Fail(pH, "full");
	return RetNum(pH, id);
}

int TracerImpl(uintptr_t pH) {
	float id, len, reach, ox, oy, oz, start, dash, anchor;
	Any fx;
	if (!g_sim || !Num(pH, 1, id) || !Param(pH, 2, fx, T_STR) || !fx.s || !Num(pH, 3, len) || !Num(pH, 4, reach) || !Num(pH, 5, ox)
		|| !Num(pH, 6, oy) || !Num(pH, 7, oz) || !Num(pH, 8, start) || !Num(pH, 9, dash) || !Num(pH, 10, anchor))
		return Fail(pH, "DN.tracer(id, fx, length, reach, ox, oy, oz, start, dash, anchor)");
	int k = 0;
	for (size_t i = 0; i < g_fx.size(); i++) if (g_fx[i] == fx.s) k = (int)i + 1;
	if (!k) { g_fx.push_back(fx.s); k = (int)g_fx.size(); }
	sim_vec off{ ox, oy, oz };
	sim_tracer(g_sim, (uint32_t)id, k, len, reach, &off, start, dash, anchor);
	return Ret0(pH);
}

int AheadImpl(uintptr_t pH) {
	if (g_sim) sim_set_ahead(g_sim, Flag(pH, 1) ? 1 : 0);
	return Ret0(pH);
}

int AdvanceImpl(uintptr_t pH) {
	if (!g_sim) return Fail(pH, "DN.advance: DN.simInit first");
	Tick();
	sim_advance(g_sim, g_now, 0.1);
	return Ret0(pH);
}

int EventsImpl(uintptr_t pH) {
	if (!g_sim) return Fail(pH, "DN.events: DN.simInit first");
	static const char* const KIND[] = { "", "hit", "done", "landed", "flyby", "ahead" };
	uint32_t n = 0;
	const sim_event* list = sim_events(g_sim, &n);
	uintptr_t out = NewTable();
	if (!out) return Fail(pH, "DN.events: no table");
	auto setAt = (TSetAtFn)VF(out, 0x68);
	for (uint32_t k = 0; k < n; k++) {
		const sim_event& e = list[k];
		uintptr_t ev = NewTable();
		if (!ev) break;
		TSetStr(ev, "kind", KIND[e.kind >= 1 && e.kind <= 5 ? e.kind : 0]);
		TSetNum(ev, "id", e.round);
		TSetVec(ev, "vel", e.state.vel);
		TSetNum(ev, "age", e.state.age);
		TSetNum(ev, "hops", e.state.hops);
		const sim_hit& h = e.hit;
		if (e.kind == SIM_EV_HIT || e.kind == SIM_EV_FLYBY || e.kind == SIM_EV_AHEAD) {
			TSetVec(ev, "pos", h.pos);
			TSetVec(ev, "dir", h.dir);
			if (h.entity) TSetHandle(ev, "entity", (uint32_t)h.entity);
			if (e.kind != SIM_EV_FLYBY) {
				TSetVec(ev, "normal", h.normal);
				if (h.surface >= 0) TSetNum(ev, "surface", h.surface);
				if (h.partid >= 0) TSetNum(ev, "partid", h.partid);
			}
			if (e.kind == SIM_EV_HIT) { TSetNum(ev, "dist", h.dist); TSetNum(ev, "energy", h.energy); }
			else TSetNum(ev, "dist", e.state.dist);
			if (e.kind == SIM_EV_AHEAD) TSetNum(ev, "hitDist", h.dist);
		} else {
			TSetVec(ev, "pos", e.state.pos);
			TSetNum(ev, "dist", e.state.dist);
		}
		Any a{}; a.type = T_TABLE; a.p = ev;
		setAt(out, (int)k + 1, &a);
		Release(ev);
	}
	sim_clear_events(g_sim);
	Any r{}; r.type = T_TABLE; r.p = out;
	int ret = Ret(pH, r);
	Release(out);
	return ret;
}

int AnswerImpl(uintptr_t pH) {
	float id, v[6];
	if (!g_sim || !Num(pH, 1, id)) return Fail(pH, "DN.answer(id, goesOn, px, py, pz, vx, vy, vz, skip, tumble, markHit, turned)");
	for (int i = 0; i < 6; i++) if (!Num(pH, i + 3, v[i])) return Fail(pH, "DN.answer: pos and vel as 6 numbers");
	Tick();
	sim_vec pos{ v[0], v[1], v[2] }, vel{ v[3], v[4], v[5] };
	sim_answer(g_sim, (uint32_t)id, Flag(pH, 2) ? 1 : 0, &pos, &vel, Handle(pH, 9), Flag(pH, 10) ? 1 : 0, Flag(pH, 11) ? 1 : 0,
		Flag(pH, 12) ? 1 : 0);
	return Ret0(pH);
}

// DN.round(id, t): the round's live fields into t (px py pz vx vy vz dist age hops); true, or nil when it's gone
int RoundImpl(uintptr_t pH) {
	float id;
	Any t;
	if (!g_sim || !Num(pH, 1, id) || !Param(pH, 2, t, T_TABLE) || !t.p) return Fail(pH, "DN.round(id, t)");
	sim_round r;
	if (!sim_get(g_sim, (uint32_t)id, &r)) { Release(t.p); Any nil{}; nil.type = T_NIL; return Ret(pH, nil); }
	TSetNum(t.p, "px", r.pos.x); TSetNum(t.p, "py", r.pos.y); TSetNum(t.p, "pz", r.pos.z);
	TSetNum(t.p, "vx", r.vel.x); TSetNum(t.p, "vy", r.vel.y); TSetNum(t.p, "vz", r.vel.z);
	TSetNum(t.p, "dist", r.dist); TSetNum(t.p, "age", r.age); TSetNum(t.p, "hops", r.hops);
	Release(t.p);
	Any yes{}; yes.type = T_BOOL; yes.b = true;
	return Ret(pH, yes);
}

// every DN function: its body under SEH (a bad engine pointer logs instead of taking the game down)
#define DN_FN(name, impl) \
	int __cdecl name(uintptr_t pH, void*, int) { \
		__try { return impl(pH); } \
		__except (EXCEPTION_EXECUTE_HANDLER) { Log("rounds: " #name " crashed (caught)"); return 0; } \
	}
DN_FN(DnSimInit, SimInitImpl)
DN_FN(DnSimPhys, SimPhysImpl)
DN_FN(DnFire, FireImpl)
DN_FN(DnTracer, TracerImpl)
DN_FN(DnAhead, AheadImpl)
DN_FN(DnAdvance, AdvanceImpl)
DN_FN(DnEvents, EventsImpl)
DN_FN(DnAnswer, AnswerImpl)
DN_FN(DnRound, RoundImpl)

uintptr_t g_dn = 0;
bool AddFn(uintptr_t table, const char* name, void* fn) {
	alignas(16) uint8_t desc[0x58] = {};
	static int marker = 0x444E;   // a non-empty data buffer: AddFunction copies it into the closure's userdata
	*(const char**)(desc + 0x00) = name;
	*(const char**)(desc + 0x08) = "";
	*(const char**)(desc + 0x10) = "DN";
	*(void**)(desc + 0x40) = fn;
	*(void**)(desc + 0x48) = &marker;
	*(int*)(desc + 0x50) = sizeof(marker);
	auto add = (bool(__fastcall*)(uintptr_t, const uint8_t*))VF(table, 0xB0);
	return add && add(table, desc);
}

int SafeBind() {
	__try {
		uintptr_t t = Dn_Table();
		if (!t) return -1;
		struct { const char* name; void* fn; } fns[] = {
			{ "simInit", (void*)&DnSimInit }, { "simPhys", (void*)&DnSimPhys }, { "fire", (void*)&DnFire },
			{ "tracer", (void*)&DnTracer }, { "ahead", (void*)&DnAhead }, { "advance", (void*)&DnAdvance },
			{ "events", (void*)&DnEvents }, { "answer", (void*)&DnAnswer }, { "round", (void*)&DnRound },
		};
		int ok = 0;
		for (auto& f : fns) ok += AddFn(t, f.name, f.fn);
		return ok;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}

void Stats() {
	for (int u = 0; u < U_COUNT; u++) {
		if (!g_cost[u].n) continue;
		double ms = g_cost[u].ticks * 1000.0 / (double)g_freq.QuadPart;
		Log("rounds stats: %-36s %8llu calls, %9.2f ms, %6.2f us each", U_NAME[u], (unsigned long long)g_cost[u].n, ms,
			ms * 1000.0 / (double)g_cost[u].n);
	}
	if (g_sim) {
		sim_stats st;
		sim_get_stats(g_sim, &st);
		Log("rounds stats: sim %llu steps, %llu rays, %llu hits, %u alive, %d tracer slots in use", (unsigned long long)st.steps,
			(unsigned long long)st.rays, (unsigned long long)st.hits, sim_alive(g_sim), Fx_TracersInUse());
	}
}

}  // namespace

uintptr_t Dn_Table() {
	if (g_dn) return g_dn;
	__try {
		uintptr_t ss = Env(0x28);
		uintptr_t t = NewTable();   // kept referenced for the session
		auto setGlobal = (void(__fastcall*)(uintptr_t, const char*, const Any*))VF(ss, 0xF8);
		if (!t || !setGlobal) return 0;
		Any g{}; g.type = T_TABLE; g.p = t;
		setGlobal(ss, "DN", &g);
		g_dn = t;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
	return g_dn;
}

void Rounds_Install(uintptr_t env, uintptr_t) {
	g_env = env;
	QueryPerformanceFrequency(&g_freq);
	Log("rounds: ready (nothing runs until \"rounds bind\")");
}

void Rounds_Command(const char* rest) {
	if (!strncmp(rest, "bind", 4)) {
		int r = SafeBind();
		Log("rounds bind: %s (%d of 9 DN functions added, thread %lu)", r == 9 ? "ok" : "FAILED", r, GetCurrentThreadId());
	} else if (!strncmp(rest, "stats", 5)) Stats();
	else Log("rounds: unknown command '%s' (bind, stats)", rest);
}
