// Reading the load's streaming requests ahead: see streamahead.h.
#include "streamahead.h"
#include "log.h"
#include "ramgov.h"
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace {

constexpr ULONGLONG kRecordMaxMs = 30 * 60 * 1000;   // recording stops at the list, or this long after start
constexpr ULONGLONG kKeepMs = 30 * 60 * 1000;        // nothing held after this (a long stay at the menu)
constexpr ULONGLONG kListAfterMs = 5000;             // the list is written this long after gameplay first starts
constexpr ULONGLONG kFreeAfterMs = 20000;            // ... and what was never asked for is freed this long after
constexpr long long kCapBytes = 512ll << 20;         // held at once (16 GB PC: pakahead holds up to 2 GB too)
// Free RAM: ramgov.h (read while >= 1.5 GB free, free the farthest copies below 1.2 GB). Was a fixed 3.5 GB floor: boot
// 13 (2026-10-24) read 6,535 of 24,271 at the menu, then waited for RAM through the whole load (1.8 GB free).
// Readers in parallel: the game's IO thread reads one request at a time (queue depth 1); boot 13's single reader made
// 58 MB/s in ~145 KB reads. Several reads in flight are what an SSD needs.
constexpr int kReaders = 4;
constexpr int64_t kMerge = 256 << 10;                // neighbouring requests this close are read in one go ...
constexpr int64_t kChunk = 4 << 20;                  // ... up to this much per read
constexpr int64_t kMaxItem = 64ll << 20;
constexpr int kCheck = 100;                          // copies checked against the game's own read first
// While a save-load / fast-travel screen waits (readiness.cpp calls Streamahead_SetGate) the readers stop, so the disk
// is the game's, and a copy the game is handed is kept (up to kKeepBytes) for when it asks the same bytes again; the
// kept copies are freed when the screen opens. Why (2026-10-24): Settle's last seconds are textures, read by the game's
// one reader thread one request at a time (boot 31: 95 % of its samples in NtReadFile). Reading more of the list in the
// gate didn't help: a 64 MB window held the wrong copies (boot 33, 9.0 s), and 16 readers reading the rest of the list
// (boots 34-36, 10.6-11.7 s against 8.2-9.0 s) read ~800 MB the game didn't ask for, beside the game's own 250-330 MB/s,
// while ~4,000 of the game's requests in the gate asked again for exact pieces it had already been handed (and we freed).
constexpr long long kKeepBytes = 256ll << 20;

int g_mode = 1;
std::string g_dir;
ULONGLONG g_start = 0;
long long g_freq = 1, g_t0 = 0;
std::atomic<ULONGLONG> g_loadedAt{0};
std::atomic<bool> g_listDone{false};

inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

std::string Utf8(const wchar_t* w) {
	char a[2048] = {};
	WideCharToMultiByte(CP_UTF8, 0, w, -1, a, sizeof a, nullptr, nullptr);
	return a;
}
std::wstring Wide(const std::string& s) {
	wchar_t w[1100] = {};
	MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w, 1100);
	return w;
}
uint64_t Key(int pak, uint32_t dataOff, int64_t offset, int64_t size) {
	uint64_t x = ((uint64_t)(uint32_t)pak << 32) ^ dataOff;
	x ^= (uint64_t)offset * 0x9E3779B97F4A7C15ull;
	x ^= (uint64_t)size * 0xC2B2AE3D27D4EB4Full;
	x ^= x >> 31; x *= 0xBF58476D1CE4E5B9ull; x ^= x >> 29;
	return x;
}

// ---- paks: the game's cache object + handle -> the pak's path (lower case), size and write time
struct PakFile { std::string path; unsigned long long size = 0, mtime = 0; };
struct PakSeen { uint8_t* cache = nullptr; HANDLE h = nullptr; int rec = -1, list = -1; };
std::mutex g_pakMu;
std::vector<PakSeen> g_seen;
std::vector<PakFile> g_recPaks;                      // this session's recording
std::unordered_map<std::string, int> g_recPakIdx;
std::vector<PakFile> g_listPaks;                     // the list's paks
std::unordered_map<std::string, int> g_listPakIdx;   // only paks unchanged since the list was made

bool InfoOf(HANDLE h, PakFile& p) {
	wchar_t w[1024];
	const DWORD n = GetFinalPathNameByHandleW(h, w, 1024, 0);
	if (!n || n >= 1024) return false;
	CharLowerW(w);
	p.path = Utf8(w);
	BY_HANDLE_FILE_INFORMATION fi{};
	if (!GetFileInformationByHandle(h, &fi)) return false;
	p.size = ((unsigned long long)fi.nFileSizeHigh << 32) | fi.nFileSizeLow;
	p.mtime = ((unsigned long long)fi.ftLastWriteTime.dwHighDateTime << 32) | fi.ftLastWriteTime.dwLowDateTime;
	return true;
}

PakSeen Pak(uint8_t* cache) {
	const HANDLE h = *(HANDLE*)(cache + 0x10);
	thread_local PakSeen last;
	if (last.cache == cache && last.h == h) return last;
	std::lock_guard<std::mutex> g(g_pakMu);
	for (auto& s : g_seen) if (s.cache == cache && s.h == h) return last = s;
	PakSeen s;
	s.cache = cache;
	s.h = h;
	PakFile p;
	if (InfoOf(h, p)) {
		auto it = g_recPakIdx.find(p.path);
		if (it == g_recPakIdx.end()) { it = g_recPakIdx.emplace(p.path, (int)g_recPaks.size()).first; g_recPaks.push_back(p); }
		s.rec = it->second;
		auto l = g_listPakIdx.find(p.path);
		if (l != g_listPakIdx.end() && g_listPaks[l->second].size == p.size && g_listPaks[l->second].mtime == p.mtime) s.list = l->second;
	}
	for (auto& o : g_seen) if (o.cache == cache) { o = s; return last = s; }   // the cache object now holds another pak
	g_seen.push_back(s);
	return last = s;
}

// ---- recording
struct Rec { uint32_t tMs, us; int32_t pak; uint32_t dataOff; int64_t offset, size; };
std::mutex g_recMu;
std::vector<Rec> g_recs;

// ---- the list, read ahead
struct Item {
	int pak = -1;
	uint32_t dataOff = 0;
	int64_t offset = 0, size = 0, abs = 0;
	std::atomic<int> state{0};    // 0 waiting, 1 being read, 2 ready, 3 taken or dropped, 5 being freed for RAM (then 0),
	                              // 7 a kept copy being handed over again
	uint8_t* data = nullptr;
	bool freedOnce = false;       // freed for RAM once: the next time it is dropped for good (boot 16: 3.6 GB read for 1.9 GB)
	std::atomic<bool> kept{false};      // handed to the game during a loading screen and kept for a repeat (state 2, or 7
	                                    // while a repeat copies it); freed when the screen opens
};
std::unique_ptr<Item[]> g_items;
size_t g_n = 0;
std::unordered_map<uint64_t, uint32_t> g_byKey;     // read-only once the reader starts
std::atomic<bool> g_serveOn{false}, g_stop{false}, g_loadSeen{false}, g_readerDone{false};
std::atomic<long long> g_held{0};
std::mutex g_chunkMu;                                // readers pick their next chunk under this
size_t g_cursor = 0;                                 // first list item not handed to a reader yet
std::atomic<int> g_readersLeft{0};
std::atomic<size_t> g_servedHi{0};                  // 1 + the highest list index served: RAM is freed above it first
std::atomic<bool> g_gate{false};                    // a loading screen waits (Streamahead_SetGate)
std::atomic<int> g_inFlight{0}, s_inFlightMax{0};   // reads under way now, and the most at once
std::atomic<long long> s_keptServed{0}, s_keptServedBytes{0}, s_keptBytes{0}, s_keptPeak{0};
std::atomic<long long> s_shedBytes{0}, s_shedTimes{0}, s_shedDropped{0}, s_again2{0};
std::atomic<int> g_checkLeft{kCheck};
std::atomic<long long> s_ready{0}, s_readBytes{0}, s_reads{0}, s_failed{0}, s_readTicks{0}, s_waitRam{0};
std::atomic<long long> s_served{0}, s_servedBytes{0}, s_checked{0}, s_bad{0}, s_gameFirst{0}, s_waited{0}, s_again{0},
	s_notListed{0}, s_timeouts{0};
double g_listMb = 0;

bool LoadList() {
	FILE* f = fopen((g_dir + "fastload_streamahead_list.txt").c_str(), "r");
	if (!f) { Log("streamahead: no list yet (it is written 5 s after gameplay first starts)"); return false; }
	struct Raw { int pak; uint32_t dataOff; long long offset, size; };
	std::vector<Raw> raw;
	std::vector<bool> ok;
	size_t changed = 0, skipped = 0;
	char line[1400];
	while (fgets(line, sizeof line, f)) {
		if (!strncmp(line, "#pak\t", 5)) {
			int idx = -1;
			unsigned long long sz = 0, mt = 0;
			char path[1100] = {};
			if (sscanf(line + 5, "%d\t%llu\t%llu\t%1099[^\r\n]", &idx, &sz, &mt, path) != 4 || idx != (int)g_listPaks.size()) continue;
			PakFile p;
			p.path = path; p.size = sz; p.mtime = mt;
			WIN32_FILE_ATTRIBUTE_DATA a{};
			const bool same = GetFileAttributesExW(Wide(p.path).c_str(), GetFileExInfoStandard, &a) &&
			                  (((unsigned long long)a.nFileSizeHigh << 32) | a.nFileSizeLow) == sz &&
			                  (((unsigned long long)a.ftLastWriteTime.dwHighDateTime << 32) | a.ftLastWriteTime.dwLowDateTime) == mt;
			ok.push_back(same);
			if (same) g_listPakIdx.emplace(p.path, (int)g_listPaks.size());
			else changed++;
			g_listPaks.push_back(p);
			continue;
		}
		if (line[0] == '#') continue;
		int pak = -1;
		unsigned dataOff = 0;
		long long off = 0, size = 0;
		if (sscanf(line, "%d\t%u\t%lld\t%lld", &pak, &dataOff, &off, &size) != 4) continue;
		if (pak < 0 || pak >= (int)ok.size() || !ok[pak] || size <= 0 || size > kMaxItem || off < 0) { skipped++; continue; }
		raw.push_back({ pak, dataOff, off, size });
	}
	fclose(f);
	g_n = raw.size();
	g_items.reset(new Item[g_n]);
	double bytes = 0;
	for (size_t i = 0; i < g_n; i++) {
		Item& it = g_items[i];
		it.pak = raw[i].pak; it.dataOff = raw[i].dataOff; it.offset = raw[i].offset; it.size = raw[i].size;
		it.abs = raw[i].offset + raw[i].dataOff;
		g_byKey.emplace(Key(it.pak, it.dataOff, it.offset, it.size), (uint32_t)i);
		bytes += (double)it.size;
	}
	g_listMb = bytes / 1e6;
	Log("streamahead: list %zu requests, %.0f MB from %zu paks (%zu paks changed since it was made, %zu requests skipped)",
	    g_n, g_listMb, g_listPaks.size(), changed, skipped);
	return g_n > 0;
}

void IoPriority(HANDLE h, bool low) {
	FILE_IO_PRIORITY_HINT_INFO p{};
	p.PriorityHint = low ? IoPriorityHintLow : IoPriorityHintNormal;
	SetFileInformationByHandle(h, FileIoPriorityHintInfo, &p, sizeof p);
}

// may a reader take another chunk now (not while a loading screen waits: the disk is the game's then)
bool MayRead() {
	return !g_gate.load(std::memory_order_relaxed) && g_held.load() + kChunk <= kCapBytes && ramgov::CanTake(kChunk);
}

// next chunk for a reader: the first waiting item and its neighbours in the same pak (state 0 -> 1). false = list done
bool NextChunk(std::vector<size_t>& chunk, int& pak, int64_t& lo, int64_t& hi) {
	std::lock_guard<std::mutex> g(g_chunkMu);
	for (;;) {
		while (g_cursor < g_n && g_items[g_cursor].state.load() != 0) g_cursor++;
		if (g_cursor >= g_n) return false;
		Item& first = g_items[g_cursor];
		int e0 = 0;
		if (!first.state.compare_exchange_strong(e0, 1)) { g_cursor++; continue; }   // the game asked first
		chunk.clear();
		chunk.push_back(g_cursor);
		pak = first.pak;
		lo = first.abs;
		hi = first.abs + first.size;
		size_t j = g_cursor + 1;
		for (; j < g_n; j++) {
			Item& it = g_items[j];
			if (it.pak != first.pak || it.abs < lo || it.abs > hi + kMerge || it.abs + it.size - lo > kChunk) break;
			if (g_held.load() + (hi - lo) + it.size > kCapBytes) break;
			int e = 0;
			if (!it.state.compare_exchange_strong(e, 1)) continue;   // already taken: its bytes just get read along
			chunk.push_back(j);
			if (it.abs + it.size > hi) hi = it.abs + it.size;
		}
		g_cursor = j;
		return true;
	}
}

// RAM is short: frees ready copies from the far end (list order) until `want` bytes are freed; they wait again
void Shed(long long want) {
	size_t top;
	{ std::lock_guard<std::mutex> g(g_chunkMu); top = g_cursor; }
	const size_t floorIdx = g_servedHi.load();
	long long got = 0;
	size_t lowest = top;
	for (size_t i = top; i-- > floorIdx && got < want;) {
		Item& it = g_items[i];
		if (it.kept.load(std::memory_order_relaxed)) continue;   // a loading screen keeps it; freed when the screen opens
		int r = 2;
		if (!it.state.compare_exchange_strong(r, 5)) continue;
		free(it.data);
		it.data = nullptr;
		g_held -= it.size;
		got += it.size;
		if (it.freedOnce) { s_ready--; it.state.store(3, std::memory_order_release); s_shedDropped++; continue; }
		it.freedOnce = true;
		s_ready--;   // counted again when it is read again
		s_again2++;
		it.state.store(0, std::memory_order_release);
		lowest = i;
	}
	if (!got) return;
	{ std::lock_guard<std::mutex> g(g_chunkMu); if (lowest < g_cursor) g_cursor = lowest; }
	s_shedBytes += got;
	if (s_shedTimes++ == 0)
		Log("streamahead: free RAM %lld MB, below %lld: freeing the farthest-ahead copies (%.0f MB now; read again when "
		    "there is room)", ramgov::AvailMb(), ramgov::ShedLevel().load(), got / 1e6);
}

DWORD WINAPI Reader(LPVOID) {
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_BELOW_NORMAL);
	std::vector<HANDLE> hs(g_listPaks.size(), INVALID_HANDLE_VALUE);
	bool low = true;   // low I/O priority until the load asks for listed data: the menu and the game's start come first
	size_t cap = 0;
	uint8_t* buf = nullptr;   // allocated at the first read, grown for a bigger one
	std::vector<size_t> chunk;
	const long long t0 = Now();
	while (!g_stop) {
		bool counted = false;
		while (!g_stop && !MayRead()) {
			if (!counted && !g_gate.load(std::memory_order_relaxed)) { s_waitRam++; counted = true; }
			if (const long long want = ramgov::ShedBytes()) Shed(want);
			Sleep(20);
		}
		if (g_stop) break;
		if (low && g_loadSeen.load()) {
			low = false;
			for (HANDLE h : hs) if (h != INVALID_HANDLE_VALUE) IoPriority(h, false);
		}
		int pak = 0;
		int64_t lo = 0, hi = 0;
		if (!NextChunk(chunk, pak, lo, hi)) {   // nothing waiting: wait, RAM shedding may put copies back
			Sleep(50);
			continue;
		}
		HANDLE& h = hs[pak];
		if (h == INVALID_HANDLE_VALUE) {
			h = CreateFileW(Wide(g_listPaks[pak].path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
			                nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
			if (h != INVALID_HANDLE_VALUE) IoPriority(h, low);
		}
		const int64_t s = lo & ~4095ll, e = (hi + 4095) & ~4095ll;
		if ((size_t)(e - s) > cap) {
			if (buf) VirtualFree(buf, 0, MEM_RELEASE);
			cap = (size_t)((e - s + (1 << 20) - 1) & ~((1ll << 20) - 1));
			buf = (uint8_t*)VirtualAlloc(nullptr, cap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
		}
		bool ok = false;
		if (buf && h != INVALID_HANDLE_VALUE) {
			OVERLAPPED ov{};
			ov.Offset = (DWORD)(s & 0xffffffff);
			ov.OffsetHigh = (DWORD)((uint64_t)s >> 32);
			DWORD got = 0;
			const int now = ++g_inFlight;
			for (int m = s_inFlightMax.load(); now > m && !s_inFlightMax.compare_exchange_weak(m, now);) {}
			ok = ReadFile(h, buf, (DWORD)(e - s), &got, &ov) && (int64_t)got >= hi - s;
			--g_inFlight;
		}
		for (size_t k : chunk) {
			Item& it = g_items[k];
			if (ok) it.data = (uint8_t*)malloc((size_t)it.size);
			if (ok && it.data) {
				memcpy(it.data, buf + (it.abs - s), (size_t)it.size);
				g_held += it.size;
				s_ready++;
				it.state.store(2, std::memory_order_release);
			} else {
				s_failed++;
				it.state.store(3, std::memory_order_release);
			}
		}
		s_reads++;
		s_readBytes += e - s;
		s_readTicks = Now() - t0;
	}
	for (HANDLE h : hs) if (h != INVALID_HANDLE_VALUE) CloseHandle(h);
	if (buf) VirtualFree(buf, 0, MEM_RELEASE);
	if (g_readersLeft.fetch_sub(1) == 1) g_readerDone = true;
	return 0;
}

void FreeAll(const char* why) {
	g_stop = true;
	g_serveOn = false;
	for (int k = 0; k < 500 && !g_readerDone; k++) Sleep(10);
	size_t n = 0;
	double bytes = 0;
	for (size_t i = 0; i < g_n; i++) {
		Item& it = g_items[i];
		int e = 2;
		if (it.state.compare_exchange_strong(e, 3)) { bytes += (double)it.size; free(it.data); it.data = nullptr; g_held -= it.size; n++; }
	}
	Log("streamahead: read-ahead store closed (%s): %zu requests, %.0f MB never asked for, freed", why, n, bytes / 1e6);
}

void FlushTrace() {
	std::vector<Rec> recs;
	std::vector<PakFile> paks;
	{ std::lock_guard<std::mutex> g(g_recMu); recs = g_recs; }
	{ std::lock_guard<std::mutex> g(g_pakMu); paks = g_recPaks; }
	const std::string tmp = g_dir + "fastload_streamahead_trace.tmp", out = g_dir + "fastload_streamahead_trace.csv";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	for (size_t i = 0; i < paks.size(); i++) fprintf(f, "#pak,%zu,%llu,%llu,%s\n", i, paks[i].size, paks[i].mtime, paks[i].path.c_str());
	fprintf(f, "t_ms,pak,data_off,offset,size,us\n");
	for (const Rec& r : recs) fprintf(f, "%u,%d,%u,%lld,%lld,%u\n", r.tMs, r.pak, r.dataOff, (long long)r.offset, (long long)r.size, r.us);
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
}

void WriteList() {
	std::vector<Rec> recs;
	std::vector<PakFile> paks;
	{ std::lock_guard<std::mutex> g(g_recMu); recs = g_recs; }
	{ std::lock_guard<std::mutex> g(g_pakMu); paks = g_recPaks; }
	if (recs.size() < 200) { Log("streamahead: only %zu stream requests before gameplay - the list stays as it was", recs.size()); return; }
	std::unordered_set<uint64_t> seen;
	const std::string tmp = g_dir + "fastload_streamahead_list.tmp", out = g_dir + "fastload_streamahead_list.txt";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f) return;
	fprintf(f, "# fastload plugin: the streaming requests of the last boot load, in order (streamahead.cpp). Rewritten every start.\n");
	for (size_t i = 0; i < paks.size(); i++) fprintf(f, "#pak\t%zu\t%llu\t%llu\t%s\n", i, paks[i].size, paks[i].mtime, paks[i].path.c_str());
	size_t n = 0;
	double bytes = 0;
	for (const Rec& r : recs) {
		if (!seen.insert(Key(r.pak, r.dataOff, r.offset, r.size)).second) continue;
		fprintf(f, "%d\t%u\t%lld\t%lld\n", r.pak, r.dataOff, (long long)r.offset, (long long)r.size);
		n++;
		bytes += (double)r.size;
	}
	fclose(f);
	MoveFileExA(tmp.c_str(), out.c_str(), MOVEFILE_REPLACE_EXISTING);
	Log("streamahead: list for the next start written: %zu requests, %.0f MB from %zu paks (%zu requests this load)", n,
	    bytes / 1e6, paks.size(), recs.size());
}

void Status() {
	Log("streamahead: read ahead %lld of %zu (%.0f MB in %lld reads, %.1f s, %lld failed, waited for RAM %lld x, freed for RAM "
	    "%.0f MB in %lld goes: %lld to read again, %lld dropped), held %.0f MB; "
	    "served %lld (%.0f MB), checked %lld, mismatches %lld; game came first %lld, waited on our read %lld (timeouts %lld), "
	    "asked again %lld, not listed %lld; in loading screens repeats served from kept copies %lld (%.0f MB, most kept %.0f MB); most reads in flight %d", s_ready.load(), g_n, s_readBytes.load() / 1e6, s_reads.load(),
	    (double)s_readTicks.load() / g_freq, s_failed.load(), s_waitRam.load(), s_shedBytes.load() / 1e6, s_shedTimes.load(),
	    s_again2.load(), s_shedDropped.load(),
	    g_held.load() / 1e6, s_served.load(),
	    s_servedBytes.load() / 1e6, s_checked.load(), s_bad.load(), s_gameFirst.load(), s_waited.load(), s_timeouts.load(),
	    s_again.load(), s_notListed.load(), s_keptServed.load(), s_keptServedBytes.load() / 1e6, s_keptPeak.load() / 1e6, s_inFlightMax.load());
}

DWORD WINAPI Writer(LPVOID) {
	size_t written = 0;
	long long lastSig = -1;
	bool freed = false;
	ULONGLONG lastFlush = 0;
	for (;;) {
		Sleep(1000);
		const ULONGLONG now = GetTickCount64();
		const ULONGLONG la = g_loadedAt.load();
		if (now - lastFlush >= 5000 && !g_listDone) {
			lastFlush = now;
			size_t n;
			{ std::lock_guard<std::mutex> g(g_recMu); n = g_recs.size(); }
			if (n != written) { FlushTrace(); written = n; }
		}
		if (!g_listDone && la && now - la >= kListAfterMs) {
			g_listDone = true;   // recording stops here
			FlushTrace();
			WriteList();
		}
		if (!g_listDone && now - g_start > kRecordMaxMs) { g_listDone = true; FlushTrace(); Log("streamahead: recording stopped (30 minutes, no gameplay start seen)"); }
		if (g_n && !freed && !g_stop) if (const long long want = ramgov::ShedBytes()) Shed(want);   // readers may be done
		if (g_n) {
			const long long sig = s_ready + s_served * 7 + s_gameFirst * 13 + s_notListed * 17 + s_bad;
			if (sig != lastSig && (now - g_start) % 2000 < 1000) { lastSig = sig; Status(); }
			if (!freed && ((la && now - la >= kFreeAfterMs) || now - g_start >= kKeepMs)) {
				freed = true;
				Status();
				FreeAll(la ? "20 s after gameplay started" : "30 minutes after start");
			}
		}
		if (g_listDone && (!g_n || freed)) return 0;
	}
}

}  // namespace

int Streamahead_Serve(uint8_t* cache, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size, int64_t want,
                      StreamReadFn orig) {
	if (!g_serveOn.load(std::memory_order_relaxed) || want <= 0) return -1;
	const PakSeen p = Pak(cache);
	if (p.list < 0) { s_notListed++; return -1; }
	const uint32_t dataOff = *(uint32_t*)(entry + 0xc);
	const auto f = g_byKey.find(Key(p.list, dataOff, offset, want));
	if (f == g_byKey.end()) { s_notListed++; return -1; }
	Item& it = g_items[f->second];
	if (it.pak != p.list || it.dataOff != dataOff || it.offset != offset || it.size != want) { s_notListed++; return -1; }
	g_loadSeen.store(true, std::memory_order_relaxed);
	int st = it.state.load(std::memory_order_acquire);
	while (st == 5 || st == 7) { SwitchToThread(); st = it.state.load(std::memory_order_acquire); }   // being freed (back at 0
	                                                                                               // next) / a repeat copying
	if (st == 1) {   // our read of it is under way (one read of up to 4 MB, ~10 ms): wait for it, at most 100 ms
		s_waited++;
		const long long t0 = Now();
		while ((st = it.state.load(std::memory_order_acquire)) == 1 && Now() - t0 < g_freq / 10) SwitchToThread();
		if (st == 1) { s_timeouts++; return -1; }
	}
	if (st == 0) {
		int e = 0;
		if (it.state.compare_exchange_strong(e, 3)) { s_gameFirst++; return -1; }   // not read yet: the game reads it
		st = e;
	}
	const size_t idx = f->second;
	if (st == 2 && g_gate.load(std::memory_order_relaxed) && g_checkLeft.load() <= 0) {   // a loading screen: keep it
		for (size_t hi = g_servedHi.load(); hi < idx + 1 && !g_servedHi.compare_exchange_weak(hi, idx + 1);) {}
		int e7 = 2;
		if (it.state.compare_exchange_strong(e7, 7)) {
			const bool repeat = it.kept.load(std::memory_order_relaxed);
			bool keep = repeat;
			if (!repeat && s_keptBytes.load() + want <= kKeepBytes) {
				keep = true;
				it.kept.store(true, std::memory_order_relaxed);
				const long long k = s_keptBytes += want;
				for (long long m = s_keptPeak.load(); k > m && !s_keptPeak.compare_exchange_weak(m, k);) {}
			}
			memcpy(out, it.data, (size_t)want);
			if (repeat) { s_keptServed++; s_keptServedBytes += want; }
			else { s_served++; s_servedBytes += want; }
			if (keep) { it.state.store(2, std::memory_order_release); return 0; }
			free(it.data);   // the keep budget is full: as outside a loading screen
			it.data = nullptr;
			g_held -= want;
			it.state.store(3, std::memory_order_release);
			return 0;
		}
		st = e7;
	}
	int e2 = 2;
	if (st != 2 || !it.state.compare_exchange_strong(e2, 3)) { s_again++; return -1; }
	if (it.kept.exchange(false)) s_keptBytes -= want;   // the screen opened meanwhile: no longer kept
	uint8_t* d = it.data;
	it.data = nullptr;
	for (size_t hi = g_servedHi.load(); hi < idx + 1 && !g_servedHi.compare_exchange_weak(hi, idx + 1);) {}
	int r = 0;
	if (g_checkLeft.load() > 0) {   // self-check: the game reads it too and keeps its own bytes
		r = (int)orig(cache, entry, out, offset, size);
		if (r == 0) {
			if (memcmp(d, out, (size_t)want)) {
				s_bad++;
				if (g_serveOn.exchange(false))
					Log("streamahead: MISMATCH on a %lld-byte request (data offset %u, offset %lld) - serving off for this session",
					    (long long)want, dataOff, (long long)offset);
			} else if (s_checked++, g_checkLeft.fetch_sub(1) == 1) {
				Log("streamahead: self-check done (%d copies matched the game's own read), serving from memory now", kCheck);
			}
		}
	} else {
		memcpy(out, d, (size_t)want);
		s_served++;
		s_servedBytes += want;
	}
	free(d);
	g_held -= want;
	return r;
}

void Streamahead_Record(uint8_t* cache, uint8_t* entry, int64_t offset, int64_t want, long long ticks) {
	if (g_mode < 1 || g_listDone.load(std::memory_order_relaxed) || !entry) return;
	const PakSeen p = Pak(cache);
	if (p.rec < 0) return;
	Rec r{ (uint32_t)((Now() - g_t0) * 1000 / g_freq), (uint32_t)(ticks * 1000000 / g_freq), p.rec, *(uint32_t*)(entry + 0xc),
	       offset, want };
	std::lock_guard<std::mutex> g(g_recMu);
	g_recs.push_back(r);
}

void Streamahead_Stats(long long* served, long long* gameFirst, long long* notListed) {
	*served = s_served.load();
	*gameFirst = s_gameFirst.load();
	*notListed = s_notListed.load();
}

void Streamahead_SetGate(bool open) {
	if (g_gate.exchange(open) == open || !g_n || g_stop) return;   // not after the store closed (fast travel later on)
	size_t freedN = 0;
	long long freedB = 0;
	if (!open) {   // the kept copies go (a repeat being copied right now finishes first)
		for (size_t i = 0; i < g_n; i++) {
			Item& it = g_items[i];
			if (!it.kept.load(std::memory_order_acquire)) continue;
			int e = 2;
			while (!it.state.compare_exchange_weak(e, 3)) { if (e != 7 && e != 2) break; if (e == 7) SwitchToThread(); e = 2; }
			if (e != 2) continue;
			it.kept = false;
			free(it.data);
			it.data = nullptr;
			g_held -= it.size;
			s_keptBytes -= it.size;
			freedB += it.size;
			freedN++;
		}
	}
	Log("streamahead: loading screen %s (%lld MB free, %.0f MB held)", open
	    ? "waits: the readers stop, copies handed to the game are kept for repeats (up to 256 MB)"
	    : "gone: the readers go on", ramgov::AvailMb(), g_held.load() / 1e6);
	if (!open)
		Log("streamahead: in that loading screen %lld repeats came from kept copies so far (%.0f MB); %zu kept copies freed (%.0f MB)",
		    s_keptServed.load(), s_keptServedBytes.load() / 1e6, freedN, freedB / 1e6);
}

long long Streamahead_Ready() { return s_ready.load(); }
long long Streamahead_KeptServed() { return s_keptServed.load(); }
long long Streamahead_HeldBytes() { return g_held.load(); }

void Streamahead_OnLoaded() {
	ULONGLONG z = 0;
	g_loadedAt.compare_exchange_strong(z, GetTickCount64());
}

void Streamahead_Install(void* self) {
	g_start = GetTickCount64();
	LARGE_INTEGER fq; QueryPerformanceFrequency(&fq); g_freq = fq.QuadPart; g_t0 = Now();
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path;
	g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((g_dir + "fastload_streamahead.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(fp);
	}
	if (g_mode == 0) { Log("streamahead: off"); g_listDone = true; return; }
	g_recs.reserve(100000);
	if (g_mode == 2 && LoadList()) {
		g_serveOn = true;
		Log("streamahead: reading ahead with %d readers, at most %lld MB held, while >= %lld MB RAM free (%lld MB free now)",
		    kReaders, kCapBytes >> 20, ramgov::TakeLevel().load(), ramgov::AvailMb());
		g_readersLeft = kReaders;
		for (int r = 0; r < kReaders; r++) CloseHandle(CreateThread(nullptr, 0, Reader, nullptr, 0, nullptr));
	}
	Log("streamahead: mode %d (%s)", g_mode, g_mode == 2 ? "record + serve" : "record");
	CloseHandle(CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr));
}

void Streamahead_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && *args >= '0' && *args <= '2' && (args[1] == 0 || args[1] == ' ')) {
		if (FILE* f = fopen((g_dir + "fastload_streamahead.txt").c_str(), "w")) { fprintf(f, "%c\n", *args); fclose(f); }
		Log("streamahead: mode %c from the next start (fastload_streamahead.txt); now %d", *args, g_mode);
		return;
	}
	size_t n;
	{ std::lock_guard<std::mutex> g(g_recMu); n = g_recs.size(); }
	Log("streamahead: mode %d, %zu requests recorded, list %zu requests %.0f MB  (streamahead 0|1|2)", g_mode, n, g_n, g_listMb);
	if (g_n) Status();
}
