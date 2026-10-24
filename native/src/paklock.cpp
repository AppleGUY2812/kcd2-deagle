// CryPak reads outside the open-files lock: see paklock.h.
#include "paklock.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// ---- WHGame.dll 1.5.6 (decompile 2026-10-24; bytes read from the installed DLL)
constexpr uintptr_t kRvaFReadRaw = 0x4610a4;     // CCryPak::FReadRaw(pak, buf, size, count, handle)
const unsigned char kFReadRawBytes[] = { 0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x6c,0x24,0x18,0x56,0x57,0x41,0x56 };
constexpr uintptr_t kRvaLockModify = 0xc35288;   // CryReadModifyLock::LockModify(lock): mutex, modifyCount++, wait for readCount 0
const unsigned char kLockModifyBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20 };
constexpr uintptr_t kRvaLockRead = 0x23c9b0c;    // AutoReadLock ctor(tok, lock): mutex, readCount++, mutex released
const unsigned char kLockReadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x48,0x8b,0xf9,0x48,0x89,0x11 };
constexpr uintptr_t kRvaUnlockRead = 0x506d34;   // UnlockRead(lock): readCount--, wakes a waiting modifier
const unsigned char kUnlockReadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
constexpr uintptr_t kRvaZipFRead = 0x461654;     // CZipPseudoFile::FRead(slot, buf, size, count): reads at slot.pos, advances it
constexpr uintptr_t kRvaOsRead = 0x4d7854;       // the non-pak path: (buf, size, count, handle)
constexpr uintptr_t kRvaStatBegin = 0x461170;    // the pak's read-time statistic (guard, pak) / (guard)
const unsigned char kStatBeginBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x89,0x11,0x48,0x8b,0xd9 };
constexpr uintptr_t kRvaStatEnd = 0x46119c;
const unsigned char kStatEndBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x20,0x48,0x8b,0x01,0x48,0x8b,0xd9 };
constexpr uintptr_t kOpenLock = 0x10, kSlotsBegin = 0x40, kSlotsEnd = 0x48;   // in CCryPak

struct Slot { uint32_t pos, pad; void* cached; uint32_t flags, pad2; };   // CZipPseudoFile in m_arrOpenFiles
static_assert(sizeof(Slot) == 0x18, "slot stride");

using FReadRawFn = size_t(__fastcall*)(uintptr_t pak, void* buf, size_t size, size_t count, intptr_t handle);
using LockModifyFn = uint64_t(__fastcall*)(void* lock);
using LockReadFn = void*(__fastcall*)(void** tok, void* lock);
using UnlockReadFn = void(__fastcall*)(void* lock);
using ZipFReadFn = size_t(__fastcall*)(Slot* slot, void* buf, size_t size, size_t count);
using OsReadFn = size_t(__fastcall*)(void* buf, size_t size, size_t count, intptr_t handle);
using StatBeginFn = void*(__fastcall*)(void* guard, uintptr_t pak);
using StatEndFn = void(__fastcall*)(void* guard);
using RefFn = void(__fastcall*)(void* obj);

FReadRawFn g_origRead = nullptr;
LockModifyFn g_origModify = nullptr;
LockReadFn g_lockRead = nullptr;
UnlockReadFn g_unlockRead = nullptr;
ZipFReadFn g_zipRead = nullptr;
OsReadFn g_osRead = nullptr;
StatBeginFn g_statBegin = nullptr;
StatEndFn g_statEnd = nullptr;

int g_mode = 1;
long long g_freq = 1;
std::atomic<uintptr_t> g_openLock{0};   // the pak's m_csOpenFiles, from the first FReadRaw
std::atomic<long long> g_outside{0}, g_outsideTicks{0}, g_inside{0}, g_writeBackMissed{0};
std::atomic<long long> g_waits{0}, g_waitTicks{0}, g_waitMax{0};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long t) { return (double)t * 1000.0 / g_freq; }
inline void AddRef(void* o) { ((RefFn)(*(void***)o)[1])(o); }    // CCachedFileData vtable +8: atomic ++refs
inline void Release(void* o) { ((RefFn)(*(void***)o)[2])(o); }   // +0x10: --refs, frees under its owner's lock at 0

size_t __fastcall HkFReadRaw(uintptr_t pak, void* buf, size_t size, size_t count, intptr_t handle) {
	if (g_mode != 1) { g_openLock = pak + kOpenLock; return g_origRead(pak, buf, size, count, handle); }
	alignas(8) unsigned char guard[16];
	g_statBegin(guard, pak);
	void* lock = (void*)(pak + kOpenLock);
	g_openLock = (uintptr_t)lock;
	void* tok = nullptr;
	g_lockRead(&tok, lock);
	const size_t i = (size_t)handle - 1;
	size_t r;
	uintptr_t b = *(uintptr_t*)(pak + kSlotsBegin), e = *(uintptr_t*)(pak + kSlotsEnd);
	if (i < (e - b) / sizeof(Slot)) {
		Slot s = *(Slot*)(b + i * sizeof(Slot));
		if (!s.cached) {   // FRead returns 0 at once: nothing to take out of the lock
			r = g_zipRead((Slot*)(b + i * sizeof(Slot)), buf, size, count);
			g_unlockRead(lock);
			g_inside++;
		} else {
			AddRef(s.cached);
			g_unlockRead(lock);
			const long long t = Now();
			r = g_zipRead(&s, buf, size, count);
			g_outsideTicks += Now() - t;
			g_outside++;
			g_lockRead(&tok, lock);
			b = *(uintptr_t*)(pak + kSlotsBegin); e = *(uintptr_t*)(pak + kSlotsEnd);
			Slot* now = i < (e - b) / sizeof(Slot) ? (Slot*)(b + i * sizeof(Slot)) : nullptr;
			if (now && now->cached == s.cached) now->pos = s.pos;
			else g_writeBackMissed++;   // closed (and maybe reopened) while we read: the game's lock would have made FClose wait
			g_unlockRead(lock);
			Release(s.cached);
		}
	} else {
		g_unlockRead(lock);
		r = g_osRead(buf, size, count, handle);
	}
	g_statEnd(guard);
	return r;
}

uint64_t __fastcall HkLockModify(void* lock) {
	if ((uintptr_t)lock != g_openLock.load(std::memory_order_relaxed)) return g_origModify(lock);
	const long long t = Now();
	const uint64_t r = g_origModify(lock);
	const long long dt = Now() - t;
	g_waits++;
	g_waitTicks += dt;
	long long m = g_waitMax.load();
	while (dt > m && !g_waitMax.compare_exchange_weak(m, dt)) {}
	return r;
}

bool Hook(uintptr_t base, uintptr_t rva, const unsigned char* bytes, size_t n, void* detour, void** orig, const char* what) {
	void* p = (void*)(base + rva);
	if (memcmp(p, bytes, n)) { Log("paklock: %s: code differs - not hooked", what); return false; }
	if (MH_CreateHook(p, detour, orig) != MH_OK || MH_EnableHook(p) != MH_OK) { Log("paklock: %s: hook FAILED", what); return false; }
	return true;
}

}  // namespace

void Paklock_Report(const char* when) {
	Log("paklock: %s: reads outside the lock %lld (%.0f ms of reading), inside %lld, position not written back %lld; "
	    "open-files modify waits %lld, %.0f ms, longest %.1f ms (all threads)", when, g_outside.load(), Ms(g_outsideTicks.load()),
	    g_inside.load(), g_writeBackMissed.load(), g_waits.load(), Ms(g_waitTicks.load()), Ms(g_waitMax.load()));
	g_outside = 0; g_outsideTicks = 0; g_inside = 0; g_writeBackMissed = 0; g_waits = 0; g_waitTicks = 0; g_waitMax = 0;
}

void Paklock_Install(uintptr_t base, void* self) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir = path;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((dir + "fastload_paklock.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m == 0 ? 0 : 1;
		fclose(fp);
	}
	// the functions FReadRaw calls must be the ones read in the decompile, or the replacement is off
	const bool calls = !memcmp((void*)(base + kRvaLockRead), kLockReadBytes, sizeof kLockReadBytes) &&
	                   !memcmp((void*)(base + kRvaUnlockRead), kUnlockReadBytes, sizeof kUnlockReadBytes) &&
	                   !memcmp((void*)(base + kRvaStatBegin), kStatBeginBytes, sizeof kStatBeginBytes) &&
	                   !memcmp((void*)(base + kRvaStatEnd), kStatEndBytes, sizeof kStatEndBytes);
	if (!calls && g_mode == 1) { g_mode = 0; Log("paklock: the lock / statistic functions differ: replacement off"); }
	g_lockRead = (LockReadFn)(base + kRvaLockRead);
	g_unlockRead = (UnlockReadFn)(base + kRvaUnlockRead);
	g_zipRead = (ZipFReadFn)(base + kRvaZipFRead);
	g_osRead = (OsReadFn)(base + kRvaOsRead);
	g_statBegin = (StatBeginFn)(base + kRvaStatBegin);
	g_statEnd = (StatEndFn)(base + kRvaStatEnd);
	const bool rd = Hook(base, kRvaFReadRaw, kFReadRawBytes, sizeof kFReadRawBytes, (void*)&HkFReadRaw, (void**)&g_origRead, "FReadRaw");
	const bool md = Hook(base, kRvaLockModify, kLockModifyBytes, sizeof kLockModifyBytes, (void*)&HkLockModify, (void**)&g_origModify, "LockModify");
	if (!rd) g_mode = 0;
	Log("paklock: mode %d (%s; fastload_paklock.txt), FReadRaw hook %s, modify-wait timing %s", g_mode,
	    g_mode == 1 ? "pak reads outside the open-files lock" : "the game's own reads", rd ? "on" : "OFF", md ? "on" : "OFF");
}
