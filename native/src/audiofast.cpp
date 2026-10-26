// The sound system's start-up requests without Main waiting for them: see audiofast.h.
#include "audiofast.h"
#include "log.h"
#include "early.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaVtable = 0x3d281e0;    // CAudioSystem's vtable
constexpr uintptr_t kRvaSignal = 0x500f18;     // FUN_180500de8: mov rcx,[rbp+0x5e8]; call [SetEvent] (a blocking request done)
const unsigned char kSignalBytes[] = { 0x48,0x8b,0x8d,0xe8,0x05,0x00,0x00,0xff,0x15 };
constexpr size_t kEventOff = 0x5e8;            // the audio system's event for blocking requests
constexpr int kPushSlot = 3;
// The four pushes, by return address (each a call [rax+0x18]): FUN_181810b10's, then FUN_181174530's three
constexpr uintptr_t kSkipRet[4] = { 0x1810c2b, 0x11745ca, 0x1174605, 0x1174648 };
const char* const kSkipName[4] = { "the FMOD implementation", "the global controls", "the global preloads", "the global preload" };
// Slots that read the audio system's tables (or its impl) on the calling thread, with the expected function, and the
// push itself. All take integer / pointer arguments only (at most 4). Not slot 17 (+0x690, the XML folder set by
// Initialize on Main): FUN_181174530 reads it before its three pushes.
struct Slot { int n; uintptr_t rva; const char* name; };
constexpr Slot kSlots[] = {
	{ 3, 0x763660, "PushRequest" }, { 6, 0x8004d4, "ExternalUpdate" }, { 7, 0x3b8cb4, "a trigger id" },
	{ 8, 0x480a50, "a parameter id" }, { 9, 0x3b949c, "a switch id" }, { 10, 0x3b96b8, "a switch state id" },
	{ 11, 0x1811e58, "a preload id" }, { 12, 0x3b77fc, "an environment id" }, { 13, 0x39b725c, "a listener reserved" },
	{ 14, 0x39b71e8, "a listener released" }, { 16, 0x1ab44e0, "the impl's info" },
	{ 22, 0x39b7334, "a parameter id (2)" }, { 23, 0x5330e8, "the impl (+0x248)" }, { 24, 0x39b6ec4, "the impl (+0x250)" },
};
constexpr int kNumSlots = sizeof kSlots / sizeof kSlots[0];

using Fn4 = uintptr_t(__fastcall*)(uintptr_t, uintptr_t, uintptr_t, uintptr_t);
using PushFn = void(__fastcall*)(uintptr_t sys, uint32_t* req);
using SetEventFn = BOOL(WINAPI*)(HANDLE);

uintptr_t g_base = 0;
void** g_vt = nullptr;
Fn4 g_orig[32] = {};
void* g_mine[32] = {};
void** g_iat = nullptr;
SetEventFn g_setEventOrig = nullptr;
std::atomic<bool> g_on{ false };
bool g_reported = true;
DWORD g_mainTid = 0;
std::atomic<DWORD> g_audioTid{ 0 };
std::atomic<HANDLE> g_evt{ nullptr };
HANDLE g_wake = nullptr;
long long g_freq = 1;
// pushes whose wait was skipped, and their signals counted (only the audio thread counts)
std::atomic<long> g_pending{ 0 }, g_done{ 0 };
struct SkipRec { int site; long long pushed, done; };
SkipRec g_skips[4] = {};
unsigned g_seen = 0;   // Main only
struct HoldRec { int slot; DWORD tid; long long t0, t1; bool timedOut; };
constexpr int kMaxHolds = 48;
HoldRec g_holds[kMaxHolds] = {};
std::atomic<long> g_nHolds{ 0 };
std::atomic<long long> g_holdTicksMain{ 0 };

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline bool Busy() { return g_done.load(std::memory_order_acquire) < g_pending.load(std::memory_order_acquire); }

bool SetPtr(void** at, void* v) {
	DWORD old = 0;
	if (!VirtualProtect(at, sizeof(void*), PAGE_READWRITE, &old)) return false;
	InterlockedExchangePointer(at, v);
	VirtualProtect(at, sizeof(void*), old, &old);
	return true;
}

bool IsAudioThread(DWORD tid) {
	if (tid == g_audioTid.load()) return true;
	using DescFn = HRESULT(WINAPI*)(HANDLE, PWSTR*);
	static DescFn desc = (DescFn)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
	PWSTR w = nullptr;
	bool audio = false;
	if (desc && SUCCEEDED(desc(GetCurrentThread(), &w)) && w) {
		audio = !wcsncmp(w, L"AudioThread", 11) || !wcsncmp(w, L"FMOD", 4);
		LocalFree(w);
	}
	return audio;
}

// The calling thread waits until every skipped request is done (2 s at most: a lock the audio thread needs could be
// held by this thread; the four took 0.6 s in all). Never on the audio thread itself.
void Hold(int slot) {
	if (!Busy()) return;
	const DWORD tid = GetCurrentThreadId();
	if (IsAudioThread(tid)) return;
	const long long t0 = Now();
	bool timedOut = false;
	while (Busy()) {
		if (Now() - t0 > 2 * g_freq) { timedOut = true; break; }
		WaitForSingleObject(g_wake, 5);
	}
	const long long t1 = Now();
	if (tid == g_mainTid) g_holdTicksMain += t1 - t0;
	const long i = g_nHolds++;
	if (i < kMaxHolds) g_holds[i] = { slot, tid, t0, t1, timedOut };
}

// CryMovie's start-up (FUN_1807eb10c) looks up the 16 TrackView parameter ids ("TrackViewParam1".."16") into a global
// table and ignores the result: the first audio system call after the four requests (boots 158-159: Main waited 418-526 ms
// there, 34-101 ms after the pushes). While the requests run, those 16 get at once the id the lookup gives a known name
// (CRC32 of the lower-case name, FUN_180480a5c); at the first frame, after the requests, the game's own lookup runs for each
// and writes the real answer (0 and its "INVALID_AUDIO_CONTROL_ID" line for an unknown name). Nothing plays a TrackView
// sequence before the first frame.
constexpr uintptr_t kRvaTrackViewIdsRet = 0x7eb181;   // after its call [rax+0x40] (slot 8) in the 16-name loop
constexpr uintptr_t kRvaCrcTable = 0x40999f0;         // the lookup's CRC32 table
struct Deferred { uintptr_t sys; const char* name; uint32_t* out; };
constexpr int kMaxDeferred = 16;
Deferred g_deferred[kMaxDeferred] = {};
int g_nDeferred = 0;   // Main only
bool g_deferOk = false;

bool Defer(uintptr_t sys, uintptr_t name, uintptr_t out) {
	if (!g_deferOk || !name || !out || g_nDeferred >= kMaxDeferred || GetCurrentThreadId() != g_mainTid) return false;
	const uint32_t* tbl = (const uint32_t*)(g_base + kRvaCrcTable);
	uint32_t crc = 0xffffffff;
	for (const unsigned char* p = (const unsigned char*)name; *p; p++) {
		const unsigned char ch = (*p >= 'A' && *p <= 'Z') ? *p + 0x20 : *p;   // as the game: ASCII upper case only
		crc = crc >> 8 ^ tbl[(ch ^ crc) & 0xff];
	}
	*(uint32_t*)out = ~crc;
	g_deferred[g_nDeferred++] = { sys, (const char*)name, (uint32_t*)out };
	return true;
}

template <int N> uintptr_t __fastcall Gate(uintptr_t a, uintptr_t b, uintptr_t c, uintptr_t d) {
	if (Busy()) {
		if (N == 8 && (uintptr_t)_ReturnAddress() == g_base + kRvaTrackViewIdsRet && Defer(a, b, c)) return 1;
		Hold(N);
	}
	return g_orig[N](a, b, c, d);
}

// The audio thread: a blocking request is done. The skipped ones are counted, and not signalled (nobody waits).
BOOL WINAPI HkSetEvent(HANDLE h) {
	if (h && h == g_evt.load(std::memory_order_acquire)) {
		const long d = g_done.load(std::memory_order_relaxed);
		if (d < g_pending.load(std::memory_order_acquire)) {
			g_audioTid = GetCurrentThreadId();
			if (d < 4) g_skips[d].done = Now();
			g_done.store(d + 1, std::memory_order_release);
			SetEvent(g_wake);
			return TRUE;
		}
	}
	return g_setEventOrig(h);
}

void __fastcall HkPush(uintptr_t sys, uint32_t* req) {
	const uintptr_t r = (uintptr_t)_ReturnAddress();
	int site = -1;
	for (int i = 0; i < 4; i++)
		if (r == g_base + kSkipRet[i]) site = i;
	const uint32_t flags = req ? *req : 0;
	if (site >= 0 && g_on.load() && (flags & 4) && !(g_seen & (1u << site)) && GetCurrentThreadId() == g_mainTid) {
		g_seen |= 1u << site;
		HANDLE ev = *(HANDLE*)(sys + kEventOff);
		HANDLE expect = nullptr;
		if (ev && (g_evt.compare_exchange_strong(expect, ev) || expect == ev)) {
			const long k = g_pending.load();
			if (k < 4) g_skips[k] = { site, Now(), 0 };
			g_pending.store(k + 1, std::memory_order_release);   // before the push: its signal can come at once
			SetEvent(ev);   // the push's own wait returns at once (a direct call: not counted)
			((PushFn)g_orig[kPushSlot])(sys, req);
			if (WaitForSingleObject(ev, 0) == WAIT_OBJECT_0) {   // the push did not wait: not queued
				ResetEvent(ev);
				g_pending--;
				Log("audiofast: %s was not queued (the queue full or the audio system shutting down)", kSkipName[site]);
			}
			return;
		}
	}
	// another blocking push shares the event. Thread-safe pushes (0x40) never wait: FMOD's callback threads make them,
	// and the audio thread can be waiting for FMOD
	if (flags & 4) Hold(kPushSlot);
	((PushFn)g_orig[kPushSlot])(sys, req);
}

// one gate function per slot number
template <int N> void* GateFor() { return (void*)&Gate<N>; }
void* GateOf(int n) {
	switch (n) {
	case 6: return GateFor<6>(); case 7: return GateFor<7>(); case 8: return GateFor<8>(); case 9: return GateFor<9>();
	case 10: return GateFor<10>(); case 11: return GateFor<11>(); case 12: return GateFor<12>(); case 13: return GateFor<13>();
	case 14: return GateFor<14>(); case 16: return GateFor<16>(); case 22: return GateFor<22>();
	case 23: return GateFor<23>(); case 24: return GateFor<24>();
	}
	return nullptr;
}

void** FindImport(uintptr_t base, const char* dll, const char* fn) {
	auto dos = (IMAGE_DOS_HEADER*)base;
	auto nt = (IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
	if (!dir.VirtualAddress) return nullptr;
	for (auto d = (IMAGE_IMPORT_DESCRIPTOR*)(base + dir.VirtualAddress); d->Name; d++) {
		if (_stricmp((const char*)(base + d->Name), dll)) continue;
		auto names = (IMAGE_THUNK_DATA64*)(base + (d->OriginalFirstThunk ? d->OriginalFirstThunk : d->FirstThunk));
		auto slots = (void**)(base + d->FirstThunk);
		for (size_t i = 0; names[i].u1.AddressOfData; i++) {
			if (IMAGE_SNAP_BY_ORDINAL64(names[i].u1.Ordinal)) continue;
			auto ibn = (IMAGE_IMPORT_BY_NAME*)(base + names[i].u1.AddressOfData);
			if (!strcmp((const char*)ibn->Name, fn)) return &slots[i];
		}
	}
	return nullptr;
}

void Restore() {
	for (const Slot& s : kSlots)
		if (g_vt[s.n] == g_mine[s.n]) SetPtr(&g_vt[s.n], (void*)g_orig[s.n]);
	if (g_iat && *g_iat == (void*)&HkSetEvent) SetPtr(g_iat, (void*)g_setEventOrig);
}

const char* SlotName(int n) {
	for (const Slot& s : kSlots)
		if (s.n == n) return s.name;
	return "?";
}

}  // namespace

void Audiofast_Install(uintptr_t base, void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	int mode = 0;
	if (FILE* fp = fopen((dir + "fastload_audiofast.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &mode) != 1) mode = 0;
		fclose(fp);
	}
	if (mode != 1 && mode != 2) {
		Log("audiofast: off (fastload_audiofast.txt 1 = the sound system's start-up requests without Main waiting, 2 = also "
		    "CryMovie's TrackView ids answered at once)");
		return;
	}
	g_base = base;
	g_vt = (void**)(base + kRvaVtable);
	for (const Slot& s : kSlots)
		if ((uintptr_t)g_vt[s.n] != base + s.rva) {
			Log("audiofast: the audio system's slot %d differs (another game version?) - off", s.n);
			return;
		}
	for (int i = 0; i < 4; i++)
		if (memcmp((void*)(base + kSkipRet[i] - 3), "\xff\x50\x18", 3)) { Log("audiofast: the sound init's push %d differs - off", i); return; }
	if (memcmp((void*)(base + kRvaSignal), kSignalBytes, sizeof kSignalBytes)) { Log("audiofast: the audio thread's signal differs - off"); return; }
	g_iat = FindImport(base, "KERNEL32.dll", "SetEvent");
	// the signal's call goes through this slot
	const uintptr_t callAt = base + kRvaSignal + 7;
	const uintptr_t used = callAt + 6 + *(int32_t*)(callAt + 2);
	if (!g_iat || (uintptr_t)g_iat != used) { Log("audiofast: the game's SetEvent import not found where its signal calls it - off"); return; }
	const uint32_t* crc = (const uint32_t*)(base + kRvaCrcTable);
	g_deferOk = mode == 2 && !memcmp((void*)(base + kRvaTrackViewIdsRet - 3), "\xff\x50\x40", 3) && crc[0] == 0 && crc[1] == 0x77073096 &&
	            crc[255] == 0x2d02ef8d;
	if (mode == 2 && !g_deferOk) Log("audiofast: CryMovie's TrackView id loop or the CRC table differs: those lookups wait as the rest");
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_mainTid = Early_MainThread();
	g_wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
	if (!g_wake || !g_mainTid) { Log("audiofast: no event or main thread - off"); return; }
	g_setEventOrig = (SetEventFn)*g_iat;
	for (const Slot& s : kSlots) {
		g_orig[s.n] = (Fn4)g_vt[s.n];
		g_mine[s.n] = s.n == kPushSlot ? (void*)&HkPush : GateOf(s.n);
	}
	if (!SetPtr(g_iat, (void*)&HkSetEvent)) { Log("audiofast: the SetEvent import is read-only - off"); return; }
	bool ok = true;
	for (const Slot& s : kSlots) ok = ok && g_mine[s.n] && SetPtr(&g_vt[s.n], g_mine[s.n]);
	if (!ok) { Restore(); Log("audiofast: the audio system's vtable could not be changed - off"); return; }
	g_on = true;
	g_reported = false;
	Log("audiofast: on (the sound system's 4 start-up requests without Main waiting; %d audio system slots wait for them; "
	    "%sfastload_audiofast.txt %d)", kNumSlots - 1, g_deferOk ? "CryMovie's 16 TrackView ids answered at once; " : "", mode);
}

void Audiofast_Frame() {
	if (g_reported) return;
	g_reported = true;
	Hold(-1);   // the first frame: everything done before the game runs on
	g_on = false;
	Restore();
	if (g_nDeferred) {   // the game's own lookup for the ids answered early, now that the tables are in
		int unknown = 0, changed = 0;
		const bool done = !Busy();
		for (int i = 0; done && i < g_nDeferred; i++) {
			const Deferred& r = g_deferred[i];
			const uint32_t early = *r.out;
			g_orig[8](r.sys, (uintptr_t)r.name, (uintptr_t)r.out, 0);
			unknown += *r.out == 0;
			changed += *r.out != early;
		}
		if (done)
			Log("audiofast: CryMovie's %d TrackView parameter ids were answered at once, the game's lookup at the first frame: "
			    "%d known, %d unknown (0, as the game), %d differed from the early answer", g_nDeferred, g_nDeferred - unknown,
			    unknown, changed);
		else
			Log("audiofast: CryMovie's %d TrackView parameter ids answered at once; the requests still not done at the first "
			    "frame, so their early answers stay", g_nDeferred);
	}
	const double ms = 1000.0 / g_freq;
	const long n = g_pending.load(), d = g_done.load();
	std::string s;
	const long long t0 = n > 0 ? g_skips[0].pushed : 0;
	for (long i = 0; i < n && i < 4; i++) {
		char b[160];
		snprintf(b, sizeof b, "%s%s (pushed +%.0f ms, done +%.0f ms)", i ? ", " : "", kSkipName[g_skips[i].site],
		         (g_skips[i].pushed - t0) * ms, g_skips[i].done ? (g_skips[i].done - t0) * ms : -1.0);
		s += b;
	}
	Log("audiofast: %ld of 4 start-up requests ran without Main waiting, %ld signalled done: %s; times from the first push",
	    n, d, n ? s.c_str() : "none came");
	const long holds = g_nHolds.load();
	std::string h;
	int shown = 0;
	for (long i = 0; i < holds && i < kMaxHolds && shown < 8; i++) {
		const HoldRec& r = g_holds[i];
		char b[200];
		snprintf(b, sizeof b, "%s%s on %s at +%.0f ms for %.0f ms%s", shown ? "; " : "", r.slot < 0 ? "the first frame" : SlotName(r.slot),
		         r.tid == g_mainTid ? "Main" : ("thread " + std::to_string(r.tid)).c_str(), (r.t0 - t0) * ms, (r.t1 - r.t0) * ms,
		         r.timedOut ? " (GAVE UP after 2 s)" : "");
		h += b;
		shown++;
	}
	Log("audiofast: %ld waits until they were done (Main %.0f ms in all)%s%s; the game's own behaviour from now on", holds,
	    g_holdTicksMain.load() * ms, holds ? ": " : "", h.c_str());
}
