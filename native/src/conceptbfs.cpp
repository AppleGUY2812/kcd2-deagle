// The concept graph's find-by-name without the game's deque: see conceptbfs.h. WHGame.dll 1.5.6, checked at start.
#include "conceptbfs.h"
#include "conceptbfs_core.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "MinHook.h"

namespace {

// FUN_181e32040(root, out, name): breadth-first over root's children (node +0x40..+0x48), each node tested before its
// children are queued; a node is compared when its vt+0xb0 says it has a name (char* at +0x10, compared with
// **(char***)name); it is a container (its children queued) when FUN_180695864(&node) says so (an rttr cast that keeps
// the pointer, FUN_180695820). The game holds the queue in a std::deque of counted pointers: per node 4 AddRef +
// 4 Release (FUN_181e38f20 / FUN_181e39320, LOCK'd) and a 16-byte block per 2 pushes. *out = the node with one
// reference for the caller, or 0; returns out. Boot 13: 0.35 s in the apply step + 0.22 s before it (re_stepG 2c).
constexpr uintptr_t kRvaFind = 0x1e32040, kRvaAddRef = 0x1e38f20, kRvaRelease = 0x1e39320, kRvaContainer = 0x695864;
const unsigned char kFindBytes[] = { 0x48,0x89,0x5c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x55,0x41,0x56,0x41,0x57,0x48,
                                     0x8d,0x6c,0x24,0xb9,0x48,0x81,0xec,0xa0,0x00,0x00,0x00 };
const unsigned char kAddRefBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb7,0x41,0x0c };
const unsigned char kReleaseBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0xb8,0xff,0xff,0xff,0xff };
const unsigned char kContainerBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x09,0x33,0xc0,0x48,0x85,0xc9 };

using FindFn = void**(__fastcall*)(uint8_t* root, void** out, const char*** name);
using RefFn = void(__fastcall*)(void* node);
using ContainerFn = uint64_t(__fastcall*)(void** slot);
FindFn g_orig = nullptr;
RefFn g_addRef = nullptr, g_release = nullptr;
ContainerFn g_container = nullptr;

std::atomic<int> g_mode{1};               // 0 off, 1 the walk without the deque (default), 2 + the walk remembered
                                          // per root (opt-in: boot 66 crashed in it, fixed; boot 66: slower in the first 200)
                                          // (the first kCheck calls, then every 64th, also run the game's and compare)
constexpr long long kCheck = 200;
std::atomic<long long> g_checkLeft{kCheck};
std::atomic<long long> n_calls{0}, n_nodes{0}, n_found{0}, n_bad{0}, n_ticks{0}, n_origTicks{0}, n_checked{0};
std::atomic<long long> n_roots{0}, n_builds{0}, n_stale{0}, n_idx{0};
long long g_freq = 1;

struct Ops {
	bool Named(void* n) { return ((char(__fastcall*)(void*))(*(void***)n)[0xb0 / 8])(n) != 0; }
	bool Container(void** slot) { return (char)g_container(slot) != 0; }
	void AddRef(void* n) { g_addRef(n); }
	void Release(void* n) { g_release(n); }
	const char* Name(void* n) { return *(const char**)((uint8_t*)n + 0x10); }
	const void* Vt(void* n) { return *(const void* const*)n; }
	void** Kids(void* n, void*** end) {
		*end = *(void***)((uint8_t*)n + 0x48);
		return *(void***)((uint8_t*)n + 0x40);
	}
};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

void** __fastcall HkFind(uint8_t* root, void** out, const char*** name) {
	if (!g_mode.load(std::memory_order_relaxed) || !root || !name || !*name || !**name) return g_orig(root, out, name);
	thread_local std::vector<void*> scratch;
	thread_local int depth = 0;   // a name test that finds again gets its own queue
	std::vector<void*> own;
	std::vector<void*>& q = depth ? own : scratch;
	Ops o;
	size_t visited = 0;
	const long long t0 = Now();
	depth++;
	void* mine = nullptr;
	if (depth == 1 && g_mode.load(std::memory_order_relaxed) == 2) {
		// the remembered walks, one per root (the game asks under a few roots): reuse the root's, else the oldest slot
		struct Slot { cbfs::Snap s; long long used = 0; };
		thread_local Slot slots[8];
		thread_local long long tick = 0;
		Slot* sl = nullptr;
		for (Slot& x : slots) if (x.s.root == root) { sl = &x; break; }
		if (!sl) {
			sl = &slots[0];
			for (Slot& x : slots) if (x.used < sl->used) sl = &x;
			if (!sl->s.root) n_roots++;
			sl->s.root = nullptr;   // a new root: Build runs
		}
		sl->used = ++tick;
		const size_t b0 = sl->s.builds, st0 = sl->s.stale;
		mine = cbfs::FindIndexed(root, **name, o, sl->s);
		n_builds += (long long)(sl->s.builds - b0);
		n_stale += (long long)(sl->s.stale - st0);
		const long long k = ++n_idx;
		// a walk is a full one: if the graph changes between most finds it costs more than the game's walk
		if (k == 200 && n_builds.load() * 2 > k) {
			int two = 2;
			if (g_mode.compare_exchange_strong(two, 1))
				Log("conceptbfs: remembered walk off for this session: %lld walks built in %lld finds (the graph changes "
				    "between finds), the walk without the deque only", n_builds.load(), k);
		}
	} else {
		mine = cbfs::Find(root, **name, o, q, &visited);
	}
	depth--;
	const long long t1 = Now();
	n_calls++;
	n_nodes += (long long)visited;
	n_ticks += t1 - t0;
	if (mine) n_found++;
	thread_local long long callNo = 0;
	const bool check = g_checkLeft.load(std::memory_order_relaxed) > 0 || (++callNo & 63) == 0;
	if (check) {   // self-check: the game's own answer, then its reference back
		void* game = nullptr;
		const long long a = Now();
		g_orig(root, &game, name);
		n_origTicks += Now() - a;
		n_checked++;
		if (game) g_release(game);
		if (game != mine) {
			n_bad++;
			if (g_mode.exchange(0))
				Log("conceptbfs: MISMATCH for \"%.80s\" (ours %p, the game's %p, %zu nodes looked at) - off for this session",
				    **name, mine, game, visited);
		} else if (g_checkLeft.load() > 0 && g_checkLeft.fetch_sub(1) == 1) {
			Log("conceptbfs: self-check done (%lld finds matched the game's: ours %.1f ms, the game's %.1f ms), ours only now",
			    n_checked.load(), 1000.0 * n_ticks.load() / g_freq, 1000.0 * n_origTicks.load() / g_freq);
		}
	}
	*out = mine;
	return out;
}

DWORD WINAPI Reporter(LPVOID) {
	long long last = 0;
	for (int i = 0; i < 180; i++) {   // every 5 s while it runs, the first 15 minutes
		Sleep(5000);
		const long long c = n_calls.load();
		if (c == last) continue;
		last = c;
		Log("conceptbfs: %lld finds (%lld found), %lld nodes looked at, %.0f ms; checked %lld, mismatches %lld%s; "
		    "remembered walks: %lld finds, %lld roots, %lld walks built (%lld after a change)", c,
		    n_found.load(), n_nodes.load(), 1000.0 * n_ticks.load() / g_freq, n_checked.load(), n_bad.load(),
		    g_mode.load() ? "" : " (off)", n_idx.load(), n_roots.load(), n_builds.load(), n_stale.load());
	}
	return 0;
}

}  // namespace

void Conceptbfs_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_conceptbfs.txt";
	if (FILE* fp = fopen(file.c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	if (!g_mode) { Log("conceptbfs: off (fastload_conceptbfs.txt)"); return; }
	void* fn = (void*)(base + kRvaFind);
	const bool okF = !memcmp(fn, kFindBytes, sizeof kFindBytes);
	const bool okA = !memcmp((void*)(base + kRvaAddRef), kAddRefBytes, sizeof kAddRefBytes) &&
	                 !memcmp((void*)(base + kRvaRelease), kReleaseBytes, sizeof kReleaseBytes) &&
	                 !memcmp((void*)(base + kRvaContainer), kContainerBytes, sizeof kContainerBytes);
	if (!okF || !okA) {
		Log("conceptbfs: code check failed (find %d, helpers %d) - not installed (other game version?)", okF, okA);
		return;
	}
	g_addRef = (RefFn)(base + kRvaAddRef);
	g_release = (RefFn)(base + kRvaRelease);
	g_container = (ContainerFn)(base + kRvaContainer);
	const bool h = MH_CreateHook(fn, (void*)&HkFind, (void**)&g_orig) == MH_OK && MH_EnableHook(fn) == MH_OK;
	Log("conceptbfs: concept find-by-name without the deque %s%s (the first %lld finds, then every 64th, also run the "
	    "game's and must match)", h ? "on" : "FAILED", g_mode == 2 ? ", the walk remembered per root" : "", kCheck);
	if (h) CloseHandle(CreateThread(nullptr, 0, Reporter, nullptr, 0, nullptr));
}
