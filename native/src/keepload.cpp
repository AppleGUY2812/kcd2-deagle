// keepload: the map (and later the inventory) stays loaded after the menu closes (fastload.asi).
//
// WHGame 1.5.6 FUN_18056259c(panel, next screen, own screen) decides, for every Apse panel listener, whether the panel's
// Flash movie unloads (wh_ui_ApseUnloadMode, DAT_1855e2b24). In the game's mode 2 the map (screen 6) stays loaded only
// while you switch tabs: closing the menu (next screen 0) unloads it, and the next open reloads the movie and re-sends
// every marker (FUN_180dc95d4 map fill: 100-125 ms per open, menutime 2026-10-26). The hook answers "keep" for the
// screens in the mask when the menu closes; the panel then skips the reload and the fill on the next open. Screens: 1, 2 inventory,
// 4 character, 6 map, 7. "keepload <mask>" (fastload_keepload.txt), default 64 = the map only; 0 = the game's choice.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"
#include "early.h"
#include "ftpreload.h"
#include <atomic>
#include <mutex>
#include <vector>

namespace {
constexpr uintptr_t kRva = 0x56259c;
constexpr uint8_t kBytes[] = { 0x8b, 0x05, 0x82, 0x05, 0x08, 0x05, 0x85, 0xc0, 0x74, 0x3c, 0x84, 0xd2, 0x74, 0x51 };
typedef uint64_t(__fastcall* UnloadFn)(void* panel, char next, uint8_t own);
UnloadFn o_unload = nullptr;
unsigned g_mask = 1u << 6;
std::string g_file;
unsigned g_kept = 0, g_passed = 0;

uint64_t __fastcall HkUnload(void* panel, char next, uint8_t own) {
	uint64_t r = o_unload(panel, next, own);
	if (!(r & 0xff) || own >= 32 || !((g_mask >> own) & 1)) return r;
	// Only the menu closing (next screen 0) is overridden. Opening a kept screen in another mode (pickpocket, trade,
	// loot: the panel listener asks with the new screen) must reload it: overriding that left the inventory blank after
	// a pickpocket (2026-10-26, keepload 84), and after a sale with wh_ui_ApseUnloadMode 0 (2026-10-23).
	if (next != 0) {
		if (++g_passed <= 10 || g_passed % 50 == 0) Log("keepload: screen %u reloads for screen %d (the game's choice), %u times so far", own, next, g_passed);
		return r;
	}
	// The map after a fast travel reloads once: kept, it kept the trip's look (dark map, the piece in a circle) through
	// a travel encounter, a death and the save load after it (2026-10-27 19:03-19:06, the user's screenshot).
	// Never at the close that ends the trip: 214 unloaded the map the moment the walk ended and the arrival never
	// started (2026-10-28 15:04, a quest fast travel: black screen for good, the save load after it hung too). So the
	// reload waits for a later close, 10 s or more after the walk and the arrival wait are over.
	static unsigned seenTrips = 0;
	if (own == 6) {
		const unsigned trips = Ftpre_TripSeq();
		if (trips != seenTrips && Ftpre_TripQuietMs() >= 10000) {
			seenTrips = trips;
			Log("keepload: screen 6 reloads after a fast travel (the game's choice)");
			return r;
		}
	}
	if (++g_kept <= 10 || g_kept % 50 == 0) Log("keepload: screen %u kept loaded (next screen %d), %u times so far", own, next, g_kept);
	return r & ~(uint64_t)0xff;
}
// The map's quest-item tracker (vtable near 0x183ed8dd0: init FUN_18174e130, shutdown FUN_182afe83c, game events
// FUN_18085e104) listens to item events of the inventories it tracks (FUN_181f46f40 adds FUN_180dc7068 to each item).
// FUN_180c4ded4(tracker, item, inventory) then looks the inventory's name (CryString at +0x10) up in the tracker's
// std::map at +0xb0 (FUN_181507e80) and uses the result without checking it was found: for an inventory with no
// entry it reads the tree's head node as a value. The game only listens while the map is loaded, i.e. inside the
// menu; with the map kept loaded it also hears stash / loot moves in play, and one crashed the game (2026-10-27
// 00:22, WHGame+489c97 reading 0x19 under FUN_180c4ded4 during ItemTransfer). The guard walks the same tree read-only
// (MSVC _Tree: head at +0, root head+8, node left +0 / right +0x10 / isnil +0x19 / key char* +0x20; the game's
// byte compare = strcmp) and skips the call when the name isn't there: nothing is tracked for it, nothing to remove.
constexpr uintptr_t kRvaUntrack = 0xc4ded4;
constexpr uint8_t kUntrackBytes[] = { 0x40, 0x55, 0x53, 0x56, 0x57, 0x41, 0x56, 0x48, 0x8b, 0xec, 0x48, 0x83, 0xec, 0x60 };
typedef void(__fastcall* UntrackFn)(uintptr_t tracker, uintptr_t item, uintptr_t inv);
UntrackFn o_untrack = nullptr;
unsigned g_untrackSkips = 0;

// 1 found, 0 not found, -1 unreadable (a broken tree: the game's own walk would crash too)
int HasName(uintptr_t map, const char* key, char* shown, size_t n) {
	__try {
		strncpy_s(shown, n, key, _TRUNCATE);
		const uintptr_t head = *(const uintptr_t*)map;
		if (!head) return 0;
		uintptr_t node = *(const uintptr_t*)(head + 8);
		for (int guard = 0; node && node != head && !*(const uint8_t*)(node + 0x19) && guard < 100000; guard++) {
			const int c = strcmp(*(const char* const*)(node + 0x20), key);
			if (!c) return 1;
			node = c > 0 ? *(const uintptr_t*)node : *(const uintptr_t*)(node + 0x10);
		}
		return 0;
	} __except (1) { return -1; }
}

void __fastcall HkUntrack(uintptr_t tracker, uintptr_t item, uintptr_t inv) {
	char shown[80] = "?";
	int r = -1;
	__try { r = HasName(tracker + 0xb0, *(const char* const*)(inv + 0x10), shown, sizeof shown); } __except (1) { r = -1; }
	if (r != 1) {
		if (++g_untrackSkips <= 10 || g_untrackSkips % 50 == 0)
			Log("keepload: map quest tracker: %s '%s' - skipped the game's lookup that would read garbage (%u so far)",
			    r ? "unreadable entry for" : "no entry for inventory", shown, g_untrackSkips);
		return;
	}
	o_untrack(tracker, item, inv);
}
// The quest log hears item and container events from whichever thread makes them. NPCs change their items on the
// game's AI thread (the thread that loads the Scheduler table at start-up), so with the map kept loaded the quest log's
// tracking maps and the map marker list (FUN_180a193c0) were edited off the main thread while the main thread used
// them: three crashes 2026-10-27 00:22 / 00:50 / 00:55, all in FUN_180c4ded4 on a non-main thread, the last one at the
// same tower stairs twice. Here those callbacks run on the main thread: an event from another thread takes its own
// reference on the item / container (16-bit count at +0xc: add FUN_181e38f20, release FUN_181e39320) and waits in a
// queue that the frame tick plays in order. An object with count 0 can't be held (adding then releasing would delete
// it), so such an event runs where it came from, as before, and is counted.
constexpr uintptr_t kRvaItemEv = 0xdc7068;    // (questlog, type, item, container): item events
constexpr uintptr_t kRvaInvLoad = 0xdc8dc8;   // (questlog, type, container): type 1 subscribes a tracked container
constexpr uintptr_t kRvaInvDrop = 0x2b005b4;  // (questlog, type, container): types 0/2/3 unsubscribe it
constexpr uintptr_t kRvaAddRef = 0x1e38f20, kRvaRelease = 0x1e39320;
constexpr uint8_t kItemEvBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x20 };
constexpr uint8_t kInvLoadBytes[] = { 0x83, 0xfa, 0x01, 0x74, 0x01, 0xc3, 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57 };
constexpr uint8_t kInvDropBytes[] = { 0x48, 0x83, 0xec, 0x28, 0xf7, 0xc2, 0xfc, 0xff, 0xff, 0xff, 0x75, 0x0d };
constexpr uint8_t kAddRefBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x0f, 0xb7, 0x41, 0x0c };
constexpr uint8_t kReleaseBytes[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xd9, 0xb8, 0xff, 0xff, 0xff, 0xff };
typedef void(__fastcall* ItemEvFn)(uintptr_t ql, int type, uintptr_t item, uintptr_t inv);
typedef void(__fastcall* InvEvFn)(uintptr_t ql, int type, uintptr_t inv);
typedef void(__fastcall* RefFn)(uintptr_t obj);
ItemEvFn o_itemEv = nullptr;
InvEvFn o_invLoad = nullptr, o_invDrop = nullptr;
RefFn g_addRef = nullptr, g_release = nullptr;
DWORD g_mainTid = 0;
bool g_qlOn = false;

struct QlEvent { int kind; uintptr_t ql; int type; uintptr_t item, inv; DWORD tid; };   // kind 0 item, 1 load, 2 drop
std::mutex g_qlMu;
std::vector<QlEvent> g_qlQueue;
std::atomic<unsigned> g_qlQueued{0}, g_qlInline{0}, g_qlMaxBatch{0};

// A reference for the queued call; false when the object has none (count 0) or can't be read.
bool Hold(uintptr_t obj) {
	if (!obj) return true;
	short n = 0;
	__try { n = *(volatile const short*)(obj + 0xc); } __except (1) { return false; }
	if (n <= 0) return false;
	g_addRef(obj);
	return true;
}

void Drain() {
	std::vector<QlEvent> batch;
	{ std::lock_guard<std::mutex> l(g_qlMu); batch.swap(g_qlQueue); }
	if (batch.empty()) return;
	if (batch.size() > g_qlMaxBatch) g_qlMaxBatch = (unsigned)batch.size();
	for (const QlEvent& e : batch) {
		if (e.kind == 0) o_itemEv(e.ql, e.type, e.item, e.inv);
		else if (e.kind == 1) o_invLoad(e.ql, e.type, e.inv);
		else o_invDrop(e.ql, e.type, e.inv);
		if (e.item) g_release(e.item);
		if (e.inv) g_release(e.inv);
	}
}

// Queue the call for the main thread; false = it has to run here (an object without a reference).
bool Defer(int kind, uintptr_t ql, int type, uintptr_t item, uintptr_t inv) {
	if (!Hold(item)) { g_qlInline++; return false; }
	if (!Hold(inv)) { if (item) g_release(item); g_qlInline++; return false; }
	const unsigned n = ++g_qlQueued;
	if (n <= 5 || n % 500 == 0)
		Log("keepload: quest log: %s event (type %d) from thread %lu runs on the main thread (%u so far, %u ran in place: no reference to hold)",
		    kind == 0 ? "item" : "container", type, GetCurrentThreadId(), n, g_qlInline.load());
	std::lock_guard<std::mutex> l(g_qlMu);
	g_qlQueue.push_back({ kind, ql, type, item, inv, GetCurrentThreadId() });
	return true;
}

void __fastcall HkItemEv(uintptr_t ql, int type, uintptr_t item, uintptr_t inv) {
	if (GetCurrentThreadId() == g_mainTid) { Drain(); o_itemEv(ql, type, item, inv); return; }
	if (type != 2 && (type < 0 || type >= 5)) return;   // the game's function does nothing for these
	if (!Defer(0, ql, type, type == 2 ? item : 0, inv)) o_itemEv(ql, type, item, inv);
}
void __fastcall HkInvLoad(uintptr_t ql, int type, uintptr_t inv) {
	if (GetCurrentThreadId() == g_mainTid) { Drain(); o_invLoad(ql, type, inv); return; }
	if (type != 1) return;
	if (!Defer(1, ql, type, 0, inv)) o_invLoad(ql, type, inv);
}
void __fastcall HkInvDrop(uintptr_t ql, int type, uintptr_t inv) {
	if (GetCurrentThreadId() == g_mainTid) { Drain(); o_invDrop(ql, type, inv); return; }
	if ((type & ~3u) || type == 1) return;
	if (!Defer(2, ql, type, 0, inv)) o_invDrop(ql, type, inv);
}

bool HookAt(uintptr_t base, uintptr_t rva, const uint8_t* bytes, size_t n, void* hk, void** orig) {
	void* p = (void*)(base + rva);
	if (memcmp(p, bytes, n)) return false;
	return MH_CreateHook(p, hk, orig) == MH_OK && MH_EnableHook(p) == MH_OK;
}

void InstallQuestLogThreads(uintptr_t base) {
	g_mainTid = Early_MainThread();
	if (!g_mainTid || memcmp((void*)(base + kRvaAddRef), kAddRefBytes, sizeof kAddRefBytes) ||
	    memcmp((void*)(base + kRvaRelease), kReleaseBytes, sizeof kReleaseBytes)) {
		Log("keepload: quest log on the main thread: code check failed (main thread %lu) - not installed", g_mainTid);
		return;
	}
	g_addRef = (RefFn)(base + kRvaAddRef);
	g_release = (RefFn)(base + kRvaRelease);
	const bool a = HookAt(base, kRvaItemEv, kItemEvBytes, sizeof kItemEvBytes, (void*)&HkItemEv, (void**)&o_itemEv);
	const bool b = HookAt(base, kRvaInvLoad, kInvLoadBytes, sizeof kInvLoadBytes, (void*)&HkInvLoad, (void**)&o_invLoad);
	const bool c = HookAt(base, kRvaInvDrop, kInvDropBytes, sizeof kInvDropBytes, (void*)&HkInvDrop, (void**)&o_invDrop);
	g_qlOn = a && b && c;
	Log("keepload: quest log events from other threads run on the main thread (%lu): %s (item %d, container load %d, drop %d)",
	    g_mainTid, g_qlOn ? "on" : "NOT all hooked", a, b, c);
}
}  // namespace

// The frame tick (main thread): the quest log events other threads queued, in order.
void Keepload_Frame() {
	if (g_qlOn) Drain();
}

void Keepload_Install(uintptr_t base, HMODULE self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	g_file = path;
	g_file = g_file.substr(0, g_file.find_last_of('\\') + 1) + "fastload_keepload.txt";
	if (FILE* fp = fopen(g_file.c_str(), "r")) {
		unsigned m;
		if (fscanf(fp, "%u", &m) == 1) g_mask = m;
		fclose(fp);
	}
	void* p = (void*)(base + kRva);
	if (memcmp(p, kBytes, sizeof kBytes)) { Log("keepload: code check failed - not installed (other game version?)"); return; }
	const bool h = MH_CreateHook(p, (void*)&HkUnload, (void**)&o_unload) == MH_OK && MH_EnableHook(p) == MH_OK;
	Log("keepload: %s, mask %u (64 = the map stays loaded after closing; 0 = the game's choice)", h ? "on" : "hook FAILED", g_mask);
	void* u = (void*)(base + kRvaUntrack);
	if (memcmp(u, kUntrackBytes, sizeof kUntrackBytes)) { Log("keepload: map quest tracker guard: code check failed - not installed"); return; }
	const bool g = MH_CreateHook(u, (void*)&HkUntrack, (void**)&o_untrack) == MH_OK && MH_EnableHook(u) == MH_OK;
	Log("keepload: map quest tracker guard %s (an item event for an untracked inventory no longer reads garbage)", g ? "on" : "hook FAILED");
	InstallQuestLogThreads(base);
}

void Keepload_Command(const char* args) {
	unsigned m;
	if (args && sscanf_s(args, "%u", &m) == 1) {
		g_mask = m;
		if (FILE* fp = fopen(g_file.c_str(), "w")) { fprintf(fp, "%u\n", m); fclose(fp); }
	}
	Log("keepload: mask %u (64 = map, 4 = inventory, 16 = character; 0 = the game's choice)", g_mask);
}
