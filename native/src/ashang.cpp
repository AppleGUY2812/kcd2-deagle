// ashang: which Flash (AS2) script function is running when the game's main thread stops making frames (inventory.asi).
//
// 2026-10-27: the item-transfer screen froze the game after a multi-item move. The main thread spun in Scaleform's AS2
// interpreter three script calls deep, inside a movie's Advance; no script loop in our movies or the game's menu scripts
// looks endless. This finds the function the next time.
//
// WHGame 1.5.6 FUN_181c7bf60 is the AS2 function call (it counts nesting at +0x184, limit 255) and runs the body with
// FUN_181c83280(fn+0x78 action buffer, env, fn+0x98 start, fn+0x9c length, ...). The hook keeps a stack of the called
// function objects (main thread only, two stores per call). A watchdog thread logs that stack once the frame tick has
// not run for 6 s: for each function its start / length and the bytes before the start, which hold the DefineFunction2
// record with the function's name, so the log names the looping function directly.
//
// 2026-10-29, slow frames: every map / inventory open spent ~300 ms in Flash script in the key-press frame, before the
// fade (fastload's menutime starts at the fader, so it never saw it). A sampler thread now reads that same stack every
// 2 ms once a frame is 60 ms old, and when a frame of 150 ms or more with Flash script in it ends, logs the functions
// it found: share of samples with the function anywhere on the stack / innermost, name, start, length.
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "ashang.h"

namespace {
constexpr uintptr_t kRvaCall = 0x1c7bf60;
const unsigned char kCallBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57 };
using CallFn = void(__fastcall*)(void* fn, void* call, void* a3, void* a4);
CallFn g_orig = nullptr;
constexpr int kMax = 64;
void* volatile g_stack[kMax];
volatile int g_depth = 0;
volatile DWORD g_tid = 0;
volatile LONG g_frames = 0;
volatile LONGLONG g_frameT = 0;   // QPC at the last frame tick

void __fastcall HkCall(void* fn, void* call, void* a3, void* a4) {
	if (GetCurrentThreadId() != g_tid) { g_orig(fn, call, a3, a4); return; }
	const int d = g_depth;
	if (d < kMax) g_stack[d] = fn;
	g_depth = d + 1;
	g_orig(fn, call, a3, a4);
	g_depth = d;
}

// printable text of n bytes at p ('.' for the rest), or false if unreadable
bool Text(const unsigned char* p, int n, char* out) {
	__try {
		for (int i = 0; i < n; i++) out[i] = (p[i] >= 32 && p[i] < 127) ? (char)p[i] : '.';
		out[n] = 0;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Read(const void* p, void* out, size_t n) {
	__try { memcpy(out, p, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

void Report(int depth) {
	Log("ashang: the main thread made no frame for 6 s, %d AS2 calls deep (innermost last):", depth);
	const int n = depth < kMax ? depth : kMax;
	for (int i = (n > 8 ? n - 8 : 0); i < n; i++) {
		const unsigned char* fn = (const unsigned char*)g_stack[i];
		uintptr_t buf = 0;
		int start = 0, len = 0;
		if (!fn || !Read(fn + 0x78, &buf, 8) || !Read(fn + 0x98, &start, 4) || !Read(fn + 0x9c, &len, 4)) {
			Log("ashang:  #%d fn %p unreadable", i, (void*)fn);
			continue;
		}
		Log("ashang:  #%d fn %p buffer %p start %d length %d", i, (void*)fn, (void*)buf, start, len);
		// the buffer object's pointers: the one that holds the code shows the DefineFunction2 header (its name) before start
		uintptr_t q[8] = {};
		if (!buf || !Read((void*)buf, q, sizeof q)) continue;
		for (int k = 0; k < 8; k++) {
			if (q[k] < 0x10000 || start < 0) continue;
			char t[200];
			const int back = start < 128 ? start : 128;
			if (Text((const unsigned char*)q[k] + start - back, back + 48, t))
				Log("ashang:    buffer+0x%x -> %p: \"%s\"", k * 8, (void*)q[k], t);
		}
	}
}

// The function's name from the DefineFunction2 (0x8e) / DefineFunction (0x9b) record that ends right at its start;
// an unnamed one (a method assigned with "x = function") gets the printable bytes before the record instead.
void FnName(const unsigned char* fn, char* out, size_t cap, int* startOut, int* lenOut) {
	snprintf(out, cap, "?");
	uintptr_t buf = 0;
	int start = 0, len = 0;
	if (!fn || !Read(fn + 0x78, &buf, 8) || !Read(fn + 0x98, &start, 4) || !Read(fn + 0x9c, &len, 4)) return;
	*startOut = start; *lenOut = len;
	uintptr_t q[8] = {};
	if (!buf || start < 4 || !Read((void*)buf, q, sizeof q)) return;
	const int back = start < 300 ? start : 300;
	unsigned char w[300];
	for (int k = 0; k < 8; k++) {
		if (q[k] < 0x10000 || !Read((const unsigned char*)q[k] + start - back, w, back)) continue;
		for (int j = back - 3; j >= 0; j--) {
			if ((w[j] != 0x8e && w[j] != 0x9b) || (int)(w[j + 1] | w[j + 2] << 8) != back - j - 3) continue;
			const char* nm = (const char*)w + j + 3;
			const int room = back - j - 3;
			int n = 0;
			while (n < room && nm[n]) n++;
			if (n > 0 && n < room) { snprintf(out, cap, "%.*s", n > 60 ? 60 : n, nm); return; }
			char t[48];
			int m = 0;
			for (int i = (j > 40 ? j - 40 : 0); i < j && m < 46; i++) t[m++] = (w[i] >= 32 && w[i] < 127) ? (char)w[i] : '.';
			t[m] = 0;
			snprintf(out, cap, "(unnamed, after \"%s\")", t);
			return;
		}
	}
}

struct Prof { const void* fn; int any, self, start, len; char name[96]; };
constexpr int kProf = 128;
Prof g_prof[kProf];

DWORD WINAPI Sampler(LPVOID) {
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
	const double toMs = 1000.0 / fq.QuadPart;
	HANDLE tm = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
	if (!tm) tm = CreateWaitableTimerW(nullptr, TRUE, nullptr);
	int nProf = 0, samples = 0, inAs = 0, maxDepth = 0;
	LONG curF = 0;
	LONGLONG t0 = 0;
	for (;;) {
		LARGE_INTEGER due; due.QuadPart = -20000;   // 2 ms
		if (tm && SetWaitableTimer(tm, &due, 0, nullptr, nullptr, FALSE)) WaitForSingleObject(tm, 50); else Sleep(2);
		LARGE_INTEGER now; QueryPerformanceCounter(&now);
		const LONG f = g_frames;
		if (samples && f != curF) {   // the sampled frame ended
			const double ms = (now.QuadPart - t0) * toMs;
			if (ms >= 150 && inAs >= 3) {
				Log("ashang: slow frame %.0f ms: %d samples every 2 ms from 60 ms in, %d in Flash script (up to %d calls deep); "
				    "functions (on the stack %% / innermost %%):", ms, samples, inAs, maxDepth);
				for (int shown = 0; shown < 14; shown++) {
					int best = -1;
					for (int i = 0; i < nProf; i++)
						if (g_prof[i].any > 0 && (best < 0 || g_prof[i].any > g_prof[best].any)) best = i;
					if (best < 0 || g_prof[best].any * 20 < samples) break;   // under 5 %
					Prof& p = g_prof[best];
					Log("ashang:   %3d%% / %3d%%  %s  (start %d, length %d)", p.any * 100 / samples, p.self * 100 / samples,
					    p.name, p.start, p.len);
					p.any = -1;
				}
			}
			nProf = samples = inAs = maxDepth = 0;
		}
		const LONGLONG ft = g_frameT;
		if (!ft || (now.QuadPart - ft) * toMs < 60) continue;
		if (!samples) { curF = f; t0 = ft; }
		if (f != curF) continue;
		samples++;
		int d = g_depth;
		if (d <= 0) continue;
		inAs++;
		if (d > maxDepth) maxDepth = d;
		if (d > kMax) d = kMax;
		const void* st[kMax];
		for (int i = 0; i < d; i++) st[i] = g_stack[i];
		for (int i = 0; i < d; i++) {
			bool dup = false;
			for (int j = 0; j < i && !dup; j++) dup = st[j] == st[i];
			int at = -1;
			for (int k = 0; k < nProf; k++) if (g_prof[k].fn == st[i]) { at = k; break; }
			if (at < 0) {
				if (nProf >= kProf) continue;
				at = nProf++;
				Prof& p = g_prof[at];
				p.fn = st[i]; p.any = p.self = p.start = p.len = 0;
				FnName((const unsigned char*)st[i], p.name, sizeof p.name, &p.start, &p.len);
			}
			if (!dup) g_prof[at].any++;
			if (i == d - 1) g_prof[at].self++;
		}
	}
}

DWORD WINAPI Watch(LPVOID) {
	LONG last = -1;
	int still = 0;
	bool told = false;
	for (;;) {
		Sleep(1000);
		const LONG f = g_frames;
		if (f != last) { last = f; still = 0; if (told) Log("ashang: frames again"); told = false; continue; }
		if (++still >= 6 && !told && f > 0) {
			told = true;
			const int d = g_depth;
			if (d > 0) Report(d);
			else Log("ashang: the main thread made no frame for 6 s, outside AS2 (a load or the game's own code)");
		}
	}
}
}  // namespace

void AsHang_Frame() {
	if (!g_tid) g_tid = GetCurrentThreadId();
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	g_frameT = t.QuadPart;
	InterlockedIncrement(&g_frames);
}

// outside-the-game test (native/test/ashang_test.cpp): run body as if the Flash function object fn were being called
void AsHang_TestStart() {
	g_tid = GetCurrentThreadId();
	CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr);
}
void AsHang_TestCall(void* fn, void (*body)()) {
	const int d = g_depth;
	if (d < kMax) g_stack[d] = fn;
	g_depth = d + 1;
	body();
	g_depth = d;
}

void AsHang_Install(uintptr_t base) {
	void* at = (void*)(base + kRvaCall);
	if (memcmp(at, kCallBytes, sizeof kCallBytes) != 0) { Log("ashang: the AS2 call at +0x%x isn't the 1.5.6 code: off", (unsigned)kRvaCall); return; }
	if (MH_CreateHook(at, (void*)&HkCall, (void**)&g_orig) != MH_OK || MH_EnableHook(at) != MH_OK) { Log("ashang: hook failed: off"); return; }
	CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
	CreateThread(nullptr, 0, Sampler, nullptr, 0, nullptr);
	Log("ashang: on (a stuck Flash script is named in this log after 6 s without a frame; frames of 150 ms+ with "
	    "Flash script in them list their functions)");
}
