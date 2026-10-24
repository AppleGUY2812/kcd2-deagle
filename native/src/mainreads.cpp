// The main thread's file reads for a short window. See mainreads.h.
#include "mainreads.h"
#include "log.h"
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct Read { char kind; std::string what; uint32_t off; uint64_t bytes; long long start, ticks; };
std::atomic<ULONGLONG> g_until{0};   // GetTickCount64 when the window ends, 0 = none
std::atomic<DWORD> g_tid{0};
std::mutex g_mu;
std::vector<Read> g_reads;
long long g_t0 = 0, g_freq = 1;
int g_asked = 0;
bool g_writer = false;              // a writer thread is waiting for the window to end
constexpr size_t kMaxReads = 20000;
constexpr int kMaxLines = 200, kSlowest = 15;

long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

struct Window { long long t0, freq; DWORD tid; int asked; };
double Ms(const Window& w, long long ticks) { return ticks * 1000.0 / (double)w.freq; }

void Line(const Window& w, const Read& r, bool nested) {
	char where[64] = "";   // pak reads: data offset and unpacked size (XML loads: the path says it all)
	if (r.kind == 'p' || r.kind == 'P') snprintf(where, sizeof where, " @%u %llu KB", r.off, (unsigned long long)(r.bytes >> 10));
	Log("mainreads: %s+%.0f ms %s %.1f ms %s%s%s", nested ? "    " : "", Ms(w, r.start - w.t0),
	    r.kind == 'x' || r.kind == 'X' ? "xml" : "pak", Ms(w, r.ticks), r.what.c_str(), where,
	    r.kind == 'X' || r.kind == 'P' ? " (ready-made)" : "");
}

DWORD WINAPI Writer(LPVOID) {
	std::vector<Read> reads;
	Window w{};
	for (;;) {   // the window can be extended while it is open: check its end under the lock
		ULONGLONG wait;
		{
			std::lock_guard<std::mutex> lk(g_mu);
			const ULONGLONG until = g_until.load(), now = GetTickCount64();
			if (now >= until) {
				g_until = 0;
				reads.swap(g_reads);
				w = { g_t0, g_freq, g_tid.load(), g_asked };
				g_writer = false;
				break;
			}
			wait = (std::min<ULONGLONG>)(until - now + 5, 1000);
		}
		Sleep((DWORD)wait);
	}
	std::stable_sort(reads.begin(), reads.end(), [](const Read& a, const Read& b) { return a.start < b.start; });
	long long xmlT = 0, pakT = 0, pakB = 0;
	int xml = 0, pak = 0, ready = 0;
	long long xmlEnd = 0;   // pak reads that start inside an XML load are part of it
	for (const Read& r : reads) {
		if (r.kind == 'X' || r.kind == 'P') ready++;
		if (r.kind == 'x') { xml++; xmlT += r.ticks; xmlEnd = (std::max)(xmlEnd, r.start + r.ticks); }
		if (r.kind == 'p') { pak++; pakB += (long long)r.bytes; if (r.start >= xmlEnd) pakT += r.ticks; }
	}
	Log("mainreads: %d ms window on thread %lu: %d XML loads %.0f ms, %d pak reads %.0f ms outside them (%.1f MB), "
	    "%d ready-made%s", w.asked, (unsigned long)w.tid, xml, Ms(w, xmlT), pak, Ms(w, pakT), pakB / 1048576.0, ready,
	    reads.size() >= kMaxReads ? " (list full: later reads not counted)" : "");
	if (reads.empty()) return 0;
	std::vector<const Read*> slow;
	for (const Read& r : reads) slow.push_back(&r);
	std::stable_sort(slow.begin(), slow.end(), [](const Read* a, const Read* b) { return a->ticks > b->ticks; });
	Log("mainreads: slowest:");
	for (size_t i = 0; i < slow.size() && i < (size_t)kSlowest; i++) Line(w, *slow[i], false);
	Log("mainreads: in order:");
	xmlEnd = 0;
	int lines = 0;
	for (const Read& r : reads) {
		if (lines >= kMaxLines) { Log("mainreads: ... %zu more", reads.size() - (size_t)lines); break; }
		const bool nested = (r.kind == 'p' || r.kind == 'P') && r.start < xmlEnd;
		if (r.kind == 'x' || r.kind == 'X') xmlEnd = (std::max)(xmlEnd, r.start + r.ticks);
		Line(w, r, nested);
		lines++;
	}
	return 0;
}
}  // namespace

void MainReads_Log(int ms) {
	if (ms <= 0) return;
	if (ms > 60000) ms = 60000;
	std::lock_guard<std::mutex> lk(g_mu);
	const ULONGLONG until = GetTickCount64() + (ULONGLONG)ms;
	if (!g_writer) {
		LARGE_INTEGER f; QueryPerformanceFrequency(&f); g_freq = f.QuadPart;
		g_t0 = Now();
		g_tid = GetCurrentThreadId();
		g_asked = ms;
		g_reads.clear();
		g_until = until;
		g_writer = true;
		if (HANDLE h = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr)) CloseHandle(h);
		else { g_writer = false; g_until = 0; }
		return;
	}
	if (until > g_until.load()) {   // extends the open window
		g_asked += (int)(until - g_until.load());
		g_until = until;
	}
}

bool MainReads_On() {
	const ULONGLONG until = g_until.load(std::memory_order_relaxed);
	return until && GetCurrentThreadId() == g_tid.load(std::memory_order_relaxed) && GetTickCount64() < until;
}

void MainReads_Add(char kind, const char* what, uint32_t off, uint64_t bytes, long long startTicks, long long ticks) {
	std::lock_guard<std::mutex> lk(g_mu);
	if (!g_until.load() || g_reads.size() >= kMaxReads) return;
	g_reads.push_back({ kind, what ? what : "?", off, bytes, startTicks, ticks });
}
