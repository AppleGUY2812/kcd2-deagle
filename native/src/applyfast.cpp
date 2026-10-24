// Faster pieces of the save load's "apply save" step. See applyfast.h.
#include "applyfast.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <intrin.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

int g_mode = 2;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

bool Hook(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig, const char* what) {
	void* p = (void*)(base + rva);
	if (memcmp(p, bytes, n)) { Log("applyfast: %s: code check failed - not hooked (other game version?)", what); return false; }
	if (MH_CreateHook(p, detour, orig) != MH_OK || MH_EnableHook(p) != MH_OK) { Log("applyfast: %s: hook FAILED", what); return false; }
	return true;
}

// ---- 1. map clouds vs fog of war
// FUN_180b59660(fog, poly): rax is junk when the fog has no root (its callers read al); the hook passes rax through.
// FUN_180b59c54(node, fog, rect {x0, y0, x1, y1}, poly): one 64 m node; leaf cells are tested at their corners.
constexpr uintptr_t kRvaFogAny = 0xb59660, kRvaFogNode = 0xb59c54;
const unsigned char kFogAnyBytes[] = { 0x48,0x8b,0xc4,0x48,0x83,0xec,0x38,0x48,0x83,0x79,0x68,0x00 };
const unsigned char kFogNodeBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x10,0x48,0x89,0x68,0x18,0x56 };
using FogAnyFn = uint64_t(__fastcall*)(uint8_t* fog, uint8_t* poly);
using FogNodeFn = uint64_t(__fastcall*)(void* node, uint8_t* fog, float* rect, uint8_t* poly);
FogAnyFn g_fogAny = nullptr;
FogNodeFn g_fogNode = nullptr;

struct Box {
	const uint8_t* poly = nullptr;   // the cloud being asked about (null: no culling)
	float x0 = 0, y0 = 0, x1 = 0, y1 = 0;
	long long nodes = 0, culled = 0, checked = 0, bad = 0;
};
thread_local Box t_box;
std::atomic<long long> s_clouds{0}, s_ticks{0}, s_nodes{0}, s_culled{0}, s_checked{0}, s_bad{0}, s_yes{0};

uint64_t __fastcall HkFogAny(uint8_t* fog, uint8_t* poly) {
	const long long t0 = Now();
	const Box saved = t_box;   // nested asks (none seen) keep their own box
	t_box = Box{};
	if (g_mode && poly) {
		const float* p = *(const float**)poly;
		const float* e = *(const float**)(poly + 8);
		float x0 = INFINITY, y0 = INFINITY, x1 = -INFINITY, y1 = -INFINITY;
		bool ok = p && e && p <= e;
		for (; ok && p < e; p += 2) {
			if (!std::isfinite(p[0]) || !std::isfinite(p[1])) { ok = false; break; }   // the game's answer, the slow way
			if (p[0] < x0) x0 = p[0];
			if (p[0] > x1) x1 = p[0];
			if (p[1] < y0) y0 = p[1];
			if (p[1] > y1) y1 = p[1];
		}
		if (ok) { t_box.poly = poly; t_box.x0 = x0 - 1.f; t_box.y0 = y0 - 1.f; t_box.x1 = x1 + 1.f; t_box.y1 = y1 + 1.f; }
	}
	const uint64_t r = g_fogAny(fog, poly);
	s_nodes += t_box.nodes; s_culled += t_box.culled; s_checked += t_box.checked; s_bad += t_box.bad;
	if ((uint8_t)r && fog && *(void**)(fog + 0x68)) s_yes++;
	t_box = saved;
	s_clouds++;
	s_ticks += Now() - t0;
	return r;
}

uint64_t __fastcall HkFogNode(void* node, uint8_t* fog, float* rect, uint8_t* poly) {
	Box& b = t_box;
	if (b.poly && b.poly == poly && rect && fog) {
		b.nodes++;
		const uint32_t cellShift = *(uint32_t*)(fog + 0x48) & 31, nodeShift = *(uint32_t*)(fog + 0x4c) & 31;
		const int cells = *(int*)(fog + 0x18);
		float ext = (float)(1u << nodeShift);
		const float leafExt = (float)cells * (float)(1u << cellShift);   // the corners tested lie below x0 + cells * cell
		if (leafExt > ext) ext = leafExt;
		const float x0 = rect[0], y0 = rect[1];
		const float hx = (std::max)(x0 + ext, rect[2]), hy = (std::max)(y0 + ext, rect[3]);
		if (hx < b.x0 || x0 > b.x1 || hy < b.y0 || y0 > b.y1) {
			b.culled++;
			if (g_mode == 2 && (b.culled & 63) == 0) {
				b.checked++;
				if ((uint8_t)g_fogNode(node, fog, rect, poly)) {
					if (b.bad++ == 0 && s_bad.load() == 0)
						Log("applyfast: fog MISMATCH: node (%.1f, %.1f) outside the cloud's box (%.1f..%.1f, %.1f..%.1f) said yes "
						    "- the cull is off for this session", x0, y0, b.x0, b.x1, b.y0, b.y1);
					g_mode = 0;
					return 1;
				}
			}
			return 0;
		}
	}
	return g_fogNode(node, fog, rect, poly);
}

std::atomic<long long> s_lastClouds{0}, s_lastTicks{0};

// ---- 2. the facial DBA list, sorted before the game inserts it (objects step; RE report re_stepC.md)
// male/female.chrparams: `$TracksDatabase path="Animations\humans\facials\*.dba"` makes the file finder
// FUN_18041d194 (recursive) return 19,800 paths into the vector at its 5th argument. The caller (return address
// 0x1803c5e35) inserts each into a sorted array (FUN_1803c6c60: '\' -> '/', ASCII lower case; lower_bound
// FUN_1803c6b0c compares unsigned bytes). The paths come unsorted, so every insert shifted the tail: 0.66 s in boot 13.
// Sorted by the same key first, every insert lands at the end. The set holds the same paths in any order.
constexpr uintptr_t kRvaFind = 0x41d194, kRvaFindChrparams = 0x3c5e35;
const unsigned char kFindBytes[] = { 0x40,0x55,0x53,0x56,0x57,0x41,0x54,0x41,0x56,0x41,0x57,0x48 };
const unsigned char kFindCallBytes[] = { 0x48,0x8d,0x4d,0x90 };   // at the return address: lea rcx,[rbp-70h]
using FindFn = void(__fastcall*)(void* a1, void* a2, void* a3, void* a4, uint64_t* out);
FindFn g_find = nullptr;
uintptr_t g_base = 0;
std::atomic<long long> s_sorts{0}, s_sorted{0}, s_moved{0}, s_sortTicks{0};

inline unsigned char DbaKey(char c) {
	const unsigned char u = (unsigned char)c;
	return u == '\\' ? '/' : ((unsigned)(u - 'A') < 26u ? (unsigned char)(u + 32) : u);
}
bool DbaLess(uint64_t a, uint64_t b) {
	const char* x = (const char*)a;
	const char* y = (const char*)b;
	if (!x || !y) return x < y;
	for (;; x++, y++) {
		const unsigned char p = DbaKey(*x), q = DbaKey(*y);
		if (p != q) return p < q;
		if (!p) return false;
	}
}

void __fastcall HkFind(void* a1, void* a2, void* a3, void* a4, uint64_t* out) {
	const uintptr_t ret = (uintptr_t)_ReturnAddress();
	g_find(a1, a2, a3, a4, out);
	if (g_mode && out && ret == g_base + kRvaFindChrparams) {
		const long long t0 = Now();
		uint64_t* b = (uint64_t*)out[0];
		uint64_t* e = (uint64_t*)out[1];
		if (b && e && b < e) {
			const size_t n = (size_t)(e - b);
			size_t out_of_order = 0;
			for (size_t i = 1; i < n; i++) out_of_order += DbaLess(b[i], b[i - 1]);
			if (out_of_order) std::sort(b, e, DbaLess);
			s_sorted += (long long)n;
			s_moved += (long long)out_of_order;
		}
		s_sorts++;
		s_sortTicks += Now() - t0;
	}
}

}  // namespace

void Applyfast_OnLoaded() {
	const long long c = s_clouds.load(), t = s_ticks.load();
	const long long dc = c - s_lastClouds.exchange(c), dt = t - s_lastTicks.exchange(t);
	if (!dc && !s_sorts.load()) return;
	if (s_sorts.load())
		Log("applyfast: facial DBA lists sorted before the game inserts them: %lld lists, %lld paths (%lld out of order), "
		    "%.1f ms", s_sorts.load(), s_sorted.load(), s_moved.load(), s_sortTicks.load() * 1000.0 / g_freq);
	Log("applyfast: fog: %lld cloud asks in %.0f ms (%lld said yes); grid nodes %lld, skipped %lld (outside the cloud's box), "
	    "checked the game's way %lld, mismatches %lld; mode %d", dc, dt * 1000.0 / g_freq, s_yes.load(), s_nodes.load(),
	    s_culled.load(), s_checked.load(), s_bad.load(), g_mode);
}

void Applyfast_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "deagle_applyfast.txt").c_str(), "r")) {
		int m = 2;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	if (!g_mode) { Log("applyfast: off (deagle_applyfast.txt)"); return; }
	const bool a = Hook(base, kRvaFogAny, kFogAnyBytes, sizeof kFogAnyBytes, (void*)&HkFogAny, (void**)&g_fogAny, "fog ask");
	const bool b = a && Hook(base, kRvaFogNode, kFogNodeBytes, sizeof kFogNodeBytes, (void*)&HkFogNode, (void**)&g_fogNode,
	                         "fog node");
	if (a && !b) MH_DisableHook((void*)(base + kRvaFogAny));
	g_base = base;
	const bool c = !memcmp((void*)(base + kRvaFindChrparams), kFindCallBytes, sizeof kFindCallBytes) &&
	               Hook(base, kRvaFind, kFindBytes, sizeof kFindBytes, (void*)&HkFind, (void**)&g_find, "facial DBA list");
	Log("applyfast: mode %d (%s); map clouds vs fog of war culled by the cloud's box: %s; facial DBA list sorted: %s", g_mode,
	    g_mode == 2 ? "on + spot check" : "on", a && b ? "on" : "OFF", c ? "on" : "OFF");
}
