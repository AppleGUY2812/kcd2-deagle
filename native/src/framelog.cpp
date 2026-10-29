// Frame rate in play, from the frame tick (fastload_main.cpp HkUpdate): the log had no record of a slow frame rate,
// only of single frames over 250 ms (hitch.cpp), so "it was laggy for 20 s after a fast travel" (2026-10-27) left
// nothing to read. Logged:
//   - once a minute: average fps, 1 % low, worst frame, frames over 50 ms, and our own tick's cost per second;
//   - each second that is slow (under 45 fps, or a frame over 100 ms), up to 60 lines in a row, with the level state.
// Seconds with the game window in the background are left out (the game sleeps then). fastload_framelog.txt 0 = off.
#include "framelog.h"
#include "log.h"
#include <windows.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <atomic>
#include <thread>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace {
bool g_on = true;
double g_freq = 1;
long long g_last = 0;          // the previous frame's start
long long g_secStart = 0, g_minStart = 0;
std::vector<float> g_secFrames, g_minFrames;
double g_secTick = 0, g_minTick = 0;   // ms in our tick
int g_badRun = 0;
bool g_secBg = false;          // a background moment in this second: the second is skipped
int g_minSkipped = 0;

// What limits the frame rate (2026-10-28, "is there a way we can optimize kcd to get more fps"): the graphics card's
// busy % and video memory (NVML, sampled once a second on our own thread) and the CPU time of the main and render
// threads over each minute. GPU near 100 % = the card limits; a thread near 100 % of one core = that thread limits.
using NvmlInit = int (*)();
using NvmlByIndex = int (*)(unsigned, void**);
struct NvmlUtil { unsigned gpu, memory; };
struct NvmlMem { unsigned long long total, free, used; };
using NvmlGetUtil = int (*)(void*, NvmlUtil*);
using NvmlGetMem = int (*)(void*, NvmlMem*);
NvmlGetUtil g_nvUtil = nullptr;
NvmlGetMem g_nvMem = nullptr;
void* g_nvDev = nullptr;
std::atomic<unsigned> g_gpuSum{0}, g_gpuN{0}, g_gpuMax{0};
std::atomic<unsigned long long> g_vramUsed{0}, g_vramTotal{0};
HANDLE g_mainThread = nullptr, g_renderThread = nullptr;
unsigned long long g_mainCpu0 = 0, g_renderCpu0 = 0;

unsigned long long CpuTime(HANDLE t) {   // 100 ns units, user + kernel
	FILETIME c, e, k, u;
	if (!t || !GetThreadTimes(t, &c, &e, &k, &u)) return 0;
	return (((unsigned long long)k.dwHighDateTime << 32) | k.dwLowDateTime) + (((unsigned long long)u.dwHighDateTime << 32) | u.dwLowDateTime);
}

HANDLE FindThread(const wchar_t* name) {
	using GetDesc = HRESULT (WINAPI*)(HANDLE, PWSTR*);
	static GetDesc getDesc = (GetDesc)GetProcAddress(GetModuleHandleA("kernel32.dll"), "GetThreadDescription");
	if (!getDesc) return nullptr;
	HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
	if (snap == INVALID_HANDLE_VALUE) return nullptr;
	THREADENTRY32 te{ sizeof te };
	HANDLE found = nullptr;
	for (BOOL ok = Thread32First(snap, &te); ok && !found; ok = Thread32Next(snap, &te)) {
		if (te.th32OwnerProcessID != GetCurrentProcessId()) continue;
		HANDLE t = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, te.th32ThreadID);
		if (!t) continue;
		PWSTR d = nullptr;
		if (SUCCEEDED(getDesc(t, &d)) && d) { if (!wcscmp(d, name)) found = t; LocalFree(d); }
		if (found != t) CloseHandle(t);
	}
	CloseHandle(snap);
	return found;
}

void GpuSampler() {
	HMODULE nv = LoadLibraryA("nvml.dll");
	auto init = nv ? (NvmlInit)GetProcAddress(nv, "nvmlInit_v2") : nullptr;
	auto byIndex = nv ? (NvmlByIndex)GetProcAddress(nv, "nvmlDeviceGetHandleByIndex_v2") : nullptr;
	g_nvUtil = nv ? (NvmlGetUtil)GetProcAddress(nv, "nvmlDeviceGetUtilizationRates") : nullptr;
	g_nvMem = nv ? (NvmlGetMem)GetProcAddress(nv, "nvmlDeviceGetMemoryInfo") : nullptr;
	if (!init || !byIndex || !g_nvUtil || !g_nvMem || init() != 0 || byIndex(0, &g_nvDev) != 0) {
		Log("framelog: no NVIDIA GPU counters (nvml.dll %s): the minute line leaves the GPU out", nv ? "found, not usable" : "not found");
		return;
	}
	for (;;) {
		Sleep(1000);
		NvmlUtil u{};
		if (g_nvUtil(g_nvDev, &u) == 0) {
			g_gpuSum += u.gpu; g_gpuN++;
			unsigned m = g_gpuMax.load();
			while (u.gpu > m && !g_gpuMax.compare_exchange_weak(m, u.gpu)) {}
		}
		NvmlMem mem{};
		if (g_nvMem(g_nvDev, &mem) == 0) { g_vramUsed = mem.used; g_vramTotal = mem.total; }
	}
}

double Ms(long long a, long long b) { return (b - a) * 1000.0 / g_freq; }

bool Foreground() {
	DWORD pid = 0;
	GetWindowThreadProcessId(GetForegroundWindow(), &pid);
	return pid == GetCurrentProcessId();
}

void EndSecond(long long now, int levelState) {
	const size_t n = g_secFrames.size();
	const double span = Ms(g_secStart, now);
	if (n && !g_secBg) {
		const float worst = *std::max_element(g_secFrames.begin(), g_secFrames.end());
		const double fps = n * 1000.0 / span;
		g_minFrames.insert(g_minFrames.end(), g_secFrames.begin(), g_secFrames.end());
		if (fps < 45 || worst > 100) {
			if (++g_badRun <= 60) {
				int over50 = 0;
				for (float f : g_secFrames) over50 += f > 50;
				Log("framelog: slow second: %.0f fps, worst frame %.0f ms, %d over 50 ms, our tick %.2f ms (level state %d)",
				    fps, worst, over50, g_secTick, levelState);
			}
		} else g_badRun = 0;
	} else if (n) g_minSkipped++;
	g_secFrames.clear();
	g_secTick = 0;
	g_secBg = false;
	g_secStart = now;
}

void EndMinute(long long now) {
	if (!g_minFrames.empty()) {
		std::vector<float> f = g_minFrames;
		const size_t n = f.size();
		double sum = 0;
		int over50 = 0;
		for (float x : f) { sum += x; over50 += x > 50; }
		std::sort(f.begin(), f.end());
		const float p99 = f[(std::min)(n - 1, (size_t)(n * 0.99))];
		// memory a minute too: a leak shows as private bytes climbing while the fps falls over a session
		PROCESS_MEMORY_COUNTERS_EX pm{};
		GetProcessMemoryInfo(GetCurrentProcess(), (PROCESS_MEMORY_COUNTERS*)&pm, sizeof pm);
		// busy over the whole minute (background seconds included), as % of one core / of the GPU
		const double wall = Ms(g_minStart, now) * 10000.0;   // 100 ns units
		if (!g_renderThread) g_renderThread = FindThread(L"RenderThread");
		const unsigned long long mc = CpuTime(g_mainThread), rc = CpuTime(g_renderThread);
		const double mainPct = g_mainCpu0 && mc ? (mc - g_mainCpu0) * 100.0 / wall : -1;
		const double rendPct = g_renderCpu0 && rc ? (rc - g_renderCpu0) * 100.0 / wall : -1;
		g_mainCpu0 = mc; g_renderCpu0 = rc;
		const unsigned gn = g_gpuN.exchange(0), gs = g_gpuSum.exchange(0), gm = g_gpuMax.exchange(0);
		char gpu[160] = "";
		if (gn) snprintf(gpu, sizeof gpu, "; GPU busy %u %% (max %u %%), video memory %.0f of %.0f MB", gs / gn, gm,
		                 g_vramUsed.load() / 1048576.0, g_vramTotal.load() / 1048576.0);
		char cpu[96] = "";
		if (mainPct >= 0 && rendPct >= 0) snprintf(cpu, sizeof cpu, "; main thread %.0f %% of a core, render thread %.0f %%", mainPct, rendPct);
		else if (mainPct >= 0) snprintf(cpu, sizeof cpu, "; main thread %.0f %% of a core", mainPct);
		Log("framelog: last %.0f s in play: %.0f fps average, 1%% low %.0f fps (99th-percentile frame %.1f ms), worst frame %.0f ms, "
		    "%d frames over 50 ms; our tick %.2f ms a second; memory %.0f MB private, %.0f MB in RAM%s%s%s",
		    sum / 1000.0, n * 1000.0 / sum, 1000.0 / p99, p99, f.back(), over50, g_minTick * 1000.0 / (std::max)(sum, 1.0),
		    pm.PrivateUsage / 1048576.0, pm.WorkingSetSize / 1048576.0, gpu, cpu,
		    g_minSkipped ? " (seconds in the background left out)" : "");
	} else {   // a minute with no play frames: restart the CPU counters so the next minute is clean
		g_mainCpu0 = CpuTime(g_mainThread); g_renderCpu0 = CpuTime(g_renderThread);
		g_gpuN = 0; g_gpuSum = 0; g_gpuMax = 0;
	}
	g_minFrames.clear();
	g_minTick = 0;
	g_minSkipped = 0;
	g_minStart = now;
}
}   // namespace

void Framelog_Install(const char* dir) {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = (double)f.QuadPart;
	const std::string file = std::string(dir) + "fastload_framelog.txt";
	if (FILE* fp = fopen(file.c_str(), "r")) { int v = 1; if (fscanf(fp, "%d", &v) == 1) g_on = v != 0; fclose(fp); }
	g_secFrames.reserve(512);
	g_minFrames.reserve(16384);
	if (g_on) std::thread(GpuSampler).detach();
	Log("framelog: %s (fps a minute in play + each slow second; fastload_framelog.txt 0 = off)", g_on ? "on" : "off");
}

long long Framelog_TickStart() {
	if (!g_on) return 0;
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	return t.QuadPart;
}

void Framelog_TickEnd(long long t0, int levelState) {
	if (!g_on || !t0) return;
	LARGE_INTEGER t; QueryPerformanceCounter(&t);
	const long long now = t.QuadPart;
	const double tick = Ms(t0, now);
	if (levelState != 0) { g_last = 0; g_secStart = g_minStart = 0; g_secFrames.clear(); g_minFrames.clear(); return; }   // loads: not play
	if (!g_mainThread) {
		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &g_mainThread, THREAD_QUERY_LIMITED_INFORMATION, FALSE, 0);
		g_mainCpu0 = CpuTime(g_mainThread);
	}
	if (!g_last) { g_last = t0; g_secStart = g_minStart = t0; g_mainCpu0 = CpuTime(g_mainThread); g_renderCpu0 = CpuTime(g_renderThread); return; }
	g_secFrames.push_back((float)Ms(g_last, t0));
	g_last = t0;
	g_secTick += tick;
	g_minTick += tick;
	if (!(g_secFrames.size() & 15) && !Foreground()) g_secBg = true;   // checked every 16 frames
	if (Ms(g_secStart, now) >= 1000) {
		if (!Foreground()) g_secBg = true;
		EndSecond(now, levelState);
	}
	if (Ms(g_minStart, now) >= 60000) EndMinute(now);
}
