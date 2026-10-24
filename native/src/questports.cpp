// Quest build: the port-definition walks cached. See questports.h.
#include "questports.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdlib>
#include <cstring>

namespace {

// ---- WHGame.dll 1.5.6 (re_quest_portbind.md 4)
// The enumerators, node vt+0xe0 or called by a derived one: void(node, std::function<void(shared_ptr<def> const&)> by value,
// p3). The callee destroys the function object.
constexpr uintptr_t kRvaEnum[3] = { 0x4f5004,    // C_Node: the class's own rttr properties
                                    0x4f6120,    // the type's property range, then the AutoTriggerable "Set" path
                                    0xacfcd0 };  // 0x4f5004, then the AutoTriggerable "Set" path
const unsigned char kEnumBytes[3][16] = {
	{ 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 },
	{ 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x41,0x54,0x41,0x55,0x41,0x56,0x41,0x57 },
	{ 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x57,0x41,0x56,0x48,0x8b } };
// _Func_impl of the bind / lookup visitor (lambda_9cb866): [0] _Copy, [2] _Do_call(impl, const shared_ptr*), [4] _Delete_this(impl,
// free); captures at impl+8: {CryString* port name, shared_ptr* result}; std::function = 64 B, _Impl* at +0x38.
constexpr uintptr_t kRvaVisitorVt = 0x3e388d8;
// node vt+0x150 / +0x158 bodies the "Set" path may call that write only the new definition (S): `ret`, modules, Quest
constexpr uintptr_t kRvaRet = 0x3b6e80, kRvaModule150 = 0x11ca874, kRvaQuest150 = 0x1f43580;
constexpr uintptr_t kRvaSpRelease = 0x23c8a8c;   // shared_ptr destructor (release ctrl if any)
// Lookup -> bind pass-through (re_quest_portbind.md 4, H3): a lazily created port (and each CreatePorts record) looks its
// definition up (FUN_1806a0674(node, shared_ptr* out, CryString* name): one walk) and then binds it (FUN_180697e78(node,
// port by value): the same walk again, then FUN_180698810(port, &node, shared_ptr by value)). The bind reuses the lookup's
// definition when it is the next bind on this thread, for the same node and port name.
constexpr uintptr_t kRvaLookup = 0x6a0674, kRvaBind = 0x697e78, kRvaSetDef = 0x698810;
constexpr uintptr_t kRvaNodeAddRef = 0x1e38f20, kRvaNodeRelease = 0x1e39320, kRvaStrRelease = 0x4fc624;
const unsigned char kLookupBytes[] = { 0x4c,0x8b,0xdc,0x53,0x48,0x83,0xec,0x60,0x48,0xc7,0x02,0x00,0x00,0x00,0x00,0x48 };
const unsigned char kBindBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x55,0x56,0x57,0x48,0x8b,0xec,0x48,0x81,0xec,0x80,0x00 };
const unsigned char kSetDefBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x49,0x8b,0xd8 };
const unsigned char kAddRefBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb7,0x41,0x0c,0x48,0x8b };
const unsigned char kReleaseBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb8,0xff,0xff,0xff,0xff,0x66,0xf0 };
const unsigned char kStrReleaseBytes[] = { 0x48,0x83,0xec,0x28,0x48,0x8b,0x09,0x48,0x83,0xc1,0xf4,0xe8 };
const unsigned char kSpReleaseBytes[] = { 0x48,0x83,0xec,0x28,0x48,0x8b,0x49,0x08,0x48,0x85,0xc9,0x74,0x05,0xe8 };

using EnumFn = void(__fastcall*)(unsigned char*, unsigned char*, uint64_t);
using SpRelFn = void(__fastcall*)(void*);
EnumFn g_orig[3] = {};
uintptr_t g_base = 0, g_vtVisitor = 0;
SpRelFn g_spRelease = nullptr;
bool g_installed = false;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

thread_local int t_job = 0;
std::atomic<bool> g_on{true}, g_bad{false};

struct Sp { void* p; void* ctrl; };

// ---- lookup -> bind pass-through
using LookupFn = Sp*(__fastcall*)(unsigned char*, Sp*, void*);
using BindFn = uint8_t(__fastcall*)(unsigned char*, void**);
using SetDefFn = void(__fastcall*)(void*, void**, Sp*);
using RefFn = void(__fastcall*)(void*);
LookupFn g_lookupOrig = nullptr;
BindFn g_bindOrig = nullptr;
SetDefFn g_setDef = nullptr;
RefFn g_nodeAddRef = nullptr, g_nodeRelease = nullptr, g_strRelease = nullptr;
bool g_h3 = false;
std::atomic<bool> g_h3On{true}, g_h3Bad{false};
std::atomic<long long> g_h3Used{0}, g_h3Other{0}, g_h3Checked{0}, g_h3CheckTicks{0};
struct Stash { unsigned char* node; Sp sp; char name[128]; bool valid; };
thread_local Stash t_stash{};
inline void AddRefSp(const Sp& sp) { if (sp.ctrl) _InterlockedIncrement((volatile long*)((char*)sp.ctrl + 8)); }
struct Ent { uint64_t h; void* type; uint32_t en, len; char* name; Sp sp; uint32_t hits; };
Ent* g_tab = nullptr;
size_t g_cap = 0, g_n = 0;
SRWLOCK g_lock = SRWLOCK_INIT;

std::atomic<long long> g_hits{0}, g_miss{0}, g_fbThread{0}, g_fbVisitor{0}, g_fbSet{0}, g_fbBody{0}, g_checked{0};
std::atomic<long long> g_missTicks{0}, g_checkTicks{0};

uint64_t Hash(unsigned en, const void* type, const char* s, size_t n) {
	uint64_t h = 0xcbf29ce484222325ull ^ (en * 0x9e3779b97f4a7c15ull) ^ ((uint64_t)(uintptr_t)type * 0xc2b2ae3d27d4eb4full);
	for (size_t i = 0; i < n; i++) { h ^= (unsigned char)s[i]; h *= 0x100000001b3ull; }
	return h | 1;   // 0 = empty slot
}

// under the lock: the slot for the key, or the empty slot where it would go
Ent* Slot(Ent* tab, size_t cap, uint64_t h, unsigned en, const void* type, const char* s, size_t n) {
	for (size_t i = h & (cap - 1);; i = (i + 1) & (cap - 1)) {
		Ent& e = tab[i];
		if (!e.h || (e.h == h && e.type == type && e.en == en && e.len == n && !memcmp(e.name, s, n))) return &e;
	}
}

bool Find(uint64_t h, unsigned en, const void* type, const char* s, size_t n, Sp* sp, uint32_t* hits) {
	AcquireSRWLockShared(&g_lock);
	bool found = false;
	if (g_tab) {
		Ent* e = Slot(g_tab, g_cap, h, en, type, s, n);
		if (e->h) { *sp = e->sp; *hits = ++e->hits; found = true; }   // hits: a benign race at worst (only the job thread writes)
	}
	ReleaseSRWLockShared(&g_lock);
	return found;
}

void Insert(uint64_t h, unsigned en, void* type, const char* s, size_t n, Sp sp) {
	AcquireSRWLockExclusive(&g_lock);
	if ((g_n + 1) * 2 > g_cap) {
		const size_t cap = g_cap ? g_cap * 2 : 4096;
		Ent* tab = (Ent*)calloc(cap, sizeof(Ent));
		if (tab) {
			for (size_t i = 0; i < g_cap; i++)
				if (g_tab[i].h) *Slot(tab, cap, g_tab[i].h, g_tab[i].en, g_tab[i].type, g_tab[i].name, g_tab[i].len) = g_tab[i];
			free(g_tab);
			g_tab = tab; g_cap = cap;
		}
	}
	if (g_tab && (g_n + 1) * 2 <= g_cap) {
		Ent* e = Slot(g_tab, g_cap, h, en, type, s, n);
		if (!e->h) {
			char* copy = (char*)malloc(n + 1);
			if (copy) {
				memcpy(copy, s, n); copy[n] = 0;
				*e = Ent{ h, type, en, (uint32_t)n, copy, sp, 0 };
				g_n++;
				sp = Sp{};   // the table keeps the reference (the definitions live until exit in the game's own map)
			}
		}
	}
	ReleaseSRWLockExclusive(&g_lock);
	if (sp.ctrl) g_spRelease(&sp);   // not kept (another thread's insert won, or no memory)
}

// The game's own static walk (5004's or 6120's original code, past our hooks) with a fresh visitor: its final match.
Sp Walk(unsigned en, unsigned char* node, void* nameStr) {
	Sp r{};
	alignas(16) void* f[8] = {};
	f[0] = (void*)g_vtVisitor; f[1] = nameStr; f[2] = &r; f[7] = f;   // small-buffer std::function, impl in place
	g_orig[en](node, (unsigned char*)f, 0);                               // destroys f (trivially), as for the game's own
	return r;
}

void Destroy(unsigned char* fn, void** impl) {   // what the enumerator does with its by-value function object
	((void(__fastcall*)(void*, bool))((void**)impl[0])[4])(impl, (unsigned char*)impl != fn);
	*(void**)(fn + 0x38) = nullptr;
}

template <int K> void __fastcall HkEnum(unsigned char* node, unsigned char* fn, uint64_t p3) {
	if (!t_job || !g_on.load(std::memory_order_relaxed)) {
		if (g_on.load(std::memory_order_relaxed)) g_fbThread.fetch_add(1, std::memory_order_relaxed);
		return g_orig[K](node, fn, p3);
	}
	void** impl = *(void***)(fn + 0x38);
	if (!impl || (uintptr_t)impl[0] != g_vtVisitor || !impl[1] || !*(const char**)impl[1]) {
		g_fbVisitor.fetch_add(1, std::memory_order_relaxed);   // another visitor (the "Set" path's, IfFunction's...)
		return g_orig[K](node, fn, p3);
	}
	const char* name = *(const char**)impl[1];
	const size_t n = strlen(name);
	if (K != 0 && (*(uint16_t*)(node + 0xe) & 2)) {   // AutoTriggerable: the game's "Set" path runs after the static walk
		if (n <= 1 || !strncmp(name, "Set", 3)) {          // it could match: the game's code
			g_fbSet.fetch_add(1, std::memory_order_relaxed);
			return g_orig[K](node, fn, p3);
		}
		const uintptr_t f = (uintptr_t)(*(void***)node)[(K == 1 ? 0x150 : 0x158) / 8] - g_base;
		if (!(f == kRvaRet || (K == 1 && (f == kRvaModule150 || f == kRvaQuest150)))) {   // skipped only if it writes nothing else
			g_fbBody.fetch_add(1, std::memory_order_relaxed);
			return g_orig[K](node, fn, p3);
		}
	}
	void* type = nullptr;
	((void(__fastcall*)(void*, void**))(*(void***)node)[0x28 / 8])(node, &type);   // static rttr type getter
	const unsigned en = K == 1 ? 1 : 0;                                            // 0xacfcd0's static part is 0x4f5004's
	const uint64_t h = Hash(en, type, name, n);
	Sp sp{};
	uint32_t hits = 0;
	if (!Find(h, en, type, name, n, &sp, &hits)) {
		const long long t = Now();
		sp = Walk(en, node, impl[1]);
		g_missTicks.fetch_add(Now() - t, std::memory_order_relaxed);
		g_miss.fetch_add(1, std::memory_order_relaxed);
		Insert(h, en, type, name, n, sp);
	} else {
		const long long k = g_hits.fetch_add(1, std::memory_order_relaxed);
		if (hits <= 2 || k < 4096 || !(k & 63)) {
			const long long t = Now();
			Sp r = Walk(en, node, impl[1]);
			g_checkTicks.fetch_add(Now() - t, std::memory_order_relaxed);
			g_checked.fetch_add(1, std::memory_order_relaxed);
			if (r.p != sp.p && !g_bad.exchange(true)) {
				g_on = false;
				Log("questfast: port cache MISMATCH: node %p type %p port \"%s\" walk %u: cached %p, the game's walk %p: the port cache "
				    "is off for this session", node, type, name, en, sp.p, r.p);
			}
			if (r.ctrl) g_spRelease(&r);
			if (r.p != sp.p) return g_orig[K](node, fn, p3);
		}
	}
	// The visitor's only effect is "result = definition" on a match (the last match wins): one call with the walk's final
	// match leaves what the whole walk leaves; no match, no call (a value already in the result stays, as in the game).
	if (sp.p) ((void(__fastcall*)(void*, const Sp*))((void**)impl[0])[2])(impl, &sp);
	Destroy(fn, impl);
}

void* const kWrap[3] = { (void*)&HkEnum<0>, (void*)&HkEnum<1>, (void*)&HkEnum<2> };

void DropStash() {
	if (t_stash.valid && t_stash.sp.ctrl) g_spRelease(&t_stash.sp);
	t_stash.valid = false;
	t_stash.sp = Sp{};
}

Sp* __fastcall HkLookup(unsigned char* node, Sp* out, void* nameStr) {
	Sp* r = g_lookupOrig(node, out, nameStr);
	if (!t_job || !g_h3On.load(std::memory_order_relaxed)) return r;
	DropStash();
	const char* name = nameStr ? *(const char**)nameStr : nullptr;
	const size_t n = name ? strlen(name) : 0;
	if (!name || n >= sizeof t_stash.name || !out->p) return r;   // nothing found: no port follows, nothing to pass on
	memcpy(t_stash.name, name, n + 1);
	t_stash.node = node;
	t_stash.sp = *out;
	AddRefSp(t_stash.sp);
	t_stash.valid = true;
	return r;
}

// two definitions have the same name (GetName, vt+0x08, returns a CryString by value). Not the port's name: a wildcard
// definition (metadata 9) matches any one-letter port name ("A", "B" of MakeArray-like nodes) under its own name.
bool SameDefName(void* a, void* b) {
	void* x = nullptr; void* y = nullptr;
	((void*(__fastcall*)(void*, void**))(*(void***)a)[1])(a, &x);
	((void*(__fastcall*)(void*, void**))(*(void***)b)[1])(b, &y);
	const bool same = x && y && !strcmp((const char*)x, (const char*)y);
	if (x) g_strRelease(&x);
	if (y) g_strRelease(&y);
	return same;
}

uint8_t __fastcall HkBind(unsigned char* node, void** port) {
	if (!t_stash.valid) return g_bindOrig(node, port);
	Stash st = t_stash;   // taken: the stash serves at most the next bind
	t_stash.valid = false; t_stash.sp = Sp{};
	const char* pname = nullptr;
	void* nameStr = nullptr;
	if (t_job && g_h3On.load(std::memory_order_relaxed) && st.node == node && port && *port) {
		nameStr = ((void*(__fastcall*)(void*))(*(void***)*port)[0x48 / 8])(*port);   // the port's CryString*
		pname = nameStr ? *(const char**)nameStr : nullptr;
	}
	if (!pname || strcmp(pname, st.name)) {
		g_h3Other.fetch_add(1, std::memory_order_relaxed);
		if (st.sp.ctrl) g_spRelease(&st.sp);
		return g_bindOrig(node, port);
	}
	const long long k = g_h3Used.fetch_add(1, std::memory_order_relaxed);
	if (k < 2048 || !(k & 127)) {   // the walk again: the same definition (static) or one of the same class and name (per instance)
		const long long t = Now();
		Sp again{};
		g_lookupOrig(node, &again, nameStr);   // the port's own name string (the visitor reads its length header)
		const bool same = again.p == st.sp.p || (again.p && st.sp.p && *(void**)again.p == *(void**)st.sp.p && SameDefName(again.p, st.sp.p));
		if (again.ctrl) g_spRelease(&again);
		g_h3CheckTicks.fetch_add(Now() - t, std::memory_order_relaxed);
		g_h3Checked.fetch_add(1, std::memory_order_relaxed);
		if (!same) {
			if (!g_h3Bad.exchange(true)) {
				g_h3On = false;
				Log("questfast: port pass-through MISMATCH: node %p port \"%s\": lookup %p, walk again %p: off for this session",
				    node, st.name, st.sp.p, again.p);
			}
			if (st.sp.ctrl) g_spRelease(&st.sp);
			return g_bindOrig(node, port);
		}
	}
	// the bind's own steps without its walk (FUN_180697e78, S): node ref, set the definition (the call takes the shared_ptr by
	// value: our reference goes to it), node and port refs released
	g_nodeAddRef(node);
	void* nodeLocal = node;
	Sp copy = st.sp;
	g_setDef(*port, &nodeLocal, &copy);
	g_nodeRelease(node);
	if (*port) g_nodeRelease(*port);
	return 1;
}

}  // namespace

void Questports_JobScope(bool enter) { t_job += enter ? 1 : -1; }

bool Questports_SetOn(bool on) {
	if (!g_installed) return false;
	g_on = on && !g_bad.load();
	g_h3On = on && !g_h3Bad.load();
	return true;
}

void Questports_Report(const char* when) {
	if (!g_installed) return;
	AcquireSRWLockShared(&g_lock);
	const size_t keys = g_n;
	ReleaseSRWLockShared(&g_lock);
	Log("questfast: %s: port cache %lld hits, %lld misses (game's walk %.0f ms), %zu keys, self-check %lld equal (%.0f ms)%s; "
	    "the game's code for: another thread %lld, another visitor %lld, \"Set\" path %lld, unknown Set body %lld",
	    when, g_hits.load(), g_miss.load(), g_missTicks.load() * 1000.0 / g_freq, keys, g_checked.load(),
	    g_checkTicks.load() * 1000.0 / g_freq, g_on ? "" : ", OFF", g_fbThread.load(), g_fbVisitor.load(), g_fbSet.load(),
	    g_fbBody.load());
	if (g_h3)
		Log("questfast: %s: port pass-through %lld binds without a walk, %lld other binds, self-check %lld equal (%.0f ms)%s", when,
		    g_h3Used.load(), g_h3Other.load(), g_h3Checked.load(), g_h3CheckTicks.load() * 1000.0 / g_freq, g_h3On ? "" : ", OFF");
}

void Questports_Install(uintptr_t base) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_base = base;
	g_vtVisitor = base + kRvaVisitorVt;
	if (memcmp((void*)(base + kRvaSpRelease), kSpReleaseBytes, sizeof kSpReleaseBytes)) {
		Log("questfast: port cache OFF (shared_ptr release code differs)");
		return;
	}
	g_spRelease = (SpRelFn)(base + kRvaSpRelease);
	void* t[3];
	int ok = 0;
	for (int i = 0; i < 3; i++) {
		t[i] = (void*)(base + kRvaEnum[i]);
		if (!memcmp(t[i], kEnumBytes[i], 16) && MH_CreateHook(t[i], kWrap[i], (void**)&g_orig[i]) == MH_OK) ok++;
	}
	if (ok != 3) {   // all three or none: a half-cached walk would still be exact, but keep it simple to reason about
		for (int i = 0; i < 3; i++) if (g_orig[i]) MH_RemoveHook(t[i]);
		Log("questfast: port cache OFF (%d of 3 enumerators match 1.5.6 / hook failed)", ok);
		return;
	}
	for (int i = 0; i < 3; i++) MH_EnableHook(t[i]);
	g_installed = true;
	// pass-through: all six pieces or none
	void* lk = (void*)(base + kRvaLookup); void* bd = (void*)(base + kRvaBind);
	if (!memcmp(lk, kLookupBytes, 16) && !memcmp(bd, kBindBytes, 16) && !memcmp((void*)(base + kRvaSetDef), kSetDefBytes, 16) &&
	    !memcmp((void*)(base + kRvaNodeAddRef), kAddRefBytes, 16) && !memcmp((void*)(base + kRvaNodeRelease), kReleaseBytes, 16) &&
	    !memcmp((void*)(base + kRvaStrRelease), kStrReleaseBytes, sizeof kStrReleaseBytes)) {
		g_setDef = (SetDefFn)(base + kRvaSetDef);
		g_nodeAddRef = (RefFn)(base + kRvaNodeAddRef); g_nodeRelease = (RefFn)(base + kRvaNodeRelease);
		g_strRelease = (RefFn)(base + kRvaStrRelease);
		if (MH_CreateHook(lk, (void*)&HkLookup, (void**)&g_lookupOrig) == MH_OK) {
			if (MH_CreateHook(bd, (void*)&HkBind, (void**)&g_bindOrig) == MH_OK) {
				MH_EnableHook(lk); MH_EnableHook(bd);
				g_h3 = true;
			} else MH_RemoveHook(lk);
		}
	}
	Log("questfast: port cache on (3 enumerators hooked, quest job thread only); lookup -> bind pass-through %s", g_h3 ? "on" : "OFF");
}
