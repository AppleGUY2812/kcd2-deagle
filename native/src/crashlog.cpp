// Crash records to fastload_crash.txt: see crashlog.h.
#include "crashlog.h"
#include "log.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstring>

namespace {

HANDLE g_file = INVALID_HANDLE_VALUE;
constexpr int kMax = 24;
std::atomic<int> g_n{0};                 // records written (slots taken)
std::atomic<long long> g_seen{0};        // crash-type exceptions seen this run
std::atomic<uintptr_t> g_rips[kMax];
SRWLOCK g_lock = SRWLOCK_INIT;           // only tried: a fault inside the handler itself must not wait on it
char g_buf[16384];                       // under g_lock: keeps the faulting thread's stack use small

void Put(const char* s, size_t n) { DWORD w = 0; WriteFile(g_file, s, (DWORD)n, &w, nullptr); }

// The module's own name from its export directory: no loader lock (the faulting thread may hold it).
const char* ModName(uintptr_t base) {
	const auto* dos = (const IMAGE_DOS_HEADER*)base;
	const auto* nt = (const IMAGE_NT_HEADERS64*)(base + dos->e_lfanew);
	const IMAGE_DATA_DIRECTORY& d = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
	if (!d.VirtualAddress || !d.Size) return nullptr;
	const auto* ex = (const IMAGE_EXPORT_DIRECTORY*)(base + d.VirtualAddress);
	return ex->Name ? (const char*)(base + ex->Name) : nullptr;
}

bool InCode(uintptr_t a, uintptr_t* base) {   // inside an executable section of a loaded image
	void* b = nullptr;
	if (!RtlPcToFileHeader((void*)a, &b) || !b) return false;
	const auto* dos = (const IMAGE_DOS_HEADER*)b;
	const auto* nt = (const IMAGE_NT_HEADERS64*)((const uint8_t*)b + dos->e_lfanew);
	const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
	const uintptr_t rva = a - (uintptr_t)b;
	for (int i = 0; i < nt->FileHeader.NumberOfSections; i++)
		if ((sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE) && rva >= sec[i].VirtualAddress &&
		    rva < sec[i].VirtualAddress + sec[i].Misc.VirtualSize) {
			*base = (uintptr_t)b;
			return true;
		}
	return false;
}

int Where(char* out, size_t n, uintptr_t a) {   // "module+offset", or the bare address
	void* b = nullptr;
	if (RtlPcToFileHeader((void*)a, &b) && b) {
		if (const char* nm = ModName((uintptr_t)b)) return snprintf(out, n, "%s+%llx", nm, (unsigned long long)(a - (uintptr_t)b));
		return snprintf(out, n, "%p+%llx", b, (unsigned long long)(a - (uintptr_t)b));
	}
	return snprintf(out, n, "%p", (void*)a);
}

LONG CALLBACK Handler(EXCEPTION_POINTERS* ep) {
	const DWORD code = ep->ExceptionRecord->ExceptionCode;
	switch (code) {
	case EXCEPTION_ACCESS_VIOLATION: case EXCEPTION_ILLEGAL_INSTRUCTION: case EXCEPTION_PRIV_INSTRUCTION:
	case EXCEPTION_INT_DIVIDE_BY_ZERO: case EXCEPTION_STACK_OVERFLOW: case EXCEPTION_IN_PAGE_ERROR:
	case EXCEPTION_DATATYPE_MISALIGNMENT: break;
	default: return EXCEPTION_CONTINUE_SEARCH;
	}
	if (g_file == INVALID_HANDLE_VALUE) return EXCEPTION_CONTINUE_SEARCH;
	const long long seen = ++g_seen;
	const uintptr_t rip = (uintptr_t)ep->ExceptionRecord->ExceptionAddress;
	const int taken = g_n.load();
	for (int i = 0; i < taken && i < kMax; i++)
		if (g_rips[i].load() == rip) return EXCEPTION_CONTINUE_SEARCH;
	const int slot = g_n.fetch_add(1);
	if (slot >= kMax) return EXCEPTION_CONTINUE_SEARCH;
	g_rips[slot] = rip;
	if (code == EXCEPTION_STACK_OVERFLOW) {   // little stack left: a fixed line only
		static const char m[] = "stack overflow (no details)\r\n";
		Put(m, sizeof m - 1);
		return EXCEPTION_CONTINUE_SEARCH;
	}
	if (!TryAcquireSRWLockExclusive(&g_lock)) return EXCEPTION_CONTINUE_SEARCH;
	const CONTEXT* c = ep->ContextRecord;
	char* b = g_buf;
	const size_t cap = sizeof g_buf;
	size_t n = 0;
	auto add = [&](int k) { if (k > 0) n = n + (size_t)k < cap ? n + (size_t)k : cap - 1; };
	SYSTEMTIME t;
	GetLocalTime(&t);
	char at[160], what[160];
	Where(at, sizeof at, rip);
	what[0] = 0;
	if ((code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR) && ep->ExceptionRecord->NumberParameters >= 2) {
		const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
		char addr[160];
		Where(addr, sizeof addr, (uintptr_t)ep->ExceptionRecord->ExceptionInformation[1]);
		snprintf(what, sizeof what, " (%s %s)", kind == 0 ? "read of" : kind == 1 ? "write to" : kind == 8 ? "run at" : "access to", addr);
	}
	add(snprintf(b + n, cap - n, "%02u:%02u:%02u.%03u thread %lu: exception %08lx at %s%s (#%lld crash-type exception this run, "
	             "record %d)\r\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds, GetCurrentThreadId(), code, at, what, seen,
	             slot + 1));
	add(snprintf(b + n, cap - n, "  rax %016llx rbx %016llx rcx %016llx rdx %016llx\r\n  rsi %016llx rdi %016llx rbp %016llx "
	             "rsp %016llx\r\n  r8  %016llx r9  %016llx r10 %016llx r11 %016llx\r\n  r12 %016llx r13 %016llx r14 %016llx "
	             "r15 %016llx\r\n", c->Rax, c->Rbx, c->Rcx, c->Rdx, c->Rsi, c->Rdi, c->Rbp, c->Rsp, c->R8, c->R9, c->R10, c->R11,
	             c->R12, c->R13, c->R14, c->R15));
	// Code addresses on the faulting frame's stack, up to the thread's stack base.
	const NT_TIB* tib = (const NT_TIB*)NtCurrentTeb();
	const uintptr_t top = (uintptr_t)tib->StackBase, low = (uintptr_t)tib->StackLimit;
	uintptr_t sp = (uintptr_t)c->Rsp & ~(uintptr_t)7;
	const bool spOk = sp >= low && sp < top;   // a wild rsp: no stack scan (a fault in here would end the game)
	const uintptr_t end = !spOk ? sp : sp + 0x1000 < top ? sp + 0x1000 : top;
	add(snprintf(b + n, cap - n, spOk ? "  stack:" : "  stack: rsp outside the thread's stack, not read"));
	int found = 0;
	for (uintptr_t p = sp; p + 8 <= end && found < 32; p += 8) {
		const uintptr_t v = *(const uintptr_t*)p;
		uintptr_t base = 0;
		if (!InCode(v, &base)) continue;
		char w[160];
		Where(w, sizeof w, v);
		add(snprintf(b + n, cap - n, " %s [+%llx]", w, (unsigned long long)(p - sp)));
		found++;
	}
	add(snprintf(b + n, cap - n, "\r\n"));
	Put(b, n);
	ReleaseSRWLockExclusive(&g_lock);
	return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

void Crashlog_Install(void* self) {
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	char* slash = strrchr(path, '\\');
	if (slash) slash[1] = 0;
	strcat_s(path, "fastload_crash.txt");
	g_file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
	if (g_file != INVALID_HANDLE_VALUE) {
		SYSTEMTIME t;
		GetLocalTime(&t);
		char line[96];
		const int k = snprintf(line, sizeof line, "--- start %04u-%02u-%02u %02u:%02u:%02u\r\n", t.wYear, t.wMonth, t.wDay, t.wHour,
		                       t.wMinute, t.wSecond);
		if (k > 0) Put(line, (size_t)k);
	}
	const bool h = g_file != INVALID_HANDLE_VALUE && AddVectoredExceptionHandler(1, Handler) != nullptr;
	Log("crashlog: %s", h ? "crash-type exceptions are written to fastload_crash.txt (each address once, at most 24 a run)"
	                      : "OFF (fastload_crash.txt could not be opened)");
}
