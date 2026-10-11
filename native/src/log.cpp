#include "log.h"
#include <windows.h>
#include <cstdio>
#include <cstdarg>
#include <mutex>

static FILE* g_file = nullptr;
static std::mutex g_mu;

void LogInit(void* module) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)module, path, MAX_PATH);
	char* dot = strrchr(path, '.');
	if (dot) strcpy_s(dot, path + MAX_PATH - dot, ".log");
	fopen_s(&g_file, path, "w");
}

void Log(const char* fmt, ...) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_file) return;
	SYSTEMTIME t; GetLocalTime(&t);
	fprintf(g_file, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list ap; va_start(ap, fmt); vfprintf(g_file, fmt, ap); va_end(ap);
	fputc('\n', g_file); fflush(g_file);
}
