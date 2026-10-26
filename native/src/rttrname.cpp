// rttr's custom-name registration made fast: see rttrname.h.
#include "rttrname.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string_view>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaCustomName = 0x4f353c;   // void (registry*, type_data** t, string_view* name)
constexpr uintptr_t kRvaDerive = 0x4f3cbc;       // GStr* (GStr* out, type_data** t): the name from the raw type's name
constexpr uintptr_t kRvaUpdate = 0x4f1dd8;       // void (registry*, GStr* name (freed), type_data** t): no-op if unchanged
constexpr uintptr_t kRvaAssign = 0x4f1d38;       // void (GStr* out (empty), const char*, size_t)
constexpr uintptr_t kRvaFree = 0x4f44f4;         // void (GStr*): frees, leaves it empty
constexpr uintptr_t kRvaTmplName = 0x4f2510;     // GStr* (GStr* out, type_data*): a template instance's name
constexpr uintptr_t kRvaMake = 0x6a4fc8;         // GStr* (GStr* out, const char* b, const char* e)
const unsigned char kCustomBytes[] = { 0x48,0x8b,0xc4,0x55,0x53,0x56,0x57,0x41,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0x68 };
const unsigned char kDeriveBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x55,0x57,0x41,0x57,0x48,0x8d };
const unsigned char kUpdateBytes[] = { 0x48,0x89,0x5c,0x24,0x20,0x55,0x56,0x57,0x41,0x56,0x41,0x57,0x48,0x8b,0xec,0x48 };
const unsigned char kAssignBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
const unsigned char kFreeBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x51,0x18,0x48,0x8b,0xd9,0x48,0x83,0xfa };
const unsigned char kTmplBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x54,0x41,0x56 };
const unsigned char kMakeBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x33,0xc0,0x0f,0x57,0xc0,0x48,0x8b,0xd9,0x0f,0x11 };

// type_data fields (rttr detail::type_data as WHGame lays it out)
constexpr size_t kRaw = 0x00;        // type_data* raw_type_data (itself for a raw type)
constexpr size_t kName = 0x18;       // std::string name (the current, custom or derived one)
constexpr size_t kOrig = 0x38;       // string_view: the compiler's name {ptr, len}
constexpr size_t kValid = 0xc0;      // char
constexpr size_t kFlags = 0xc4;      // uint32: 4 = wrapper (derived differently), 0x400 = template instance

struct GStr {   // MSVC std::string
	union { char buf[16]; char* ptr; };
	size_t size, cap;
	const char* data() const { return cap > 15 ? ptr : buf; }
};
static_assert(sizeof(GStr) == 32, "MSVC std::string layout");
inline void Empty(GStr& s) { memset(&s, 0, sizeof s); s.cap = 15; }
inline const GStr& NameOf(void* td) { return *(const GStr*)((char*)td + kName); }
inline bool Same(const GStr& a, const std::string& b) { return a.size == b.size() && !memcmp(a.data(), b.data(), a.size); }

using CustomFn = void(__fastcall*)(void* reg, void** t, void* sv);
using DeriveFn = GStr*(__fastcall*)(GStr* out, void** t);
using UpdateFn = void(__fastcall*)(void* reg, GStr* name, void** t);
using AssignFn = void(__fastcall*)(GStr* out, const char* p, size_t n);
using FreeFn = void(__fastcall*)(GStr* s);
using TmplFn = GStr*(__fastcall*)(GStr* out, void* td);
using MakeFn = GStr*(__fastcall*)(GStr* out, const char* b, const char* e);
CustomFn g_orig = nullptr;
DeriveFn g_derive = nullptr;
UpdateFn g_update = nullptr;
AssignFn g_assign = nullptr;
FreeFn g_free = nullptr;
TmplFn g_tmpl = nullptr;
MakeFn g_make = nullptr;

int g_mode = 1;   // 1 on, 2 check every skip
struct Memo { void* raw; const char* orig; size_t origLen; const char* rawOrig; size_t rawOrigLen; std::string rawName, result; };
std::unordered_map<void*, Memo> g_memo;
SRWLOCK g_lock = SRWLOCK_INIT;
std::atomic<bool> g_memoOn{ true };
std::atomic<long long> g_calls{ 0 }, g_visited{ 0 }, g_skipped{ 0 }, g_derived{ 0 }, g_checks{ 0 }, g_ticks{ 0 };
std::atomic<long long> g_mismatch{ 0 };
// Template instances' names (the second loop). A template's name is made from its arguments' names, so it can only
// change in a naming call that renames a type whose old name is inside it. Each call collects the old names of what
// it renames (the custom-named type, the derived names that change, the templates changed earlier in the loop); a
// template whose name is still the one we last computed and holds none of them is skipped. Boot 128's first try (a
// global "a name changed" generation) skipped only 9 %: some name changes in nearly every call.
std::unordered_map<void*, std::string> g_tmemo;   // under the game's own registration (one thread at a time)
std::vector<std::string> g_old;   // this call's renamed types' old names
// A template renamed in the loop may sit inside one earlier in the list, which this call has passed: the game fixes
// that one in its next call, so its old name is looked for in the next call too.
std::vector<std::string> g_carry;
inline void NoteOld(const GStr& s) { if (s.size) g_old.emplace_back(s.data(), s.size); }
bool HoldsOld(const GStr& name) {
	const std::string_view v(name.data(), name.size);
	for (const std::string& o : g_old) if (v.find(o) != std::string_view::npos) return true;
	return false;
}
std::atomic<long long> g_tvisited{ 0 }, g_tskipped{ 0 }, g_tchecks{ 0 }, g_t1{ 0 }, g_t2{ 0 };
inline bool Check(long long n) { return g_mode == 2 || n < 1000 || (n & 255) == 0; }
inline bool Equal(const GStr& a, const GStr& b) { return a.size == b.size && !memcmp(a.data(), b.data(), a.size); }
long long g_freq = 1;
char g_bad[160] = "";

// The memory's answer for tt, or nullptr: same inputs as its last derivation and it still has that name.
const Memo* Lookup(void* tt) {
	auto it = g_memo.find(tt);
	if (it == g_memo.end()) return nullptr;
	const Memo& m = it->second;
	void* raw = *(void**)((char*)tt + kRaw);
	const char* const* o = (const char* const*)((char*)tt + kOrig);
	const char* const* ro = (const char* const*)((char*)raw + kOrig);
	if (m.raw != raw || m.orig != o[0] || m.origLen != (size_t)o[1] || m.rawOrig != ro[0] || m.rawOrigLen != (size_t)ro[1]) return nullptr;
	if (!Same(NameOf(raw), m.rawName) || !Same(NameOf(tt), m.result)) return nullptr;
	return &m;
}

void Store(void* tt, const GStr& derived) {
	void* raw = *(void**)((char*)tt + kRaw);
	const char* const* o = (const char* const*)((char*)tt + kOrig);
	const char* const* ro = (const char* const*)((char*)raw + kOrig);
	const GStr& rn = NameOf(raw);
	Memo m{ raw, o[0], (size_t)o[1], ro[0], (size_t)ro[1], std::string(rn.data(), rn.size), std::string(derived.data(), derived.size) };
	AcquireSRWLockExclusive(&g_lock);
	g_memo[tt] = std::move(m);
	ReleaseSRWLockExclusive(&g_lock);
}

void NameV2(void* reg, void** t, void* sv, bool check);
bool g_v2 = true;   // off for the session after a check finds a rename it would have missed
long long g_v2calls = 0;
SRWLOCK g_v2lock = SRWLOCK_INIT;

// FUN_1804f353c, the game's steps in its order; only the no-op derive + update pairs are skipped.
void __fastcall HkCustom(void* reg, void** t, void* sv) {
	if (!*((char*)*t + kValid)) return;
	LARGE_INTEGER a; QueryPerformanceCounter(&a);
	g_calls++;
	static thread_local int depth = 0;
	if (g_mode >= 3 && g_v2 && !depth) {   // v2: only the types a rename can change (rttrname.h); a nested call goes v1
		AcquireSRWLockExclusive(&g_v2lock);
		depth++;
		const long long n = g_v2calls++;
		NameV2(reg, t, sv, g_mode == 4 || n < 8 || (n & 63) == 0);
		depth--;
		ReleaseSRWLockExclusive(&g_v2lock);
		LARGE_INTEGER z; QueryPerformanceCounter(&z);
		g_ticks += z.QuadPart - a.QuadPart;
		return;
	}
	{
		const char* const* v = (const char* const*)sv;
		GStr s; Empty(s);
		g_make(&s, v[0], v[0] + (size_t)v[1]);
		g_old.swap(g_carry);
		g_carry.clear();
		if (!Equal(s, NameOf(*t))) NoteOld(NameOf(*t));
		g_update(reg, &s, t);   // frees s
	}
	void** b = *(void***)((char*)reg + 0x28);
	void** e = *(void***)((char*)reg + 0x30);
	std::vector<void*> list(b, e);   // the game copies the list too (the updates may add to it)
	const bool memoOn = g_memoOn.load(std::memory_order_relaxed);
	long long visited = 0, skipped = 0, derived = 0;
	for (size_t i = 0; i < list.size(); i++) {
		void* tt = list[i];
		if (tt == *t || *(void**)((char*)tt + kRaw) == tt) continue;
		visited++;
		const bool plain = !(*(uint32_t*)((char*)tt + kFlags) & 4);
		if (plain && memoOn) {
			const long long n = g_skipped.load(std::memory_order_relaxed) + skipped;
			const bool check = Check(n);
			std::string remembered;
			bool hit = false;
			AcquireSRWLockShared(&g_lock);
			if (const Memo* m = Lookup(tt)) { hit = true; if (check) remembered = m->result; }
			ReleaseSRWLockShared(&g_lock);
			if (hit) {
				if (check) {
					g_checks++;
					GStr d; Empty(d);
					g_derive(&d, &list[i]);
					const bool eq = d.size == remembered.size() && !memcmp(d.data(), remembered.data(), d.size);
					if (!eq && !g_mismatch++) {
						snprintf(g_bad, sizeof g_bad, "type %p: remembered '%.50s', the game's '%.50s'", tt, remembered.c_str(), d.data());
						g_memoOn = false;
					}
					g_free(&d);
					if (!eq) goto full;
				}
				skipped++;
				continue;
			}
		}
	full:
		{
			GStr d; Empty(d);
			g_derive(&d, &list[i]);
			GStr c; Empty(c);
			g_assign(&c, d.data(), d.size);
			if (!Equal(c, NameOf(tt))) NoteOld(NameOf(tt));
			g_update(reg, &c, &list[i]);   // frees c
			if (plain && g_memoOn.load(std::memory_order_relaxed)) Store(tt, d);
			g_free(&d);
			derived++;
		}
	}
	LARGE_INTEGER m; QueryPerformanceCounter(&m);
	long long tvisited = 0, tskipped = 0;
	for (size_t i = 0; i < list.size(); i++) {   // the template instances' names, as the game does them
		void* tt = list[i];
		if (tt == *t || !(*(uint32_t*)((char*)tt + kFlags) & 0x400)) continue;
		tvisited++;
		auto it = memoOn ? g_tmemo.find(tt) : g_tmemo.end();
		const bool hit = it != g_tmemo.end() && Same(NameOf(tt), it->second) && !HoldsOld(NameOf(tt));
		const long long n = g_tskipped.load(std::memory_order_relaxed) + tskipped;
		if (hit && !Check(n)) { tskipped++; continue; }
		GStr s; Empty(s);
		g_tmpl(&s, tt);
		if (hit) {   // checked: the game's answer must be the remembered one
			g_tchecks++;
			if (!Same(s, it->second) && !g_mismatch++) {
				snprintf(g_bad, sizeof g_bad, "template %p: remembered '%.50s', the game's '%.50s'", tt, it->second.c_str(), s.data());
				g_memoOn = false;
			}
		}
		const bool changed = !Equal(s, NameOf(tt));
		if (changed && NameOf(tt).size) { NoteOld(NameOf(tt)); g_carry.push_back(g_old.back()); }   // later in the list, or the next call
		if (g_memoOn.load(std::memory_order_relaxed)) g_tmemo[tt].assign(s.data(), s.size);
		if (hit && !changed) tskipped++;   // a checked skip that matched counts as one
		g_update(reg, &s, &list[i]);   // the game's inline compare + update, then frees s
	}
	g_tvisited += tvisited;
	g_tskipped += tskipped;
	g_t1 += m.QuadPart - a.QuadPart;
	g_visited += visited;
	g_skipped += skipped;
	g_derived += derived;
	LARGE_INTEGER z; QueryPerformanceCounter(&z);
	g_ticks += z.QuadPart - a.QuadPart;
	g_t2 += z.QuadPart - m.QuadPart;
}

// ---- v2 (mode 3, rttrname.h): only the types a rename can change are visited ----
constexpr size_t kClassFn = 0xb8;   // type_data: class_data* (*)(), as FUN_1804f2510 calls it
constexpr size_t kNested = 0x98;    // class_data: vector<type_data*> template arguments {begin, end}
struct TI {
	void* td = nullptr;
	bool inList = false, derivedKind = false, wrapper = false, tmpl = false;
	bool dirty = false, tdirty = false;   // for the plain pass / the template pass
	std::vector<int> rawDeps;   // non-raw types whose raw type is this one
	std::vector<int> argDeps;   // templates with this type among their arguments
};
std::vector<TI> g_ti;
std::vector<void*> g_hk;   // open addressing: type_data* -> index in g_ti
std::vector<int> g_hv;
size_t g_hcount = 0;
inline size_t Hash(void* p) { uint64_t x = (uint64_t)p; x ^= x >> 33; x *= 0xff51afd7ed558ccdULL; x ^= x >> 29; return (size_t)x; }
int Find(void* p) {
	const size_t m = g_hk.size() - 1;
	for (size_t h = Hash(p) & m;; h = (h + 1) & m) {
		if (g_hk[h] == p) return g_hv[h];
		if (!g_hk[h]) return -1;
	}
}
void Put(void* p, int v) {
	const size_t m = g_hk.size() - 1;
	size_t h = Hash(p) & m;
	while (g_hk[h]) h = (h + 1) & m;
	g_hk[h] = p; g_hv[h] = v;
}
int Get(void* p) {
	const int i = Find(p);
	if (i >= 0) return i;
	if ((g_hcount + 1) * 2 > g_hk.size()) {   // grow
		std::vector<void*> ok; ok.swap(g_hk);
		std::vector<int> ov; ov.swap(g_hv);
		g_hk.assign(ok.size() * 2, nullptr); g_hv.assign(ok.size() * 2, -1);
		for (size_t k = 0; k < ok.size(); k++) if (ok[k]) Put(ok[k], ov[k]);
	}
	const int v = (int)g_ti.size();
	g_ti.emplace_back(); g_ti.back().td = p;
	Put(p, v); g_hcount++;
	return v;
}
uint64_t g_bloom[4];
inline void BloomClear() { memset(g_bloom, 0, sizeof g_bloom); }
inline void BloomAdd(void* p) { const size_t h = Hash(p) >> 56; g_bloom[h >> 6] |= 1ull << (h & 63); }
inline bool BloomHas(void* p) { const size_t h = Hash(p) >> 56; return (g_bloom[h >> 6] >> (h & 63)) & 1; }

std::vector<int> g_pdirty, g_tdirty, g_wrappers, g_pendingRaw;   // dirty lists (flags in TI), carried between calls
std::vector<void*> g_snap;
size_t g_lastSize = 0;
long long g_v2checks = 0, g_v2pd = 0, g_v2td = 0, g_v2scans = 0, g_v2new = 0, g_v2emptyArgs = 0;
char g_v2bad[200] = "";
inline bool NameIsOrig(void* td) {
	const GStr& n = NameOf(td);
	const char* const* o = (const char* const*)((char*)td + kOrig);
	return n.size == (size_t)o[1] && !memcmp(n.data(), o[0], n.size);
}
inline void MarkP(int i) { TI& x = g_ti[i]; if (!x.dirty) { x.dirty = true; g_pdirty.push_back(i); } }
inline void MarkT(int i) { TI& x = g_ti[i]; if (!x.tdirty) { x.tdirty = true; g_tdirty.push_back(i); } }

bool Args(void* p, void*** ab, void*** ae) {   // a template's arguments, as FUN_1804f2510 reads them
	__try {
		using ClassFn = char* (*)();
		ClassFn f = *(ClassFn*)((char*)p + kClassFn);
		if (!f) return false;
		char* cd = f();
		if (!cd) return false;
		*ab = *(void***)(cd + kNested);
		*ae = *(void***)(cd + kNested + 8);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void Discover(void* p) {   // a type seen in the list for the first time
	int i = Get(p);
	if (g_ti[i].inList) return;
	g_ti[i].inList = true;
	g_v2new++;
	void* raw = *(void**)((char*)p + kRaw);
	const uint32_t fl = *(uint32_t*)((char*)p + kFlags);
	if (raw != p) {
		const int r = Get(raw);   // may grow g_ti
		g_ti[r].rawDeps.push_back(i);
		g_ti[i].derivedKind = true;
		if (fl & 4) { g_ti[i].wrapper = true; g_wrappers.push_back(i); }
		MarkP(i);
	}
	if (fl & 0x400) {
		g_ti[i].tmpl = true;
		void** ab = nullptr; void** ae = nullptr;
		if (!Args(p, &ab, &ae)) { ab = ae = nullptr; snprintf(g_v2bad, sizeof g_v2bad, "template %p: its arguments couldn't be read", p); g_v2 = false; }
		if (ab == ae) g_v2emptyArgs++;
		for (void** a = ab; a != ae; a++) { const int k = Get(*a); g_ti[k].argDeps.push_back(i); }
		MarkT(i);
	}
}

int Idx(void* p) {
	int i = Find(p);
	if (i < 0 || !g_ti[i].inList) { Discover(p); i = Find(p); }
	return i;
}
// One naming call, v2. check: also run the game's full algorithm and count renames v2 would have missed.
void NameV2(void* reg, void** t, void* sv, bool check) {
	void* self = *t;
	bool selfRenamed;
	{
		const char* const* v = (const char* const*)sv;
		GStr s; Empty(s);
		g_make(&s, v[0], v[0] + (size_t)v[1]);
		selfRenamed = !Equal(s, NameOf(self));
		g_update(reg, &s, t);   // frees s
	}
	void** b = *(void***)((char*)reg + 0x28);
	void** e = *(void***)((char*)reg + 0x30);
	g_snap.assign(b, e);
	if (g_snap.size() != g_lastSize) {   // new types (the list only grows; renames move entries)
		g_v2scans++;
		if (g_snap.size() < g_lastSize) { snprintf(g_v2bad, sizeof g_v2bad, "the type list shrank (%zu -> %zu)", g_lastSize, g_snap.size()); g_v2 = false; }
		for (void* p : g_snap) {
			const int i = Find(p);
			if (i < 0 || !g_ti[i].inList) Discover(p);
		}
	}
	if (!g_v2) check = true;   // turned off above: this call the game's full way
	const int si = Get(self);
	std::vector<int> renamed;   // this call's renames (their template users get looked at)
	if (selfRenamed) {
		renamed.push_back(si);
		for (int d : g_ti[si].rawDeps) MarkP(d);
		if (g_ti[si].derivedKind) g_pendingRaw.push_back(-1 - si);   // its own derived name comes back next call
	}
	for (int r : g_pendingRaw) {   // templates renamed last call: their derived types; a custom-named derived type itself
		if (r >= 0) for (int d : g_ti[r].rawDeps) MarkP(d);
		else MarkP(-1 - r);
	}
	g_pendingRaw.clear();
	for (int w : g_wrappers) MarkP(w);
	long long missed = 0;
	auto doPlain = [&](void*& slot, int i) {   // the game's derive + update; true if renamed
		GStr d; Empty(d);
		g_derive(&d, &slot);
		if (Equal(d, NameOf(slot))) { g_free(&d); return false; }
		g_update(reg, &d, &slot);   // frees d
		renamed.push_back(i);
		return true;
	};
	// the plain pass, in the list's order
	if (check) {
		for (void*& p : g_snap) {
			if (p == self || *(void**)((char*)p + kRaw) == p) continue;
			const int i = Idx(p);
			const bool flagged = g_ti[i].dirty;
			g_ti[i].dirty = false;
			if (doPlain(p, i) && !flagged && !missed++)
				snprintf(g_v2bad, sizeof g_v2bad, "type %p '%.60s' renamed without a v2 mark", p, NameOf(p).data());
		}
	} else if (g_pdirty.size() == 1 && g_ti[g_pdirty[0]].td != self) {
		g_ti[g_pdirty[0]].dirty = false;
		void* p = g_ti[g_pdirty[0]].td;
		doPlain(p, g_pdirty[0]);
		g_v2pd++;
	} else if (!g_pdirty.empty()) {
		BloomClear();
		for (int i : g_pdirty) BloomAdd(g_ti[i].td);
		for (void*& p : g_snap) {
			if (!BloomHas(p) || p == self) continue;
			const int i = Find(p);
			if (i < 0 || !g_ti[i].dirty) continue;
			g_ti[i].dirty = false;
			doPlain(p, i);
			g_v2pd++;
		}
	}
	{   // what is still marked (the custom-named type itself) stays for the next call
		std::vector<int> keep;
		for (int i : g_pdirty) if (g_ti[i].dirty) keep.push_back(i);
		g_pdirty.swap(keep);
	}
	// the template pass: templates holding a renamed type, the new ones, and those carried from earlier calls
	for (int r : renamed) for (int u : g_ti[r].argDeps) MarkT(u);
	auto doTmpl = [&](void*& slot, int i) {
		if (!NameIsOrig(slot)) return false;   // FUN_1804f2510 keeps a name that is no longer the compiler's
		GStr s; Empty(s);
		g_tmpl(&s, slot);
		if (Equal(s, NameOf(slot))) { g_free(&s); return false; }
		g_update(reg, &s, &slot);   // frees s
		g_pendingRaw.push_back(i);
		for (int u : g_ti[i].argDeps) { MarkT(u); BloomAdd(g_ti[u].td); }   // later in the list: this call; earlier: the next
		return true;
	};
	if (check) {
		BloomClear();
		for (void*& p : g_snap) {
			if (p == self || !(*(uint32_t*)((char*)p + kFlags) & 0x400)) continue;
			const int i = Idx(p);
			const bool flagged = g_ti[i].tdirty;
			g_ti[i].tdirty = false;
			if (doTmpl(p, i) && !flagged && !missed++)
				snprintf(g_v2bad, sizeof g_v2bad, "template %p '%.60s' renamed without a v2 mark", p, NameOf(p).data());
		}
	} else if (!g_tdirty.empty()) {
		BloomClear();
		for (int i : g_tdirty) BloomAdd(g_ti[i].td);
		for (void*& p : g_snap) {
			if (!BloomHas(p) || p == self) continue;
			const int i = Find(p);
			if (i < 0 || !g_ti[i].tdirty) continue;
			g_ti[i].tdirty = false;
			doTmpl(p, i);
			g_v2td++;
		}
	}
	{
		std::vector<int> keep;
		for (int i : g_tdirty) if (g_ti[i].tdirty) keep.push_back(i);
		g_tdirty.swap(keep);
	}
	g_lastSize = (size_t)(*(void***)((char*)reg + 0x30) - *(void***)((char*)reg + 0x28));
	if (check) g_v2checks++;
	if (missed) { g_v2 = false; g_mismatch += missed; }   // the full algorithm already did this call right; v1 from now on
}

bool Check(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n) { return !memcmp((void*)(base + rva), bytes, n); }

}  // namespace

bool Rttrname_EarlyInstall(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* fp = fopen((dir + "fastload_rttrname.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &g_mode) != 1) g_mode = 1;
		fclose(fp);
	}
	if (g_mode < 1 || g_mode > 4) { g_mode = 0; return false; }
	if (!Check(base, kRvaCustomName, kCustomBytes, sizeof kCustomBytes) || !Check(base, kRvaDerive, kDeriveBytes, sizeof kDeriveBytes) ||
	    !Check(base, kRvaUpdate, kUpdateBytes, sizeof kUpdateBytes) || !Check(base, kRvaAssign, kAssignBytes, sizeof kAssignBytes) ||
	    !Check(base, kRvaFree, kFreeBytes, sizeof kFreeBytes) || !Check(base, kRvaTmplName, kTmplBytes, sizeof kTmplBytes) ||
	    !Check(base, kRvaMake, kMakeBytes, sizeof kMakeBytes)) { g_mode = -1; return false; }
	g_derive = (DeriveFn)(base + kRvaDerive);
	g_update = (UpdateFn)(base + kRvaUpdate);
	g_assign = (AssignFn)(base + kRvaAssign);
	g_free = (FreeFn)(base + kRvaFree);
	g_tmpl = (TmplFn)(base + kRvaTmplName);
	g_make = (MakeFn)(base + kRvaMake);
	g_hk.assign(32768, nullptr);
	g_hv.assign(32768, -1);
	g_ti.reserve(16384);
	g_memo.reserve(16384);
	g_tmemo.reserve(4096);
	void* t = (void*)(base + kRvaCustomName);
	if (MH_CreateHook(t, (void*)&HkCustom, (void**)&g_orig) != MH_OK) { g_mode = -2; return false; }
	if (MH_EnableHook(t) != MH_OK) { MH_RemoveHook(t); g_mode = -2; return false; }
	return true;
}

void Rttrname_Report() {
	if (g_mode == 0) { Log("rttrname: off (fastload_rttrname.txt)"); return; }
	if (g_mode == -1) { Log("rttrname: the rttr naming code differs (another game version?) - off"); return; }
	if (g_mode == -2) { Log("rttrname: hook failed - off"); return; }
	if (!g_orig) { Log("rttrname: off (the plugin loaded after the game's start-up code: global.ini DontLoadFromDllMain=0)"); return; }
	if (g_mode >= 3)
		Log("rttrname: v2 (mode %d) %s: %lld of %lld namings in v2, %lld checked against the game's full pass; %lld derived names and %lld "
		    "template names redone; %lld list scans for new types (%lld types, %lld templates with no arguments yet)%s%s",
		    g_mode, g_v2 ? "on" : "OFF after a check", g_v2calls, g_calls.load(), g_v2checks, g_v2pd, g_v2td, g_v2scans, g_v2new,
		    g_v2emptyArgs, g_v2bad[0] ? ": " : "", g_v2bad);
	Log("rttrname: %lld type namings in %.0f ms; %lld other types looked at, %lld unchanged names skipped, %lld derived the game's way; "
	    "%lld skips checked; template names: %lld looked at, %lld unchanged skipped, %lld checked; %lld differ%s%s (the plain "
	    "names %.0f ms, the template names %.0f ms)", g_calls.load(), g_ticks.load() * 1000.0 / g_freq, g_visited.load(),
	    g_skipped.load(), g_derived.load(), g_checks.load(), g_tvisited.load(), g_tskipped.load(), g_tchecks.load(), g_mismatch.load(),
	    g_mismatch.load() ? " (memory off from then on): " : "", g_bad, g_t1.load() * 1000.0 / g_freq, g_t2.load() * 1000.0 / g_freq);
}
