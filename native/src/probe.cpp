// Probes for docs/ARCHITECTURE.md migration step 1 (Claude 5). Each checks one static finding of docs/whgame_re/ in the
// running game (WHGame 1.5.6), run by "[DN] probe <name> <args>" from tools/probe/*.lua over rcon:
//   threads                              the Lua thread id (the camera step logs its own in main.cpp's frame line)
//   nop                                  nothing: times the [DN] text bridge itself (tools/probe/bind.lua)
//   bind                                 registers the global table DN (add, nop, tid) through IScriptTable::AddFunction
//   ray <x y z> <dx dy dz> <len> <n>     n synchronous IPhysicalWorld::RayWorldIntersection, timed (QPC)
//   rayq <x y z> <dx dy dz> <len>        one queued ray (rwi_queue): its result is logged when the engine delivers it
//   light <x y z> <r g b> <radius> <m>   an entity-free light render node: created, or moved (m 0 = SetMatrix only,
//                                        m 1 = unregister, SetMatrix, register again)
//   lightoff                             deletes it (do it before loading a save)
//   mat <node> <x y z> <dx dy dz>        the drawn sub-material at a hit on a static brush (hit.renderNode)
// No per-frame work, no hooks. Every engine call runs under SEH; strings from the engine are copied under SEH too.
#include "probe.h"
#include "log.h"
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static uintptr_t g_env = 0, g_base = 0;
static LARGE_INTEGER g_freq{};

static bool Rd(uintptr_t a, uintptr_t& out) {
	__try { out = *(uintptr_t*)a; return true; } __except (EXCEPTION_EXECUTE_HANDLER) { out = 0; return false; }
}
static uintptr_t VF(uintptr_t obj, int off) { uintptr_t vt; return obj && Rd(obj, vt) && Rd(vt + off, vt) ? vt : 0; }
static uintptr_t VT(uintptr_t obj) { uintptr_t vt = 0; return obj && Rd(obj, vt) ? vt : 0; }
static uintptr_t Env(int off) { uintptr_t p = 0; return g_env && Rd(g_env + off, p) ? p : 0; }
static void CopyStr(const char* s, char* out, size_t cap) {
	out[0] = 0;
	__try { size_t i = 0; for (; s && i + 1 < cap && s[i]; i++) out[i] = s[i]; out[i] = 0; }
	__except (EXCEPTION_EXECUTE_HANDLER) { strcpy_s(out, cap, "<bad string>"); }
}
static double Us(const LARGE_INTEGER& a, const LARGE_INTEGER& b) { return (b.QuadPart - a.QuadPart) * 1e6 / (double)g_freq.QuadPart; }
static int ParseFloats(const char* s, float* out, int n) {
	int k = 0;
	while (k < n && s && *s) { char* end; float v = strtof(s, &end); if (end == s) break; out[k++] = v; s = end; }
	return k;
}

void Probe_Install(uintptr_t env, uintptr_t gameBase) {
	g_env = env; g_base = gameBase;
	QueryPerformanceFrequency(&g_freq);
	Log("probe: ready (gEnv %p, WHGame %p)", (void*)env, (void*)gameBase);
}

// ---- threads
static void Threads() { Log("probe threads: Lua (System.LogAlways) runs on thread %lu", GetCurrentThreadId()); }

// ---- bind: the typed binding (docs/whgame_re/script_binding.md). IScriptSystem = gEnv+0x28: +0x68 CreateTable(bool),
// +0xF8 SetGlobalAny(key, any). IScriptTable: +0x18 AddRef, +0xB0 AddFunction(const SUserFunctionDesc&) (0x58 bytes:
// name +0, params +8, global +0x10, functor +0x18 (32 bytes, zero = use the user-data function), param id offset +0x38,
// pUserDataFunc +0x40, pDataBuffer +0x48, nDataSize +0x50). IFunctionHandler: +0x28 GetParamCount, +0x38
// GetParamAny(1-based, any&), +0x50 EndFunctionAny(a), +0x48 (a, b), +0x58 EndFunction(). ScriptAnyValue 0x18 bytes:
// type +0 (1 nil, 2 bool, 4 number (float), 5 string, 6 table), value +8. Errors come back as nil, message.
struct Any { int type; int pad; union { float n; bool b; const char* s; uintptr_t p; float v[3]; }; };
static_assert(sizeof(Any) == 0x18, "ScriptAnyValue is 0x18 bytes");
using GetAnyFn = bool(__fastcall*)(uintptr_t pH, int idx, Any* a);
using End1Fn = int(__fastcall*)(uintptr_t pH, const Any* a);
using End2Fn = int(__fastcall*)(uintptr_t pH, const Any* a, const Any* b);
using End0Fn = int(__fastcall*)(uintptr_t pH);
static int Fail(uintptr_t pH, const char* msg) {
	Any nil{}; nil.type = 1; Any m{}; m.type = 5; m.s = msg;
	auto end = (End2Fn)VF(pH, 0x48);
	return end ? end(pH, &nil, &m) : 0;
}
static int __cdecl DnAdd(uintptr_t pH, void*, int) {
	auto get = (GetAnyFn)VF(pH, 0x38);
	Any a{}, b{}; a.type = 4; b.type = 4;
	if (!get || !get(pH, 1, &a) || !get(pH, 2, &b)) return Fail(pH, "DN.add(a, b): two numbers expected");
	Any r{}; r.type = 4; r.n = a.n + b.n;
	auto end = (End1Fn)VF(pH, 0x50);
	return end ? end(pH, &r) : 0;
}
static int __cdecl DnNop(uintptr_t pH, void*, int) { auto end = (End0Fn)VF(pH, 0x58); return end ? end(pH) : 0; }
static int __cdecl DnTid(uintptr_t pH, void*, int) {
	Any r{}; r.type = 4; r.n = (float)GetCurrentThreadId();
	auto end = (End1Fn)VF(pH, 0x50);
	return end ? end(pH, &r) : 0;
}
static bool AddFn(uintptr_t table, const char* name, void* fn) {
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
static int SafeBind() {
	__try {
		uintptr_t ss = Env(0x28);
		auto create = (uintptr_t(__fastcall*)(uintptr_t, bool))VF(ss, 0x68);
		if (!create) return -1;
		uintptr_t t = create(ss, false);
		auto addRef = (void(__fastcall*)(uintptr_t))VF(t, 0x18);
		if (!t || !addRef) return -2;
		addRef(t);   // kept for the session: the probe never releases it
		int ok = AddFn(t, "add", (void*)&DnAdd) + AddFn(t, "nop", (void*)&DnNop) + AddFn(t, "tid", (void*)&DnTid);
		auto setGlobal = (void(__fastcall*)(uintptr_t, const char*, const Any*))VF(ss, 0xF8);
		if (!setGlobal) return -3;
		Any g{}; g.type = 6; g.p = t;
		setGlobal(ss, "DN", &g);
		return ok;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
static void Bind() {
	int r = SafeBind();
	Log("probe bind: %s (%d of 3 functions added, thread %lu)", r == 3 ? "DN registered" : "FAILED", r, GetCurrentThreadId());
}

// ---- rays (docs/whgame_re/rays.md). IPhysicalWorld = gEnv+0x30, +0x118 RayWorldIntersection(const SRWIParams&, tag,
// iCaller 4). SRWIParams 0x70 bytes: pForeignData +0, iForeignData +8, OnEvent +0x10, org +0x18, dir +0x24 (full
// length), objtypes +0x30 (0x11F), flags +0x34 (0xF; | 0x800 rwi_queue), hits +0x38, nMaxHits +0x40. ray_hit 0x50:
// dist +0, surface_idx (short) +0x18, pt +0x24. A queued ray's EventPhysRWIResult: pForeignData +0x18, pHits +0x30,
// nHits +0x38; delivered on the main thread by PumpLoggedEvents; pHits only valid inside the callback.
using RwiFn = int(__fastcall*)(uintptr_t world, const uint8_t* params, const char* tag, int iCaller);
static void FillRay(uint8_t* p, const float* org, const float* dir, float len, void* hits, int flags) {
	memset(p, 0, 0x70);
	float d[3] = { dir[0], dir[1], dir[2] };
	float m = sqrtf(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
	if (m > 0) for (float& x : d) x *= len / m;
	memcpy(p + 0x18, org, 12); memcpy(p + 0x24, d, 12);
	*(int*)(p + 0x30) = 0x11F; *(int*)(p + 0x34) = flags;
	*(void**)(p + 0x38) = hits; *(int*)(p + 0x40) = 1;
}
static int SafeRwi(uintptr_t w, const uint8_t* p) {
	__try { auto f = (RwiFn)VF(w, 0x118); return f ? f(w, p, "deagle_probe", 4) : -1; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
static void Ray(const char* args) {
	float a[8] = {};
	if (ParseFloats(args, a, 8) < 7) { Log("probe ray: want x y z dx dy dz len [n]"); return; }
	int n = a[7] > 0 ? (int)a[7] : 1000; if (n > 100000) n = 100000;
	uintptr_t w = Env(0x30);
	alignas(16) uint8_t p[0x70]; alignas(16) uint8_t hit[0x50] = {};
	FillRay(p, a, a + 3, a[6], hit, 0xF);
	int first = SafeRwi(w, p);
	float dist = *(float*)(hit + 0x00); short surf = *(short*)(hit + 0x18);
	LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
	int hits = 0;
	for (int i = 0; i < n; i++) hits += SafeRwi(w, p) > 0;
	QueryPerformanceCounter(&t1);
	Log("probe ray: first returned %d (dist %.3f m, surface_idx %d); %d rays of %.0f m: %.2f us each (%d hit), thread %lu",
		first, dist, (int)surf, n, a[6], Us(t0, t1) / n, hits, GetCurrentThreadId());
}
static LARGE_INTEGER g_rayqAt[16];
static int g_rayqSeq = 0, g_rayqLogs = 0;
static int __cdecl OnRwi(const uint8_t* ev) {
	LARGE_INTEGER now; QueryPerformanceCounter(&now);
	__try {
		if (g_rayqLogs >= 32) return 1;   // rate limit: probes only, never more than 32 lines a session
		g_rayqLogs++;
		int seq = (int)*(uintptr_t*)(ev + 0x18), nHits = *(int*)(ev + 0x38);
		const uint8_t* h = *(const uint8_t* const*)(ev + 0x30);
		float dist = -1, pt[3] = {};
		if (nHits > 0 && h) { dist = *(const float*)h; memcpy(pt, h + 0x24, 12); }
		Log("probe rayq #%d delivered after %.2f ms on thread %lu: %d hit, dist %.3f m at %.2f %.2f %.2f", seq,
			Us(g_rayqAt[seq & 15], now) / 1000.0, GetCurrentThreadId(), nHits, dist, pt[0], pt[1], pt[2]);
	} __except (EXCEPTION_EXECUTE_HANDLER) { Log("probe rayq: bad result event"); }
	return 1;
}
static void RayQ(const char* args) {
	float a[7] = {};
	if (ParseFloats(args, a, 7) < 7) { Log("probe rayq: want x y z dx dy dz len"); return; }
	uintptr_t w = Env(0x30);
	alignas(16) uint8_t p[0x70]; alignas(16) uint8_t hit[0x50] = {};
	FillRay(p, a, a + 3, a[6], hit, 0xF);
	int sync = SafeRwi(w, p);   // the same ray answered now, to compare with the queued one
	int seq = ++g_rayqSeq;
	FillRay(p, a, a + 3, a[6], nullptr, 0x80F);   // hits NULL: the engine's pool
	*(uintptr_t*)(p + 0x00) = (uintptr_t)seq;
	*(void**)(p + 0x10) = (void*)&OnRwi;
	QueryPerformanceCounter(&g_rayqAt[seq & 15]);
	int r = SafeRwi(w, p);
	Log("probe rayq #%d queued (returned %d, expect 1) on thread %lu; synchronous: %d hit, dist %.3f m", seq, r,
		GetCurrentThreadId(), sync, *(float*)hit);
}

// ---- light (docs/whgame_re/lights.md). I3DEngine = gEnv+0x08: +0x530 CreateLightSource, +0x538 DeleteLightSource,
// +0x130 RegisterEntity(node, -1, -1), +0x170 UnRegisterEntityDirect(node). Light node: +0x80 SetMatrix(const
// Matrix34&), +0x250 GetLightProperties -> CDLight (0x320 bytes): flags +0x0C (0x40 point), origin +0x10, radius +0x1C
// (+0x204 base), colour +0x20 (+0x1F0 base), specular +0x30 (+0x200 base), bulb size +0x210.
static uintptr_t g_light = 0, g_lightVt = 0;
static int SafeLight(const float* a, int mode, bool create) {
	__try {
		uintptr_t e3d = Env(0x08);
		if (!e3d) return -1;
		if (create) {
			auto mk = (uintptr_t(__fastcall*)(uintptr_t))VF(e3d, 0x530);
			if (!mk || !(g_light = mk(e3d))) return -2;
			g_lightVt = VT(g_light);
			auto props = (uint8_t*(__fastcall*)(uintptr_t))VF(g_light, 0x250);
			uint8_t* d = props ? props(g_light) : nullptr;
			if (!d) return -3;
			*(uint32_t*)(d + 0x0C) |= 0x40;
			float col[4] = { a[3], a[4], a[5], 1 };
			memcpy(d + 0x20, col, 16); memcpy(d + 0x1F0, col, 16);
			*(float*)(d + 0x1C) = a[6]; *(float*)(d + 0x204) = a[6];
			*(float*)(d + 0x30) = 1; *(float*)(d + 0x200) = 1;
			*(float*)(d + 0x210) = 0.3f;
		}
		float m[12] = { 1, 0, 0, a[0], 0, 1, 0, a[1], 0, 0, 1, a[2] };
		auto setMatrix = (void(__fastcall*)(uintptr_t, const float*))VF(g_light, 0x80);
		auto reg = (void(__fastcall*)(uintptr_t, uintptr_t, int, int))VF(e3d, 0x130);
		auto unreg = (void(__fastcall*)(uintptr_t, uintptr_t))VF(e3d, 0x170);
		if (!setMatrix || !reg || !unreg) return -4;
		if (!create && mode == 1) unreg(e3d, g_light);
		setMatrix(g_light, m);
		if (create || mode == 1) reg(e3d, g_light, -1, -1);
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
static void Light(const char* args) {
	float a[8] = {};
	if (ParseFloats(args, a, 8) < 7) { Log("probe light: want x y z r g b radius [mode]"); return; }
	bool create = !g_light;
	LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
	int r = SafeLight(a, (int)a[7], create);
	QueryPerformanceCounter(&t1);
	Log("probe light: %s %p (vtable RVA %llx) at %.2f %.2f %.2f, mode %d: %d, %.1f us", create ? "created" : "moved",
		(void*)g_light, (unsigned long long)(g_lightVt ? g_lightVt - g_base : 0), a[0], a[1], a[2], (int)a[7], r, Us(t0, t1));
}
static void LightOff() {
	if (!g_light) { Log("probe lightoff: no light"); return; }
	int r = 0;
	__try {
		uintptr_t e3d = Env(0x08);
		auto del = (void(__fastcall*)(uintptr_t, uintptr_t))VF(e3d, 0x538);
		if (VT(g_light) != g_lightVt) r = -2;   // freed (a save load): don't touch it
		else if (del) { del(e3d, g_light); r = 1; }
		else r = -1;
	} __except (EXCEPTION_EXECUTE_HANDLER) { r = -100; }
	Log("probe lightoff: %d", r);
	g_light = 0; g_lightVt = 0;
}

// ---- mat (docs/whgame_re/render_material.md). Brush vtables (RVA) 0x401FE70 CBrush, 0x4020370, 0x3A6F148. Render node
// +0xB0 GetEntityStatObj(sub, Matrix34A* out, bool), +0x130 GetMaterial(Vec3*), +0x48 name (probably the cgf path).
// IStatObj +0x288 RayIntersection(SRayHitInfo&, IMaterial*) in the object's local space. SRayHitInfo 0xC0 bytes:
// reference point +0, ray origin +0x0C, direction (with length) +0x18, fMaxHitDistance +0x70; out: fDistance +0x98,
// nHitMatID +0xB4, nHitTriID +0xB8, nHitSurfaceID +0xBC. IMaterial +0x40 GetName, +0x140 GetSafeSubMtl(i), +0x70
// GetSurfaceType -> ISurfaceType +0x18 GetName.
static bool Invert34(const float* m, float* o) {
	float a = m[0], b = m[1], c = m[2], d = m[4], e = m[5], f = m[6], g = m[8], h = m[9], i = m[10];
	float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
	float det = a * A + b * B + c * C;
	if (fabsf(det) < 1e-12f) return false;
	float k = 1 / det;
	o[0] = A * k; o[1] = -(b * i - c * h) * k; o[2] = (b * f - c * e) * k;
	o[4] = B * k; o[5] = (a * i - c * g) * k; o[6] = -(a * f - c * d) * k;
	o[8] = C * k; o[9] = -(a * h - b * g) * k; o[10] = (a * e - b * d) * k;
	for (int r = 0; r < 3; r++) o[r * 4 + 3] = -(o[r * 4] * m[3] + o[r * 4 + 1] * m[7] + o[r * 4 + 2] * m[11]);
	return true;
}
struct MatResult { int ok, matId, tri, surfId; float dist; double us[2]; char sub[96], surf[64], node[160]; };
static int SafeMat(uintptr_t rn, const float* pos, const float* dir, MatResult& out) {
	__try {
		uintptr_t vt = VT(rn);
		if (vt != g_base + 0x401FE70 && vt != g_base + 0x4020370 && vt != g_base + 0x3A6F148) return -2;
		alignas(16) float W[12] = {}, L[12];
		auto getSo = (uintptr_t(__fastcall*)(uintptr_t, unsigned, float*, bool))VF(rn, 0xB0);
		auto getMtl = (uintptr_t(__fastcall*)(uintptr_t, const float*))VF(rn, 0x130);
		uintptr_t so = getSo ? getSo(rn, 0, W, false) : 0, mtl = getMtl ? getMtl(rn, nullptr) : 0;
		if (!so || !mtl) return -3;
		if (!Invert34(W, L)) return -4;
		auto name = (const char*(__fastcall*)(uintptr_t))VF(rn, 0x48);
		if (name) CopyStr(name(rn), out.node, sizeof(out.node));
		float m = sqrtf(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]); if (m <= 0) return -5;
		float s[3], d[3];
		for (int k = 0; k < 3; k++) { s[k] = pos[k] - dir[k] / m * 0.25f; d[k] = dir[k] / m * 0.5f; }
		float o[3], ld[3];
		for (int r = 0; r < 3; r++) {
			o[r] = L[r * 4] * s[0] + L[r * 4 + 1] * s[1] + L[r * 4 + 2] * s[2] + L[r * 4 + 3];
			ld[r] = L[r * 4] * d[0] + L[r * 4 + 1] * d[1] + L[r * 4 + 2] * d[2];
		}
		auto ray = (bool(__fastcall*)(uintptr_t, uint8_t*, uintptr_t))VF(so, 0x288);
		if (!ray) return -6;
		alignas(16) uint8_t hi[0xC0];
		for (int pass = 0; pass < 2; pass++) {   // twice: the first may copy the mesh from the GPU (kept afterwards)
			memset(hi, 0, sizeof(hi));
			memcpy(hi + 0x00, o, 12); memcpy(hi + 0x0C, o, 12); memcpy(hi + 0x18, ld, 12);
			*(float*)(hi + 0x70) = 2.0f;
			LARGE_INTEGER t0, t1; QueryPerformanceCounter(&t0);
			out.ok = ray(so, hi, mtl);
			QueryPerformanceCounter(&t1);
			out.us[pass] = Us(t0, t1);
		}
		out.dist = *(float*)(hi + 0x98); out.matId = *(int*)(hi + 0xB4); out.tri = *(int*)(hi + 0xB8); out.surfId = *(int*)(hi + 0xBC);
		if (!out.ok) return 0;
		auto safeSub = (uintptr_t(__fastcall*)(uintptr_t, int))VF(mtl, 0x140);
		uintptr_t sub = safeSub ? safeSub(mtl, out.matId) : 0;
		auto mtlName = (const char*(__fastcall*)(uintptr_t))VF(sub, 0x40);
		if (mtlName) CopyStr(mtlName(sub), out.sub, sizeof(out.sub));
		auto surfType = (uintptr_t(__fastcall*)(uintptr_t))VF(sub, 0x70);
		uintptr_t st = surfType ? surfType(sub) : 0;
		auto stName = (const char*(__fastcall*)(uintptr_t))VF(st, 0x18);
		if (stName) CopyStr(stName(st), out.surf, sizeof(out.surf));
		return 1;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return -100; }
}
static void Mat(const char* args) {
	const char* hex = strstr(args, "0x");
	if (!hex) { Log("probe mat: want 0x<renderNode> x y z dx dy dz"); return; }
	char* end; uintptr_t rn = (uintptr_t)_strtoui64(hex + 2, &end, 16);
	float a[6] = {};
	if (ParseFloats(end, a, 6) < 6) { Log("probe mat: want 0x<renderNode> x y z dx dy dz"); return; }
	MatResult r{};
	int rc = SafeMat(rn, a, a + 3, r);
	Log("probe mat: %d node %p '%s': hit %d at %.3f m, sub-material %d '%s', surface %d '%s', tri %d; %.1f us first, %.1f us again",
		rc, (void*)rn, r.node, r.ok, r.dist, r.matId, r.sub, r.surfId, r.surf, r.tri, r.us[0], r.us[1]);
}

void Probe_Command(const char* rest) {
	char name[16] = {};
	if (sscanf_s(rest, "%15s", name, (unsigned)sizeof(name)) != 1) { Log("probe: no name"); return; }
	const char* args = rest + strlen(name);
	if (!g_env) { Log("probe %s: gEnv not found yet", name); return; }
	if (!strcmp(name, "threads")) Threads();
	else if (!strcmp(name, "nop")) {}
	else if (!strcmp(name, "bind")) Bind();
	else if (!strcmp(name, "ray")) Ray(args);
	else if (!strcmp(name, "rayq")) RayQ(args);
	else if (!strcmp(name, "light")) Light(args);
	else if (!strcmp(name, "lightoff")) LightOff();
	else if (!strcmp(name, "mat")) Mat(args);
	else Log("probe: unknown '%s' (threads nop bind ray rayq light lightoff mat)", name);
}
