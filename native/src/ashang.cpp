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
	InterlockedIncrement(&g_frames);
}

void AsHang_Install(uintptr_t base) {
	void* at = (void*)(base + kRvaCall);
	if (memcmp(at, kCallBytes, sizeof kCallBytes) != 0) { Log("ashang: the AS2 call at +0x%x isn't the 1.5.6 code: off", (unsigned)kRvaCall); return; }
	if (MH_CreateHook(at, (void*)&HkCall, (void**)&g_orig) != MH_OK || MH_EnableHook(at) != MH_OK) { Log("ashang: hook failed: off"); return; }
	CreateThread(nullptr, 0, Watch, nullptr, 0, nullptr);
	Log("ashang: on (a stuck Flash script is named in this log after 6 s without a frame)");
}
