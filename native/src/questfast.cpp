// The quest graph build made faster: see questfast.h.
#include "questfast.h"
#include "questfast_sort.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

// ---- game addresses (WHGame.dll 1.5.6), re_quest_builder.md 3a / 5
constexpr uintptr_t kRvaNameSort = 0x92a154;   // stable_sort(first, last, pred) of {std::string, u64 hash}, 0x28 B, key +0x20
constexpr uintptr_t kRvaHashSort = 0x6a7b90;   // stable_sort(first, last, pred) of {ptr, len, u64 hash}, 0x18 B, key +0x10
const unsigned char kSortBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x20,0x57 };

using SortFn = void(__fastcall*)(unsigned char*, unsigned char*, unsigned char);

struct List {
	const char* name;
	uintptr_t rva;
	size_t size, key;
	bool strings;               // entries start with an MSVC std::string (compared by content in the self-check)
	SortFn orig = nullptr;
	std::atomic<bool> on{true};             // false after a self-check mismatch (the game's sort from then on) or questbench's B
	std::atomic<bool> bad{false};           // a self-check mismatch: never back on this session
	std::atomic<long long> calls{0}, fast{0}, unsorted{0}, ticks{0};
	std::atomic<long long> checked{0}, checkTicks{0};
};
List g_lists[2] = { { "name list", kRvaNameSort, 0x28, 0x20, true }, { "hash list", kRvaHashSort, 0x18, 0x10, false } };
constexpr long long kCheckCalls = 64;

// MSVC std::string (release, 0x20 B): 16-byte buffer or pointer, size at +0x10, capacity at +0x18 (> 15 = on the heap)
bool SameString(const unsigned char* a, const unsigned char* b) {
	size_t sa, sb, ca, cb;
	memcpy(&sa, a + 0x10, 8); memcpy(&sb, b + 0x10, 8); memcpy(&ca, a + 0x18, 8); memcpy(&cb, b + 0x18, 8);
	if (sa != sb || ca != cb) return false;
	const char* da = ca > 15 ? *(const char* const*)a : (const char*)a;
	const char* db = cb > 15 ? *(const char* const*)b : (const char*)b;
	return da == db || !memcmp(da, db, sa);
}

bool SameEntries(const List& l, const unsigned char* a, const unsigned char* b, size_t n) {
	if (!l.strings) return !memcmp(a, b, n * l.size);
	for (size_t i = 0; i < n; i++) {
		const unsigned char* x = a + i * l.size; const unsigned char* y = b + i * l.size;
		if (!SameString(x, y) || memcmp(x + 0x20, y + 0x20, l.size - 0x20)) return false;
	}
	return true;
}

template <int I> void __fastcall HkSort(unsigned char* first, unsigned char* last, unsigned char pred) {
	List& l = g_lists[I];
	const long long c = ++l.calls;
	if (!l.on.load(std::memory_order_relaxed)) { l.orig(first, last, pred); return; }
	const size_t bytes = last > first ? (size_t)(last - first) : 0;
	unsigned char* copy = nullptr;
	if (c <= kCheckCalls && bytes) copy = (unsigned char*)malloc(bytes);   // the game's sort on a raw copy, to compare
	if (copy) memcpy(copy, first, bytes);
	const long long t0 = Now();
	const bool done = SortedInsertLast(first, last, l.size, l.key);
	l.ticks += Now() - t0;
	if (!done) {   // not "sorted + one new entry": exactly what the game would do
		l.unsorted++;
		free(copy);
		l.orig(first, last, pred);
		return;
	}
	l.fast++;
	if (!copy) return;
	const long long t1 = Now();
	l.orig(copy, copy + bytes, pred);   // moves only: the copy's strings share the real ones' memory, nothing is freed
	l.checkTicks += Now() - t1;
	const bool same = SameEntries(l, first, copy, bytes / l.size);
	// on a difference the list takes the game's own order: the copy holds the same entries (same string memory),
	// arranged by the game's sort
	if (!same) memcpy(first, copy, bytes);
	free(copy);
	l.checked++;
	if (!same) {
		l.bad = true;
		l.on = false;
		Log("questfast: rttr %s: the sorted insert differs from the game's sort at call %lld (%zu entries): the game's order "
		    "was put back and its sort is used for the rest of this session", l.name, c, bytes / l.size);
	}
}

}  // namespace

void Questfast_Report(const char* when) {
	char buf[512];
	int o = 0;
	for (auto& l : g_lists) {
		o += snprintf(buf + o, sizeof buf - o, "%s%s %lld calls, %lld by sorted insert in %.1f ms (%lld unsorted -> game's sort), "
		              "self-check %lld equal%s (the game's sort took %.1f ms for those)", o ? "; " : "", l.name, l.calls.load(),
		              l.fast.load(), l.ticks.load() * 1000.0 / g_freq, l.unsorted.load(), l.checked.load(), l.on ? "" : ", OFF",
		              l.checkTicks.load() * 1000.0 / g_freq);
		if (o >= (int)sizeof buf) break;
	}
	Log("questfast: %s: rttr type registry %s", when, buf);
}

bool Questfast_SetOn(bool on) {
	bool any = false;
	for (auto& l : g_lists)
		if (l.orig) { l.on = on && !l.bad.load(); any = true; }
	return any;
}

void Questfast_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	int mode = 1;
	if (FILE* fp = fopen((dir + "deagle_questfast.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) mode = m != 0;
		fclose(fp);
	}
	if (!mode) { Log("questfast: off (deagle_questfast.txt)"); return; }
	void* const wraps[2] = { (void*)&HkSort<0>, (void*)&HkSort<1> };
	std::string state;
	for (int i = 0; i < 2; i++) {
		List& l = g_lists[i];
		void* t = (void*)(base + l.rva);
		const bool ok = !memcmp(t, kSortBytes, sizeof kSortBytes) &&
		                MH_CreateHook(t, wraps[i], (void**)&l.orig) == MH_OK && MH_EnableHook(t) == MH_OK;
		state += state.empty() ? "" : ", ";
		state += std::string(l.name) + (ok ? " on" : " OFF (code differs or hook failed)");
	}
	Log("questfast: rttr type registry sorted insert: %s", state.c_str());
}
