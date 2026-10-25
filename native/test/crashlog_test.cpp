// Outside-the-game test of crashlog.cpp: faults caught by __except are written to fastload_crash.txt next to the exe,
// each faulting address once; the record names the fault kind, the address and the stack. Prints PASS / FAIL, exit 0/1.
#include "../src/crashlog.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>

static volatile int* g_bad = (volatile int*)0x10;

__declspec(noinline) static int WriteFault() {
	__try { *g_bad = 1; return 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}
__declspec(noinline) static int ReadFault() {
	__try { return *(volatile int*)((char*)g_bad + 0x20) + 2; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}
__declspec(noinline) static int DivFault(volatile int z) {
	__try { return 10 / z; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}
static int CppCode() {   // a C++-exception code: not crash-type, no record
	__try { RaiseException(0xE06D7363, 0, 0, nullptr); return 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 1; }
}

static int Count(const std::string& s, const char* what) {
	int k = 0;
	for (size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + 1)) k++;
	return k;
}

int main() {
	char path[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, path, MAX_PATH);
	std::string file = path;
	file = file.substr(0, file.find_last_of('\\') + 1) + "fastload_crash.txt";
	DeleteFileA(file.c_str());
	Crashlog_Install(GetModuleHandleA(nullptr));
	int caught = 0;
	for (int i = 0; i < 5; i++) caught += WriteFault();   // one address, five faults: one record
	caught += ReadFault();
	caught += DivFault(0);
	caught += CppCode();
	std::string s;
	if (FILE* f = fopen(file.c_str(), "rb")) {
		char buf[4096];
		size_t n;
		while ((n = fread(buf, 1, sizeof buf, f)) > 0) s.append(buf, n);
		fclose(f);
	}
	printf("%s", s.c_str());
	const int av = Count(s, "exception c0000005"), dz = Count(s, "exception c0000094"), cpp = Count(s, "e06d7363");
	const bool ok = caught == 8 && Count(s, "--- start") == 1 && av == 2 && dz == 1 && cpp == 0 &&
	                Count(s, "(write to 0000000000000010)") == 1 && Count(s, "(read of 0000000000000030)") == 1 &&
	                Count(s, "  stack: ") == 3 && Count(s, "rax ") == 3;
	printf("crashlog_test: caught %d, access violations %d, divide %d, C++ %d: %s\n", caught, av, dz, cpp, ok ? "PASS" : "FAIL");
	return ok ? 0 : 1;
}
