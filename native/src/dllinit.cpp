// WHGame.dll's static initializers outside the loader lock: see dllinit.h. WHGame.dll / KingdomCome.exe 1.5.6.
#include "dllinit.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

// WHGame's entry point (_DllMainCRTStartup): if (reason == 1) __security_init_cookie(); then the C runtime's dispatch.
constexpr uint32_t kRvaEntry = 0x1c1fe0c;
const unsigned char kEntryBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x20,0x49,
                                      0x8b,0xf8,0x8b,0xda,0x48,0x8b,0xf1,0x83,0xfa,0x01,0x75,0x05,0xe8 };
// CreateGameStartup, the export the exe calls right after GetProcAddress (KingdomCome.exe+353b, no arguments)
constexpr uint32_t kRvaCreate = 0x19db050;
const unsigned char kCreateBytes[] = { 0x48,0x8d,0x05,0xf9,0xd6,0x33,0x02,0x48,0x89,0x05,0x6a,0x97,0xc8,0x03 };

using EntryFn = BOOL(WINAPI*)(HINSTANCE, DWORD, LPVOID);
using CreateFn = void* (*)();
EntryFn g_origEntry = nullptr;
CreateFn g_origCreate = nullptr;
uintptr_t g_base = 0;
DWORD g_mainTid = 0;
HINSTANCE g_inst = nullptr;
LPVOID g_reserved = nullptr;
std::atomic<int> g_state{ 0 };   // 0 not held, 1 WHGame's process attach held, 2 running on Main, 3 ran
std::atomic<long> g_dropped{ 0 };
HANDLE g_done = nullptr;
BOOL g_ok = TRUE;
long long g_freq = 1, g_tInstall = 0, g_tHeld = 0, g_t0 = 0, g_t1 = 0;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

// Main, at the exe's call of CreateGameStartup: the process attach the loader would have run inside LoadLibraryA
void RunStatic() {
	int held = 1;
	if (!g_state.compare_exchange_strong(held, 2)) return;
	g_t0 = Now();
	g_ok = g_origEntry(g_inst, DLL_PROCESS_ATTACH, g_reserved);
	g_t1 = Now();
	g_state = 3;
	SetEvent(g_done);
}

// WHGame's first code to run after its load: its static initializers go first (another thread waits for them)
void* HkCreate() {
	const int s = g_state.load();
	if (s == 1 || s == 2) {
		RunStatic();
		WaitForSingleObject(g_done, INFINITE);
		if (!g_ok) return nullptr;   // the loader would have failed LoadLibraryA: the exe's "no game" path
	}
	return g_origCreate();
}

BOOL WINAPI HkEntry(HINSTANCE h, DWORD reason, LPVOID r) {
	if (reason == DLL_PROCESS_ATTACH) {
		int z = 0;
		if (GetCurrentThreadId() == g_mainTid && g_state.compare_exchange_strong(z, 1)) {
			g_inst = h; g_reserved = r; g_tHeld = Now();
			return TRUE;
		}
		return g_origEntry(h, reason, r);
	}
	const int s = g_state.load(std::memory_order_acquire);
	if (s == 1 || s == 2) { g_dropped++; return TRUE; }   // thread notifications before its C runtime started
	return g_origEntry(h, reason, r);
}

const char* g_why = nullptr;   // why it is off (logged by Dllinit_WaitStatic, which runs once logging is set up)
int g_mode = 0;

}  // namespace

bool Dllinit_EarlyInstall(uintptr_t base, void* self) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
	g_tInstall = Now();
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	std::string dir(path);
	dir = dir.substr(0, dir.find_last_of("\\/") + 1);
	if (FILE* fp = fopen((dir + "fastload_dllinit.txt").c_str(), "r")) {
		if (fscanf(fp, "%d", &g_mode) != 1) g_mode = 0;
		fclose(fp);
	}
	if (g_mode != 1) { g_why = "off (fastload_dllinit.txt 1: WHGame's static initializers outside the loader lock)"; return false; }
	const auto* nt = (const IMAGE_NT_HEADERS*)(base + ((const IMAGE_DOS_HEADER*)base)->e_lfanew);
	if (nt->OptionalHeader.AddressOfEntryPoint != kRvaEntry || memcmp((void*)(base + kRvaEntry), kEntryBytes, sizeof kEntryBytes)) {
		g_why = "WHGame's entry point differs (another game version?) - off";
		return false;
	}
	void* create = (void*)(base + kRvaCreate);
	if ((void*)GetProcAddress((HMODULE)base, "CreateGameStartup") != create || memcmp(create, kCreateBytes, sizeof kCreateBytes)) {
		g_why = "WHGame's CreateGameStartup differs (another game version?) - off";
		return false;
	}
	g_base = base;
	g_mainTid = GetCurrentThreadId();
	g_done = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	void* entry = (void*)(base + kRvaEntry);
	if (!g_done || MH_CreateHook(create, (void*)&HkCreate, (void**)&g_origCreate) != MH_OK) { g_why = "hook failed - off"; return false; }
	if (MH_CreateHook(entry, (void*)&HkEntry, (void**)&g_origEntry) != MH_OK) { MH_RemoveHook(create); g_why = "hook failed - off"; return false; }
	// CreateGameStartup's hook first: the entry point's alone would leave the static initializers never run
	if (MH_EnableHook(create) != MH_OK) { MH_RemoveHook(entry); MH_RemoveHook(create); g_why = "hook failed - off"; return false; }
	if (MH_EnableHook(entry) != MH_OK) {
		MH_DisableHook(create); MH_RemoveHook(entry); MH_RemoveHook(create);
		g_why = "hook failed - off";
		return false;
	}
	return true;
}

void Dllinit_WaitStatic() {
	if (g_why) { Log("dllinit: %s", g_why); return; }
	const int s = g_state.load();
	if (s == 0) { Log("dllinit: on, but WHGame's entry point never came for the process attach (already run?)"); return; }
	const long long w0 = Now();
	const DWORD r = WaitForSingleObject(g_done, 60000);
	const long long w1 = Now();
	if (r != WAIT_OBJECT_0) {
		Log("dllinit: WHGame's static initializers still not run after 60 s (the exe never called CreateGameStartup?)");
		return;
	}
	Log("dllinit: WHGame's static initializers ran on Main outside the loader lock: %.0f ms, from %.0f ms after the plugin's "
	    "DllMain (its entry point held %.0f ms before); result %d; %ld thread notifications to WHGame dropped before; this "
	    "thread waited %.0f ms for them", Ms(g_t0, g_t1), Ms(g_tInstall, g_t0), Ms(g_tHeld, g_t0), (int)g_ok, g_dropped.load(),
	    Ms(w0, w1));
}
