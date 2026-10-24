// Outside-the-game test of src/mainreads.cpp: a window on this thread logs the reads made on it, ignores other threads,
// can be extended, and writes one block when it ends. Exit code 0 = pass.
#include "../src/mainreads.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

std::mutex g_mu;
std::vector<std::string> g_lines;
void Log(const char* fmt, ...) {
	char b[2048];
	va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
	printf("  log: %s\n", b);
	std::lock_guard<std::mutex> lk(g_mu); g_lines.push_back(b);
}

static long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
static int Fail(const char* why) { printf("FAIL: %s\n", why); return 1; }
static bool Has(const char* s, const char* s2 = "") {
	std::lock_guard<std::mutex> lk(g_mu);
	for (auto& l : g_lines) if (l.find(s) != std::string::npos && l.find(s2) != std::string::npos) return true;
	return false;
}

int main() {
	LARGE_INTEGER f; QueryPerformanceFrequency(&f);
	const long long ms = f.QuadPart / 1000;
	if (MainReads_On()) return Fail("on before any window");
	MainReads_Log(300);
	if (!MainReads_On()) return Fail("off right after MainReads_Log");
	bool otherOn = true;
	HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD { *(bool*)p = MainReads_On(); return 0; }, &otherOn, 0, nullptr);
	WaitForSingleObject(t, INFINITE); CloseHandle(t);
	if (otherOn) return Fail("on for another thread");
	const long long t0 = Now();
	MainReads_Add('x', "libs/slow.xml", 0, 0, t0, 640 * ms);
	MainReads_Add('p', "E:/game/Data/Tables.pak", 4096, 52 * 1024, t0 + 1 * ms, 3 * ms);    // inside the XML load
	MainReads_Add('p', "E:/game/Data/Levels.pak", 8192, 2 << 20, t0 + 700 * ms, 9 * ms);   // on its own
	MainReads_Add('X', "libs/ready.xml", 0, 0, t0 + 710 * ms, 0);
	Sleep(200);
	MainReads_Log(400);   // extends: ends ~600 ms after the first call
	Sleep(250);           // past the first end (300 ms)
	if (!MainReads_On()) return Fail("the extension did not hold the window open");
	if (Has("window")) return Fail("written before the extended end");
	Sleep(500);
	if (MainReads_On()) return Fail("still on after the window");
	if (!Has("ms window on thread")) return Fail("no summary line");
	if (!Has("1 XML loads 640 ms, 2 pak reads 9 ms outside them")) return Fail("totals wrong");
	if (!Has("1 ready-made")) return Fail("ready-made count");
	if (!Has("xml 640.0 ms libs/slow.xml")) return Fail("slow XML line");
	if (!Has("mainreads:     +", "pak 3.0 ms E:/game/Data/Tables.pak @4096 52 KB")) return Fail("nested pak line");
	if (!Has("pak 9.0 ms E:/game/Data/Levels.pak @8192 2048 KB")) return Fail("pak line");
	if (Has("mainreads:     +", "Levels.pak")) return Fail("a pak read outside the XML load shown as nested");
	MainReads_Add('x', "late.xml", 0, 0, Now(), ms);   // after the window: dropped
	g_lines.clear();
	MainReads_Log(100);   // a second window starts clean
	Sleep(300);
	if (!Has("0 XML loads 0 ms, 0 pak reads 0 ms")) return Fail("second window not clean");
	printf("PASS\n");
	return 0;
}
