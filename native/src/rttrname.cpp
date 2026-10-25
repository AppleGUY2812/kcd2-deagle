// rttr's custom-name registration made fast: see rttrname.h.
#include "rttrname.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
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
// Template instances' names (the second loop): remembered with the names' generation, which goes up whenever an update
// in here really changes a name, and the type list's size; same generation and size = no name the template's name is
// made from has changed through here, and no type came in.
struct TmplMemo { long long gen; size_t listSize; std::string result; };
std::unordered_map<void*, TmplMemo> g_tmemo;
long long g_gen = 0;   // under the game's own registration (one thread at a time; the registry isn't locked either)
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

// FUN_1804f353c, the game's steps in its order; only the no-op derive + update pairs are skipped.
void __fastcall HkCustom(void* reg, void** t, void* sv) {
	if (!*((char*)*t + kValid)) return;
	LARGE_INTEGER a; QueryPerformanceCounter(&a);
	g_calls++;
	{
		const char* const* v = (const char* const*)sv;
		GStr s; Empty(s);
		g_make(&s, v[0], v[0] + (size_t)v[1]);
		if (!Equal(s, NameOf(*t))) g_gen++;
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
			if (!Equal(c, NameOf(tt))) g_gen++;
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
		const bool hit = it != g_tmemo.end() && it->second.gen == g_gen && it->second.listSize == list.size() &&
		                 Same(NameOf(tt), it->second.result);
		const long long n = g_tskipped.load(std::memory_order_relaxed) + tskipped;
		if (hit && !Check(n)) { tskipped++; continue; }
		GStr s; Empty(s);
		g_tmpl(&s, tt);
		if (hit) {   // checked: the game's answer must be the remembered one
			g_tchecks++;
			if (!Same(s, it->second.result) && !g_mismatch++) {
				snprintf(g_bad, sizeof g_bad, "template %p: remembered '%.50s', the game's '%.50s'", tt, it->second.result.c_str(), s.data());
				g_memoOn = false;
			}
		}
		const bool changed = !Equal(s, NameOf(tt));
		if (changed) g_gen++;
		if (g_memoOn.load(std::memory_order_relaxed)) {
			TmplMemo& tm = g_tmemo[tt];
			tm.gen = g_gen;   // after this one's own change: its name now is s
			tm.listSize = list.size();
			tm.result.assign(s.data(), s.size);
		}
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
	if (g_mode != 1 && g_mode != 2) { g_mode = 0; return false; }
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
	Log("rttrname: %lld type namings in %.0f ms; %lld other types looked at, %lld unchanged names skipped, %lld derived the game's way; "
	    "%lld skips checked; template names: %lld looked at, %lld unchanged skipped, %lld checked; %lld differ%s%s (the plain "
	    "names %.0f ms, the template names %.0f ms)", g_calls.load(), g_ticks.load() * 1000.0 / g_freq, g_visited.load(),
	    g_skipped.load(), g_derived.load(), g_checks.load(), g_tvisited.load(), g_tskipped.load(), g_tchecks.load(), g_mismatch.load(),
	    g_mismatch.load() ? " (memory off from then on): " : "", g_bad, g_t1.load() * 1000.0 / g_freq, g_t2.load() * 1000.0 / g_freq);
}
