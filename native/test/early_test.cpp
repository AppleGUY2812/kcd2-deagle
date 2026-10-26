// Outside-the-game test of early.cpp's mode 3 (the default): the samplers run on threads made with
// THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH, find threads by name, sample them with a memory probe beside, and report at
// "Init" and at the "first frame". b529e1f killed the game 0.3 s after launch in every boot of 157 (a function-local
// static on such a thread). The child run ("early_test tls") shows that mechanism on its own. Also: Main's samples are
// unwound to the thread's start (exact frames, 2026-10-25 late). Prints PASS / FAIL, exit 0/1.
#include "../src/early.h"
#include "../src/log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

using NtCreateThreadExFn = LONG(NTAPI*)(PHANDLE, ACCESS_MASK, PVOID, HANDLE, PVOID, PVOID, ULONG, SIZE_T, SIZE_T, SIZE_T, PVOID);

__declspec(noinline) static DWORD Guarded() {
	static const DWORD v = GetTickCount() | 1;   // dynamic init: MSVC's thread-safe guard reads a TLS variable
	return v;
}
static DWORD WINAPI TlsThread(LPVOID) { return Guarded(); }

// a SKIP_THREAD_ATTACH thread that reaches a function-local static
static int TlsChild() {
	const auto create = (NtCreateThreadExFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtCreateThreadEx");
	HANDLE t = nullptr;
	if (!create || create(&t, THREAD_ALL_ACCESS, nullptr, GetCurrentProcess(), (PVOID)&TlsThread, nullptr, 0x2, 0, 0, 0, nullptr) < 0)
		return 2;
	WaitForSingleObject(t, 5000);
	return 0;   // reached only if the thread did not take the process down
}

static volatile LONG g_quit = 0;
static DWORD WINAPI Worker(LPVOID p) {
	const wchar_t* name = (const wchar_t*)p;
	SetThreadDescription(GetCurrentThread(), name);
	const bool busy = wcsstr(name, L"gpuwarm") || wcsstr(name, L"Render");
	volatile double x = 1;
	while (!g_quit) {
		const DWORD t0 = GetTickCount();
		if (busy) while (GetTickCount() - t0 < 30) x = x * 1.0000001 + 1e-9;
		Sleep(busy ? 20 : 40);
	}
	return 0;
}

static void MainWork(int ms, bool busy) {
	const DWORD t0 = GetTickCount();
	volatile double x = 1;
	if (busy) while (GetTickCount() - t0 < (DWORD)ms) x = x * 1.0000001 + 1e-9;
	else Sleep(ms);
}

static std::string ReadAll(const std::string& p) {
	std::string s;
	if (FILE* f = fopen(p.c_str(), "rb")) {
		char b[65536];
		size_t n;
		while ((n = fread(b, 1, sizeof b, f)) > 0) s.append(b, n);
		fclose(f);
	}
	return s;
}

static int Lines(const std::string& s) {
	int k = 0;
	for (char c : s) k += c == '\n';
	return k;
}

int main(int argc, char** argv) {
	if (argc > 1 && !strcmp(argv[1], "tls")) return TlsChild();
	char exe[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	std::string dir = exe;
	dir = dir.substr(0, dir.find_last_of('\\') + 1);

	// 1. the mechanism alone, in a child process
	STARTUPINFOA si{};
	si.cb = sizeof si;
	PROCESS_INFORMATION pi{};
	std::string cmd = std::string("\"") + exe + "\" tls";
	DWORD childExit = 0xffffffff;
	if (CreateProcessA(nullptr, &cmd[0], nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) {
		WaitForSingleObject(pi.hProcess, 10000);
		GetExitCodeProcess(pi.hProcess, &childExit);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	printf("a function-local static on a SKIP_THREAD_ATTACH thread: the child exited 0x%08lx (%s)\n", childExit,
	       childExit == 0 ? "no crash" : childExit == 0xC0000005 ? "access violation: the 157 crash" : "other");

	// 2. mode 3 as in the game
	DeleteFileA((dir + "fastload_early.txt").c_str());   // the default mode
	DeleteFileA((dir + "fastload_early.csv").c_str());
	DeleteFileA((dir + "fastload_early_probe.csv").c_str());
	HMODULE self = GetModuleHandleA(nullptr);
	LogInit(self);   // early_test.log next to the exe
	Early_Start(self);
	const wchar_t* names[] = { L"RenderThread", L"AudioThread(NoCfg)", L"JobWorker_fastload gpuwarm", L"some other thread" };
	HANDLE w[4];
	for (int i = 0; i < 4; i++) w[i] = CreateThread(nullptr, 0, &Worker, (void*)names[i], 0, nullptr);
	MainWork(300, true);
	MainWork(300, false);
	const auto* dos = (const IMAGE_DOS_HEADER*)self;
	const auto* nt = (const IMAGE_NT_HEADERS*)((const char*)self + dos->e_lfanew);
	const size_t size = nt->OptionalHeader.SizeOfImage;
	Early_Report((uintptr_t)self, size);
	for (int i = 0; i < 4; i++) {
		MainWork(250, true);
		MainWork(250, false);
	}
	Early_Frame((uintptr_t)self, size);
	Sleep(3000);   // the report thread
	g_quit = 1;
	WaitForMultipleObjects(4, w, TRUE, 2000);

	const std::string log = ReadAll(dir + "early_test.log");
	const std::string csv = ReadAll(dir + "fastload_early.csv");
	const std::string probe = ReadAll(dir + "fastload_early_probe.csv");
	// Main's rows: unwound (last column 1), and an unwound stack ends at the thread's start (ntdll's RtlUserThreadStart)
	int mainRows = 0, mainUnw = 0, rooted = 0;
	for (size_t at = 0; at < csv.size();) {
		size_t e = csv.find('\n', at);
		if (e == std::string::npos) e = csv.size();
		std::string line = csv.substr(at, e - at);
		at = e + 1;
		if (!line.empty() && line.back() == '\r') line.pop_back();   // read in binary: the text-mode file's \r\n
		if (line.find(",Main,") == std::string::npos) continue;
		mainRows++;
		if (line.size() < 2 || line.compare(line.size() - 2, 2, ",1")) continue;
		mainUnw++;
		const size_t f0 = line.find(',', line.find(',', line.find(',') + 1) + 1) + 1;   // the rip + frames field
		const std::string frames = line.substr(f0, line.find(',', f0) - f0);
		const std::string last = frames.substr(frames.find_last_of(' ') + 1);
		if (!_strnicmp(last.c_str(), "ntdll.dll+", 10)) rooted++;
	}
	printf("Main rows %d, unwound %d, reaching the thread start %d\n", mainRows, mainUnw, rooted);
	struct Check { const char* what; bool ok; } checks[] = {
		{ "Init line", log.find("early: DllMain") != std::string::npos },
		{ "first-frame line", log.find("early: Main from DllMain to the first frame") != std::string::npos },
		{ "RenderThread found", log.find("RenderThread from") != std::string::npos },
		{ "AudioThread found", log.find("AudioThread from") != std::string::npos },
		{ "gpuwarm found", log.find("gpuwarm from") != std::string::npos },
		{ "RenderThread per-second line", log.find("early: RenderThread ") != std::string::npos },
		{ "Main waited lines", log.find("Main waited in") != std::string::npos },
		{ "memory probe line", log.find("early: memory probe") != std::string::npos },
		{ "csv RenderThread rows", csv.find(",RenderThread,") != std::string::npos },
		{ "csv gpuwarm rows", csv.find(",gpuwarm,") != std::string::npos },
		{ "probe csv rows > 100", Lines(probe) > 100 },
		{ "Main samples unwound (80%+)", mainRows > 0 && mainUnw * 10 >= mainRows * 8 },
		{ "unwound Main stacks reach the thread start (80%+)", mainUnw > 0 && rooted * 10 >= mainUnw * 8 },
	};
	bool all = true;
	for (const Check& c : checks) {
		printf("%s %s\n", c.ok ? "ok  " : "FAIL", c.what);
		all &= c.ok;
	}
	printf("csv %d rows, probe %d rows\n", Lines(csv), Lines(probe));
	printf("%s\n", all ? "PASS" : "FAIL");
	return all ? 0 : 1;
}
