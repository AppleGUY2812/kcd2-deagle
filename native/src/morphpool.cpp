// The heads' facial morph data parsed on a pool of threads: see morphpool.h.
#define NOMINMAX
#include "morphpool.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaDone = 0x6e3820;     // void (cache*, holder* {stream*, count*} by value): the completion
constexpr uintptr_t kRvaParse = 0x6e3ac4;    // shared_ptr<S_Model>* (out*, const char* buf, size): XML -> model
constexpr uintptr_t kRvaSpCopy = 0x39ae88;   // shared_ptr* (out*, const shared_ptr*): copy (count + 1)
constexpr uintptr_t kRvaCryStr = 0x4f66cc;   // CryString* (out*, const char*)
constexpr uintptr_t kRvaInsert = 0x6e3764;   // void (cache*, const CryString* name, shared_ptr* by value): under cache+0x48
constexpr uintptr_t kRvaCryRel = 0x4fd898;   // void (header*): a CryString's release (its chars - 0xc)
constexpr uintptr_t kRvaDecref = 0x4f6588;   // void (count*): a shared pointer's release
// the whole completion: the calls below are the ones it makes, in its order (its error path: 0x182223208)
const unsigned char kDoneBytes[] = {
	0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x40,0x48,0x8b,0xf1,0x48,0x8b,
	0xfa,0x48,0x8b,0x0a,0x48,0x8b,0x01,0xff,0x10,0x48,0x8b,0x0f,0x48,0x8b,0x11,0x84,0xc0,0x0f,0x85,0xbd,
	0xf9,0xb3,0x01,0xff,0x52,0x10,0x48,0x8b,0x0f,0x8b,0xd8,0x48,0x8b,0x01,0xff,0x50,0x18,0x44,0x8b,0xc3,
	0x48,0x8d,0x4c,0x24,0x20,0x48,0x8b,0xd0,0xe8,0x5b,0x02,0x00,0x00,0x48,0x8b,0x0f,0x48,0x8b,0x01,0xff,
	0x90,0x80,0x00,0x00,0x00,0x48,0x8d,0x54,0x24,0x20,0x48,0x8d,0x4c,0x24,0x30,0xe8,0x04,0x76,0xcb,0xff,
	0x48,0x8b,0x0f,0x48,0x8b,0xd8,0x48,0x8b,0x11,0xff,0x52,0x78,0x48,0x8b,0xd0,0x48,0x8d,0x4c,0x24,0x58,
	0xe8,0x2f,0x2e,0xe1,0xff,0x4c,0x8b,0xc3,0x48,0x8d,0x54,0x24,0x58,0x48,0x8b,0xce,0xe8,0xb7,0xfe,0xff,
	0xff,0x48,0x8b,0x4c,0x24,0x58,0x48,0x83,0xc1,0xf4,0xe8,0xdd,0x9f,0xe1,0xff,0x48,0x8b,0x4c,0x24,0x28,
	0x48,0x85,0xc9,0x74,0x05,0xe8,0xbe,0x2c,0xe1,0xff,0x48,0x8b,0x4f,0x08,0x48,0x85,0xc9,0x74,0x05,0xe8,
	0xb0,0x2c,0xe1,0xff,0x48,0x8b,0x5c,0x24,0x50,0x48,0x8b,0x74,0x24,0x60,0x48,0x83,0xc4,0x40,0x5f,0xc3,
};
const unsigned char kErrBytes[] = { 0xff,0x92,0x80,0x00,0x00,0x00,0x90,0xe9,0xb6,0x06,0x4c,0xfe };   // 0x2223208
const unsigned char kParseBytes[] = { 0x48,0x8b,0xc4,0x48,0x89,0x58,0x08,0x48,0x89,0x70,0x10,0x48,0x89,0x78,0x18,0x55 };
const unsigned char kSpCopyBytes[] = { 0x48,0xc7,0x01,0x00,0x00,0x00,0x00,0x48,0xc7,0x41,0x08,0x00,0x00,0x00,0x00,0x48 };
const unsigned char kCryStrBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x48,0x89,0x74,0x24,0x20,0x57 };
const unsigned char kInsertBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56,0x48,0x83 };
const unsigned char kCryRelBytes[] = { 0x48,0x83,0xec,0x28,0xe8,0xe7,0xfa,0xee,0x01,0x85,0xc0,0x78,0x23,0x83,0xc8,0xff };
const unsigned char kDecrefBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0xd9,0x83,0xc8,0xff,0xf0,0x0f,0xc1,0x41 };

struct Holder { void* stream; void* count; };   // std::shared_ptr<stream>: the object (vtable first), its count block
struct Sp { void* p; void* count; };

using DoneFn = void(__fastcall*)(void* cache, Holder* h);
using ParseFn = Sp*(__fastcall*)(Sp* out, const char* buf, uint64_t size);
using SpCopyFn = Sp*(__fastcall*)(Sp* out, const Sp* from);
using CryStrFn = void*(__fastcall*)(void** out, const char* s);
using InsertFn = void(__fastcall*)(void* cache, void** name, Sp* model);
using CryRelFn = void(__fastcall*)(void* header);
using DecrefFn = void(__fastcall*)(void* count);
using BoolFn = char(__fastcall*)(void*);       // the stream's +0x00: failed
using SizeFn = uint32_t(__fastcall*)(void*);   // +0x10: bytes
using PtrFn = const char*(__fastcall*)(void*); // +0x18: the buffer, +0x78: the file name
using VoidFn = void(__fastcall*)(void*);       // +0x80: frees the buffer

DoneFn g_orig = nullptr;
ParseFn g_parse = nullptr;
SpCopyFn g_spCopy = nullptr;
CryStrFn g_cryStr = nullptr;
InsertFn g_insert = nullptr;
CryRelFn g_cryRel = nullptr;
DecrefFn g_decref = nullptr;

int g_mode = 0, g_threads = 4;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long ticks) { return (double)ticks * 1000.0 / g_freq; }

struct Job { void* cache; std::string name; std::unique_ptr<char[]> data; uint32_t size; long long queued; };

std::mutex g_mu;
std::condition_variable g_cv;
std::deque<Job*> g_queue;
std::atomic<int> g_pending{ 0 };
std::atomic<long long> g_pendingBytes{ 0 };
std::atomic<bool> g_started{ false };
std::atomic<bool> g_gate{ false };
constexpr long long kMaxBytes = 256ll << 20;

// one load screen's numbers (reset when it starts; the callback thread and the pool add, Main logs)
struct Acc {
	std::atomic<long> files{ 0 }, inl{ 0 }, failed{ 0 }, overflow{ 0 };
	std::atomic<long long> bytes{ 0 }, cbTicks{ 0 }, parseTicks{ 0 }, maxParse{ 0 }, waitTicks{ 0 }, maxWait{ 0 };
	std::atomic<long long> first{ 0 }, last{ 0 };
	std::atomic<int> maxPending{ 0 };
	void Reset() {
		files = 0; inl = 0; failed = 0; overflow = 0; bytes = 0; cbTicks = 0; parseTicks = 0; maxParse = 0;
		waitTicks = 0; maxWait = 0; first = 0; last = 0; maxPending = 0;
	}
};
Acc g_acc;
long long g_gateT0 = 0;
long g_sessionFiles = 0;

void Max(std::atomic<long long>& m, long long v) { for (long long o = m.load(); v > o && !m.compare_exchange_weak(o, v);) {} }

void Run(Job* j) {
	const long long a = Now();
	Max(g_acc.maxWait, a - j->queued);
	g_acc.waitTicks += a - j->queued;
	// the calls the game makes after its own parse (FUN_1806e3820 +0x44..+0xb3), from the copy
	Sp model{};
	g_parse(&model, j->data.get(), j->size);
	j->data.reset();
	g_pendingBytes -= j->size;
	Sp copy{};
	g_spCopy(&copy, &model);
	void* name = nullptr;
	g_cryStr(&name, j->name.c_str());
	g_insert(j->cache, &name, &copy);   // takes the cache's lock; releases the copy
	if (name) g_cryRel((char*)name - 0xc);
	if (model.count) g_decref(model.count);
	const long long b = Now();
	g_acc.parseTicks += b - a;
	Max(g_acc.maxParse, b - a);
	g_acc.last = b;
	delete j;
	g_pending--;
}

DWORD WINAPI Worker(LPVOID) {
	SetThreadDescription(GetCurrentThread(), L"fastload morphpool");
	for (;;) {
		Job* j = nullptr;
		{
			std::unique_lock<std::mutex> lk(g_mu);
			g_cv.wait(lk, [] { return !g_queue.empty(); });
			j = g_queue.front();
			g_queue.pop_front();
		}
		Run(j);
	}
}

void Start() {
	bool f = false;
	if (!g_started.compare_exchange_strong(f, true)) return;
	for (int i = 0; i < g_threads; i++) CloseHandle(CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr));
	Log("morphpool: %d pool threads started", g_threads);
}

void __fastcall HkDone(void* cache, Holder* h) {
	const long long a = Now();
	const bool gate = g_gate.load(std::memory_order_relaxed);
	const bool pool = g_mode == 2 || (g_mode == 1 && gate);
	void* s = h ? h->stream : nullptr;
	if (!pool || !s) {
		g_orig(cache, h);
		if (gate) { g_acc.inl++; g_acc.cbTicks += Now() - a; }
		return;
	}
	void** vt = *(void***)s;
	const char* buf = nullptr;
	const char* file = nullptr;
	uint32_t n = 0;
	if (((BoolFn)vt[0])(s) || !(n = ((SizeFn)vt[2])(s)) || !(buf = ((PtrFn)vt[3])(s)) || !(file = ((PtrFn)vt[15])(s)) || !*file) {
		g_orig(cache, h);   // failed or empty: the game's way
		if (gate) { g_acc.failed++; g_acc.cbTicks += Now() - a; }
		return;
	}
	if (g_pendingBytes.load() + n > kMaxBytes) {
		g_orig(cache, h);
		if (gate) { g_acc.overflow++; g_acc.cbTicks += Now() - a; }
		return;
	}
	Job* j = new Job{ cache, file, std::unique_ptr<char[]>(new char[n]), n, 0 };
	memcpy(j->data.get(), buf, n);
	((VoidFn)vt[16])(s);                 // the game's error path: the stream's buffer freed,
	if (h->count) g_decref(h->count);    // the stream (passed by value) released
	Start();
	g_pendingBytes += n;
	const int p = ++g_pending;
	j->queued = Now();
	{
		std::lock_guard<std::mutex> lk(g_mu);
		g_queue.push_back(j);
	}
	g_cv.notify_one();
	g_acc.files++;
	g_acc.bytes += n;
	long long z = 0;
	g_acc.first.compare_exchange_strong(z, a);
	for (int o = g_acc.maxPending.load(); p > o && !g_acc.maxPending.compare_exchange_weak(o, p);) {}
	g_acc.cbTicks += Now() - a;
	if (!g_sessionFiles++) Log("morphpool: first file pooled: %s (%u KB)", file, n >> 10);
}

void Report(double screenS) {
	const long files = g_acc.files, inl = g_acc.inl, failed = g_acc.failed, over = g_acc.overflow;
	if (!files && !inl && !failed && !over) return;
	if (g_mode == 0 || (!files && inl)) {
		Log("morphpool: %s - while the load screen waited %.1f s, the stream callback thread spent %.0f ms on %ld morph files "
		    "(fastload_morphpool.txt 1: on %d pool threads)", g_mode == 0 ? "timed only" : "not pooled", screenS,
		    Ms(g_acc.cbTicks), inl + failed + over, g_threads);
		return;
	}
	const double span = g_acc.first && g_acc.last ? Ms(g_acc.last - g_acc.first) : 0;
	Log("morphpool: while the load screen waited %.1f s: %ld morph files (%.1f MB) parsed on %d pool threads, %.0f ms of "
	    "parsing (%.1f ms at most), first to last %.0f ms; the callback thread's part %.0f ms (copies); queue waits %.1f ms "
	    "avg (%.1f at most), at most %d files queued; the game's way: %ld (failed %ld, over 256 MB %ld); %d still pending",
	    screenS, files, g_acc.bytes / 1048576.0, g_threads, Ms(g_acc.parseTicks), Ms(g_acc.maxParse), span, Ms(g_acc.cbTicks),
	    files ? Ms(g_acc.waitTicks) / files : 0.0, Ms(g_acc.maxWait), g_acc.maxPending.load(), inl + failed + over, failed,
	    over, g_pending.load());
}

}  // namespace

void Morphpool_SetGate(bool on) {
	if (on == g_gate.load(std::memory_order_relaxed)) return;
	if (on) {
		g_acc.Reset();
		g_gateT0 = Now();
		g_gate = true;
	} else {
		g_gate = false;
		Report(Ms(Now() - g_gateT0) / 1000.0);
	}
}

int Morphpool_Pending() { return g_pending.load(std::memory_order_acquire); }

void Morphpool_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* fp = fopen((dir + "fastload_morphpool.txt").c_str(), "r")) {
		if (fscanf(fp, "%d %d", &g_mode, &g_threads) < 1) g_mode = 0;
		fclose(fp);
	}
	if (g_mode < 0 || g_mode > 2) g_mode = 0;
	g_threads = std::clamp(g_threads, 1, 16);
	const struct { uintptr_t rva; const unsigned char* b; size_t n; } checks[] = {
		{ kRvaDone, kDoneBytes, sizeof kDoneBytes }, { 0x2223208, kErrBytes, sizeof kErrBytes },
		{ kRvaParse, kParseBytes, sizeof kParseBytes }, { kRvaSpCopy, kSpCopyBytes, sizeof kSpCopyBytes },
		{ kRvaCryStr, kCryStrBytes, sizeof kCryStrBytes }, { kRvaInsert, kInsertBytes, sizeof kInsertBytes },
		{ kRvaCryRel, kCryRelBytes, sizeof kCryRelBytes }, { kRvaDecref, kDecrefBytes, sizeof kDecrefBytes },
	};
	for (auto& c : checks)
		if (memcmp((void*)(base + c.rva), c.b, c.n)) {
			Log("morphpool: the morph data code differs at +%llx (another game version?) - off", (unsigned long long)c.rva);
			return;
		}
	g_parse = (ParseFn)(base + kRvaParse);
	g_spCopy = (SpCopyFn)(base + kRvaSpCopy);
	g_cryStr = (CryStrFn)(base + kRvaCryStr);
	g_insert = (InsertFn)(base + kRvaInsert);
	g_cryRel = (CryRelFn)(base + kRvaCryRel);
	g_decref = (DecrefFn)(base + kRvaDecref);
	void* t = (void*)(base + kRvaDone);
	if (MH_CreateHook(t, (void*)&HkDone, (void**)&g_orig) != MH_OK || MH_EnableHook(t) != MH_OK) {
		Log("morphpool: hook failed - off");
		return;
	}
	Log("morphpool: mode %d (%s)", g_mode,
	    g_mode == 1 ? "the heads' morph data parsed on a pool while a load screen waits"
	    : g_mode == 2 ? "the heads' morph data parsed on a pool always"
	                  : "timed only: the game's way, its time while a load screen waits logged");
	if (g_mode) Log("morphpool: %d pool threads once the first file comes", g_threads);
}
