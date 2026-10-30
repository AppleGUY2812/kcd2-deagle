// Outside-the-game test of ashang.cpp's slow-frame sampler, a Windows exe. Two fake AS2 function objects laid out as
// WHGame 1.5.6's (+0x78 action buffer object whose first pointer is the code, +0x98 start, +0x9c length): "SB_Run"
// (a DefineFunction2 record with that name right before its start) and an unnamed one after a push of "Remove". The
// "main thread" draws 10 ms frames, then one ~300 ms frame: 100 ms in SB_Run, 200 ms in the unnamed one called from it.
// The log must show one slow frame naming SB_Run at ~100 % on the stack and the unnamed one at ~83 % innermost (samples start 60 ms into the frame).
// Run: ashang_test.exe   (writes ashang_test.log next to the exe)
#include "../src/ashang.h"
#include "../src/log.h"
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

extern "C" IMAGE_DOS_HEADER __ImageBase;
static volatile unsigned long long g_sink;

static void Busy(int ms) {
	const ULONGLONG end = GetTickCount64() + ms;
	unsigned long long x = 1;
	while (GetTickCount64() < end) for (int i = 0; i < 10000; i++) x = x * 6364136223846793005ull + 1442695040888963407ull;
	g_sink = x;
}

static std::vector<unsigned char> g_code;
alignas(16) static unsigned char g_buf[64];        // the action buffer object: first pointer = the code bytes
alignas(16) static unsigned char g_fnA[0x100], g_fnB[0x100];

// a DefineFunction2 record (0x8e, u16 length, name, u16 params 0, u8 registers 0, u16 flags 0, u16 code size) then code
static int AddFunction(const char* name, int codeLen) {
	const int n = (int)strlen(name) + 1;
	const int recLen = n + 2 + 1 + 2 + 2;
	g_code.push_back(0x8e);
	g_code.push_back((unsigned char)(recLen & 0xff));
	g_code.push_back((unsigned char)(recLen >> 8));
	g_code.insert(g_code.end(), name, name + n);
	const unsigned char rest[] = { 0, 0, 0, 0, 0, (unsigned char)(codeLen & 0xff), (unsigned char)(codeLen >> 8) };
	g_code.insert(g_code.end(), rest, rest + sizeof rest);
	const int start = (int)g_code.size();
	g_code.insert(g_code.end(), codeLen, 0x00);
	return start;
}
static void SetFn(unsigned char* fn, int start, int len) {
	void* b = g_buf;
	memcpy(fn + 0x78, &b, 8);
	memcpy(fn + 0x98, &start, 4);
	memcpy(fn + 0x9c, &len, 4);
}

static void Inner() { Busy(200); }
static void Outer() { Busy(100); AsHang_TestCall(g_fnB, Inner); }
static void Frames(int n) { for (int i = 0; i < n; i++) { AsHang_Frame(); Sleep(10); } }

int main() {
	LogInit(&__ImageBase);
	const char push[] = { (char)0x96, 8, 0, 0, 'R', 'e', 'm', 'o', 'v', 'e', 0 };   // push "Remove"
	g_code.assign(push, push + sizeof push);
	const int sA = AddFunction("SB_Run", 40);
	g_code.insert(g_code.end(), push, push + sizeof push);
	const int sB = AddFunction("", 30);   // unnamed: "x = function()"
	void* code = g_code.data();
	memcpy(g_buf, &code, 8);
	SetFn(g_fnA, sA, 40);
	SetFn(g_fnB, sB, 30);
	AsHang_TestStart();
	Frames(20);
	AsHang_TestCall(g_fnA, Outer);
	Frames(20);
	Busy(80);    // a slow frame outside Flash script: not listed
	Frames(20);
	Sleep(100);
	char path[MAX_PATH];
	GetModuleFileNameA((HMODULE)&__ImageBase, path, MAX_PATH);
	std::string log(path);
	log = log.substr(0, log.find_last_of('.')) + ".log";
	FILE* f = fopen(log.c_str(), "r");
	if (!f) { printf("FAIL: no log\n"); return 1; }
	char line[1024];
	int frames = 0, ms = 0, aAny = -1, bAny = -1, bSelf = -1;
	while (fgets(line, sizeof line, f)) {
		if (!strstr(line, "ashang: ")) continue;
		printf("%s", line);
		const char* p = strstr(line, "slow frame ");
		if (p) { frames++; sscanf(p, "slow frame %d ms", &ms); continue; }
		int any = 0, self = 0;
		if (sscanf(line + (strstr(line, "ashang:") - line), "ashang: %d%% / %d%%", &any, &self) != 2) continue;
		if (strstr(line, " SB_Run ")) aAny = any;
		if (strstr(line, "(unnamed, after") && strstr(line, "Remove")) { bAny = any; bSelf = self; }
	}
	fclose(f);
	if (frames != 1 || ms < 250 || ms > 450 || aAny < 85 || bAny < 70 || bAny > 95 || bSelf < 70) {
		printf("FAIL: slow frames %d (want 1), %d ms (want ~300), SB_Run on the stack %d%% (want >= 85), unnamed %d%% / "
		       "innermost %d%% (want ~83: sampled from 60 ms on)\n", frames, ms, aAny, bAny, bSelf);
		return 1;
	}
	printf("ashang_test: all passed\n");
	return 0;
}
