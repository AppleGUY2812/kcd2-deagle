#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>
#include <share.h>

static FILE* g_file = nullptr;
static std::mutex g_mu;

void LogInit(void* module) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)module, path, MAX_PATH);
	char* dot = strrchr(path, '.');
	if (dot) strcpy_s(dot, path + MAX_PATH - dot, ".log");
	// the last runs stay readable: X.log -> X.1.log -> X.2.log (the next start used to overwrite the only copy:
	// "more fps after a restart, is something leaking?" had no earlier session to compare with, 2026-10-27)
	if (dot) {
		char older[MAX_PATH], newer[MAX_PATH];
		const size_t stem = (size_t)(dot - path);
		for (int i = 2; i >= 1; i--) {
			snprintf(older, sizeof older, "%.*s.%d.log", (int)stem, path, i);
			if (i == 1) strcpy_s(newer, path); else snprintf(newer, sizeof newer, "%.*s.%d.log", (int)stem, path, i - 1);
			MoveFileExA(newer, older, MOVEFILE_REPLACE_EXISTING);
		}
	}
	g_file = _fsopen(path, "w", _SH_DENYWR);   // others may read it while the game runs (fopen_s locks it)
}

void Log(const char* fmt, ...) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_file) return;
	SYSTEMTIME t; GetLocalTime(&t);
	fprintf(g_file, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list ap; va_start(ap, fmt); vfprintf(g_file, fmt, ap); va_end(ap);
	fputc('\n', g_file); fflush(g_file);
}
