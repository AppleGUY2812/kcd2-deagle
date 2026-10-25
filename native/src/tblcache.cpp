// The object database's XML tables recorded once and replayed: see tblcache.h.
#include "tblcache.h"
#include "early.h"
#include "log.h"
#include "MinHook.h"
#include <windows.h>
#include <intrin.h>
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// WHGame 1.5.6
constexpr uintptr_t kRvaXmlLoad = 0xd1a11c;   // C_XMLTableSerializer vt+0x30: char (serializer, table)
const unsigned char kXmlLoadBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x41,0x56,0x41 };
constexpr uintptr_t kRvaStore = 0xd19f38;     // void (table, rows, rowCount, strings, stringBytes, stringCount, version)
const unsigned char kStoreBytes[] = { 0x40,0x53,0x48,0x83,0xec,0x30,0x8b,0x44,0x24,0x68,0x48,0x8b,0xd9,0x89 };
constexpr uintptr_t kRvaAllocFn = 0x549d378;  // the game's allocator: void* (size, int* charged, int)
constexpr uintptr_t kRvaAllocReady = 0x549d370, kRvaAllocInit = 0x45aaa0;
constexpr uintptr_t kRvaStatCharged = 0x51dfb20, kRvaStatBytes = 0x51dfb18, kRvaStatCount = 0x51dfb24;   // its counters

using XmlLoadFn = char(__fastcall*)(void* ser, void* table);
using StoreFn = void(__fastcall*)(void* table, void* rows, int rowCount, void* strings, int strBytes, int strCount, int version);
using AllocFn = void*(__fastcall*)(size_t size, int* charged, int flags);
XmlLoadFn g_origLoad = nullptr;
StoreFn g_origStore = nullptr;
uintptr_t g_base = 0;
int g_mode = 1;
std::string g_dir;
long long g_freq = 1;
inline long long Now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }
inline double Ms(long long a, long long b) { return (double)(b - a) * 1000.0 / g_freq; }

constexpr uint32_t kVer = 1;
struct Rec {   // one table file's result
	uint8_t serFlag = 0;
	int ncols = 0, stride = 0, rowCount = 0, strBytes = 0, strCount = 0, version = 0;
	std::vector<uint8_t> rows, strs;
};
std::mutex g_mu;
std::unordered_map<std::string, std::shared_ptr<Rec>> g_recs;   // file key -> record (the file's, then this run's)
uint64_t g_key = 0, g_fileKey = 0;
HANDLE g_ready = nullptr;   // the preload thread is done
std::atomic<int> g_hits{0}, g_misses{0}, g_new{0}, g_bad{0}, g_checked{0};
std::atomic<long long> g_hitTicks{0}, g_missTicks{0}, g_lastNew{0};
std::atomic<bool> g_writing{false}, g_reported{false}, g_done{false};   // done: records freed after the first level load

struct Ctx { void* table; int stride; bool got; std::shared_ptr<Rec> rec; };
thread_local Ctx* t_ctx = nullptr;

// ---- the key (as questreplay's: WHGame.dll and every pak / manifest that can hold a table) ----
inline void Mix(uint64_t& h, const void* p, size_t n) {
	for (size_t i = 0; i < n; i++) h = (h ^ ((const uint8_t*)p)[i]) * 0x100000001b3ull;
}
inline void MixS(uint64_t& h, const std::string& s) { Mix(h, s.data(), s.size()); Mix(h, "\0", 1); }
void MixDir(uint64_t& h, const std::string& dir, const char* pattern) {
	std::vector<std::string> v;
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((dir + pattern).c_str(), &fd);
	if (f == INVALID_HANDLE_VALUE) return;
	do {
		if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
		char b[512];
		snprintf(b, sizeof b, "%s|%lu|%lu|%lu|%lu", fd.cFileName, fd.nFileSizeHigh, fd.nFileSizeLow, fd.ftLastWriteTime.dwHighDateTime,
		         fd.ftLastWriteTime.dwLowDateTime);
		v.emplace_back(b);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
	std::sort(v.begin(), v.end());
	MixS(h, dir);
	for (auto& x : v) MixS(h, x);
}
std::vector<std::string> Subdirs(const std::string& dir) {
	std::vector<std::string> v;
	WIN32_FIND_DATAA fd;
	HANDLE f = FindFirstFileA((dir + "*").c_str(), &fd);
	if (f == INVALID_HANDLE_VALUE) return v;
	do {
		if ((fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && fd.cFileName[0] != '.') v.emplace_back(fd.cFileName);
	} while (FindNextFileA(f, &fd));
	FindClose(f);
	std::sort(v.begin(), v.end());
	return v;
}
uint64_t Key() {
	uint64_t h = 0xcbf29ce484222325ull;
	Mix(h, "DTBC", 4);
	Mix(h, &kVer, 4);
	const uintptr_t nt = g_base + *(int*)(g_base + 0x3c);
	Mix(h, (void*)(nt + 8), 4);      // WHGame.dll TimeDateStamp
	Mix(h, (void*)(nt + 0x50), 4);   // SizeOfImage
	const std::string root = g_dir + "..\\..\\";
	MixDir(h, root + "Data\\", "*.pak");
	MixDir(h, root + "Localization\\", "*.pak");
	for (auto& m : Subdirs(root + "Mods\\")) {
		MixDir(h, root + "Mods\\" + m + "\\Data\\", "*.pak");
		MixDir(h, root + "Mods\\" + m + "\\Localization\\", "*.pak");
		MixDir(h, root + "Mods\\" + m + "\\", "*.manifest");
	}
	MixDir(h, root + "Mods\\", "mod_order.txt");
	const std::string ws = root + "..\\..\\workshop\\content\\1771300\\";
	for (auto& m : Subdirs(ws)) {
		MixDir(h, ws + m + "\\Data\\", "*.pak");
		MixDir(h, ws + m + "\\Localization\\", "*.pak");
		MixDir(h, ws + m + "\\", "*.manifest");
	}
	return h;
}

// ---- the file: "DTBC", ver, key, count, then per record: u16 key length, key, u8 serializer flag, 6 x i32
// (ncols, stride, rowCount, strBytes, strCount, version), the rows (rowCount * stride bytes), the strings ----
std::string FilePath() { return g_dir + "fastload_tblcache.bin"; }

DWORD WINAPI Preload(LPVOID) {
	Early_NameThread(L"JobWorker_fastload tblcache read");
	const long long t0 = Now();
	g_key = Key();
	const double keyMs = Ms(t0, Now());
	FILE* f = fopen(FilePath().c_str(), "rb");
	if (!f) {
		Log("tblcache: no record yet (key #%016llx, %.0f ms): this start-up records the tables", (unsigned long long)g_key, keyMs);
		SetEvent(g_ready);
		return 0;
	}
	char magic[4] = {};
	uint32_t ver = 0, count = 0;
	uint64_t key = 0;
	bool ok = fread(magic, 4, 1, f) == 1 && !memcmp(magic, "DTBC", 4) && fread(&ver, 4, 1, f) == 1 && ver == kVer &&
	          fread(&key, 8, 1, f) == 1 && fread(&count, 4, 1, f) == 1 && count < 200000;
	g_fileKey = key;
	if (ok && key != g_key) {
		Log("tblcache: the record is for other game files (key #%016llx, now #%016llx): this start-up records again",
		    (unsigned long long)key, (unsigned long long)g_key);
		fclose(f);
		SetEvent(g_ready);
		return 0;
	}
	size_t bytes = 0;
	std::unordered_map<std::string, std::shared_ptr<Rec>> m;
	for (uint32_t i = 0; ok && i < count; i++) {
		uint16_t kl = 0;
		ok = fread(&kl, 2, 1, f) == 1;
		std::string k(kl, '\0');
		auto r = std::make_shared<Rec>();
		int v[6] = {};
		ok = ok && (!kl || fread(&k[0], 1, kl, f) == kl) && fread(&r->serFlag, 1, 1, f) == 1 && fread(v, 4, 6, f) == 6;
		if (!ok) break;
		r->ncols = v[0]; r->stride = v[1]; r->rowCount = v[2]; r->strBytes = v[3]; r->strCount = v[4]; r->version = v[5];
		const long long rb = (long long)r->rowCount * r->stride;
		ok = r->stride > 0 && r->rowCount >= 0 && r->strBytes >= 0 && rb < (1ll << 30) && r->strBytes < (1 << 30);
		if (!ok) break;
		r->rows.resize((size_t)rb);
		r->strs.resize((size_t)r->strBytes);
		ok = (!rb || fread(r->rows.data(), 1, (size_t)rb, f) == (size_t)rb) &&
		     (!r->strBytes || fread(r->strs.data(), 1, (size_t)r->strBytes, f) == (size_t)r->strBytes);
		bytes += (size_t)rb + r->strBytes;
		m[k] = std::move(r);
	}
	fclose(f);
	if (!ok) {
		Log("tblcache: fastload_tblcache.bin unreadable: ignored, this start-up records again");
	} else {
		std::lock_guard<std::mutex> l(g_mu);
		g_recs.swap(m);
		Log("tblcache: %u table records for these game files (key #%016llx, %.1f MB), read in %.0f ms (key %.0f ms)", count,
		    (unsigned long long)g_key, bytes / 1048576.0, Ms(t0, Now()), keyMs);
	}
	SetEvent(g_ready);
	return 0;
}

DWORD WINAPI Writer(LPVOID) {
	Early_NameThread(L"JobWorker_fastload tblcache write");
	const long long t0 = Now();
	std::vector<std::pair<std::string, std::shared_ptr<Rec>>> all;
	{
		std::lock_guard<std::mutex> l(g_mu);
		all.assign(g_recs.begin(), g_recs.end());
	}
	std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
	const std::string tmp = FilePath() + ".tmp";
	FILE* f = fopen(tmp.c_str(), "wb");
	bool ok = f != nullptr;
	size_t bytes = 0;
	if (ok) {
		const uint32_t n = (uint32_t)all.size();
		ok = fwrite("DTBC", 4, 1, f) == 1 && fwrite(&kVer, 4, 1, f) == 1 && fwrite(&g_key, 8, 1, f) == 1 && fwrite(&n, 4, 1, f) == 1;
		for (auto& [k, r] : all) {
			if (!ok) break;
			const uint16_t kl = (uint16_t)std::min<size_t>(k.size(), 65535);
			const int v[6] = { r->ncols, r->stride, r->rowCount, r->strBytes, r->strCount, r->version };
			ok = fwrite(&kl, 2, 1, f) == 1 && (!kl || fwrite(k.data(), 1, kl, f) == kl) && fwrite(&r->serFlag, 1, 1, f) == 1 &&
			     fwrite(v, 4, 6, f) == 6 && (r->rows.empty() || fwrite(r->rows.data(), 1, r->rows.size(), f) == r->rows.size()) &&
			     (r->strs.empty() || fwrite(r->strs.data(), 1, r->strs.size(), f) == r->strs.size());
			bytes += r->rows.size() + r->strs.size();
		}
		ok = fclose(f) == 0 && ok;
	}
	if (ok) ok = MoveFileExA(tmp.c_str(), FilePath().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
	else DeleteFileA(tmp.c_str());
	Log("tblcache: %s %zu table records (%.1f MB) in %.0f ms", ok ? "wrote" : "could not write", all.size(), bytes / 1048576.0,
	    Ms(t0, Now()));
	g_writing = false;
	return 0;
}

// ---- the hooks ----
// guarded reads only (MSVC: no unwinding objects next to __try)
bool ReadTable(void* table, char* name, char* dir, size_t n, int* ncols, int* stride) {
	__try {
		const char* nm = *(const char**)((uintptr_t)table + 0x28);
		const char* dr = *(const char**)((uintptr_t)table + 0x30);
		const int* lay = *(const int**)((uintptr_t)table + 0x40);
		if (!nm || !*nm || !lay) return false;
		*ncols = lay[0];
		*stride = lay[4];
		size_t i = 0;
		for (; nm[i] && i + 1 < n; i++) name[i] = nm[i];
		name[i] = 0;
		i = 0;
		for (; dr && dr[i] && i + 1 < n; i++) dir[i] = dr[i];
		dir[i] = 0;
		return *stride > 0 && *ncols > 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// the file the serializer opens (FUN_180d1a01c: "Libs\\Tables\\" + dir + name + ".xml"), written as xmlahead writes paths
bool KeyOf(void* table, std::string* key, int* ncols, int* stride) {
	char name[200], dir[200];
	if (!ReadTable(table, name, dir, sizeof name, ncols, stride)) return false;
	std::string p = "libs/tables/";
	if (dir[0]) { p += dir; p += '/'; }
	p += name;
	p += ".xml";
	key->clear();
	for (char ch : p) {
		ch = ch == '\\' ? '/' : (char)tolower((unsigned char)ch);
		if (ch == '/' && !key->empty() && key->back() == '/') continue;
		*key += ch;
	}
	return true;
}

void* GameAlloc(size_t size) {
	if (*(volatile uint64_t*)(g_base + kRvaAllocReady) == 0) ((void(__fastcall*)())(g_base + kRvaAllocInit))();
	int charged = 0;
	void* p = (*(AllocFn*)(g_base + kRvaAllocFn))(size, &charged, 0);
	_InterlockedExchangeAdd((volatile long*)(g_base + kRvaStatCharged), charged);
	_InterlockedExchangeAdd((volatile long*)(g_base + kRvaStatBytes), (long)size);
	_InterlockedExchangeAdd((volatile long*)(g_base + kRvaStatCount), 1);
	return p;
}

void __fastcall HkStore(void* table, void* rows, int rowCount, void* strings, int strBytes, int strCount, int version) {
	Ctx* c = t_ctx;
	if (c && c->table == table && !c->got && rowCount >= 0 && strBytes >= 0) {
		// before the original: it turns the rows' string offsets into pointers in place
		c->got = true;
		Rec& r = *c->rec;
		r.rowCount = rowCount; r.strBytes = strBytes; r.strCount = strCount; r.version = version;
		r.rows.assign((const uint8_t*)rows, (const uint8_t*)rows + (size_t)rowCount * c->stride);
		if (strBytes && strings) r.strs.assign((const uint8_t*)strings, (const uint8_t*)strings + strBytes);
	}
	g_origStore(table, rows, rowCount, strings, strBytes, strCount, version);
}

char __fastcall HkXmlLoad(void* ser, void* table) {
	static std::atomic<bool> waited{false};
	if (g_done.load()) return g_origLoad(ser, table);
	if (!waited.load()) { WaitForSingleObject(g_ready, 5000); waited = true; }
	const long long t0 = Now();
	const uint8_t serFlag = *(uint8_t*)((uintptr_t)ser + 0x10);
	std::string key;
	int ncols = 0, stride = 0;
	if (!KeyOf(table, &key, &ncols, &stride)) return g_origLoad(ser, table);
	std::shared_ptr<Rec> hit;
	{
		std::lock_guard<std::mutex> l(g_mu);
		auto it = g_recs.find(key);
		if (it != g_recs.end() && it->second->ncols == ncols && it->second->stride == stride && it->second->serFlag == serFlag)
			hit = it->second;
	}
	if (hit && g_mode == 1) {
		void* rows = GameAlloc(hit->rows.size());
		if (rows) {
			if (!hit->rows.empty()) memcpy(rows, hit->rows.data(), hit->rows.size());
			void* strs = nullptr;
			if (hit->strBytes > 0) {
				strs = GameAlloc((size_t)hit->strBytes);
				if (strs) memcpy(strs, hit->strs.data(), hit->strs.size());
			}
			if (hit->strBytes <= 0 || strs) {
				g_origStore(table, rows, hit->rowCount, strs, hit->strBytes, hit->strCount, hit->version);
				g_hits++;
				g_hitTicks += Now() - t0;
				return 1;
			}
		}
		// out of memory: the normal load below (the game frees what it holds as usual)
	}
	Ctx c{ table, stride, false, std::make_shared<Rec>() };
	c.rec->serFlag = (uint8_t)serFlag; c.rec->ncols = ncols; c.rec->stride = stride;
	Ctx* prev = t_ctx;
	t_ctx = &c;
	const char r = g_origLoad(ser, table);
	t_ctx = prev;
	g_misses++;
	g_missTicks += Now() - t0;
	if (!r || !c.got) return r;
	if (hit) {   // check mode: the record must equal the XML's result
		g_checked++;
		const Rec& a = *hit;
		const Rec& b = *c.rec;
		if (a.rowCount != b.rowCount || a.strBytes != b.strBytes || a.strCount != b.strCount || a.version != b.version ||
		    a.rows != b.rows || a.strs != b.strs) {
			if (g_bad++ < 20)
				Log("tblcache: check: %s differs from its record (rows %d / %d, strings %d / %d bytes)", key.c_str(), a.rowCount, b.rowCount,
				    a.strBytes, b.strBytes);
		}
		return r;
	}
	{
		std::lock_guard<std::mutex> l(g_mu);
		g_recs[key] = c.rec;
	}
	g_new++;
	g_lastNew = Now();
	return r;
}

bool Hook(uintptr_t rva, const unsigned char* bytes, size_t n, void* hk, void** orig, const char* what) {
	void* t = (void*)(g_base + rva);
	if (memcmp(t, bytes, n)) { Log("tblcache: %s isn't where 1.5.6 has it - off", what); return false; }
	if (MH_CreateHook(t, hk, orig) != MH_OK || MH_EnableHook(t) != MH_OK) { Log("tblcache: hooking %s failed - off", what); return false; }
	return true;
}

}  // namespace

bool Tblcache_Replays(const char* path) {
	if (g_mode != 1 || g_done.load() || !g_ready || WaitForSingleObject(g_ready, 0) != WAIT_OBJECT_0) return false;
	std::string k;
	for (const char* p = path; p && *p; p++) k += *p == '\\' ? '/' : (char)tolower((unsigned char)*p);
	std::lock_guard<std::mutex> l(g_mu);
	return g_recs.count(k) != 0;
}

void Tblcache_Install(uintptr_t base, void* self) {
	LARGE_INTEGER f;
	QueryPerformanceFrequency(&f);
	g_freq = f.QuadPart;
	g_base = base;
	char path[MAX_PATH] = {};
	GetModuleFileNameA((HMODULE)self, path, MAX_PATH);
	g_dir = path;
	g_dir = g_dir.substr(0, g_dir.find_last_of('\\') + 1);
	if (FILE* fp = fopen((g_dir + "fastload_tblcache.txt").c_str(), "r")) {
		int m = 1;
		if (fscanf(fp, "%d", &m) == 1) g_mode = m == 1 || m == 2 ? m : 0;
		fclose(fp);
	}
	if (!g_mode) { Log("tblcache: off (fastload_tblcache.txt 0)"); return; }
	if (!Hook(kRvaStore, kStoreBytes, sizeof kStoreBytes, (void*)&HkStore, (void**)&g_origStore, "the table row store")) return;
	if (!Hook(kRvaXmlLoad, kXmlLoadBytes, sizeof kXmlLoadBytes, (void*)&HkXmlLoad, (void**)&g_origLoad, "the XML table load")) return;
	g_ready = CreateEventA(nullptr, TRUE, FALSE, nullptr);
	HANDLE t = CreateThread(nullptr, 0, Preload, nullptr, 0, nullptr);
	if (t) CloseHandle(t);
	else SetEvent(g_ready);
	Log("tblcache: mode %d (%s; fastload_tblcache.txt)", g_mode,
	    g_mode == 1 ? "the XML tables replayed from fastload_tblcache.bin, recorded when it's missing or stale" :
	                  "check: every XML table loads as before and is compared with its record");
}

void Tblcache_Frame(int levelState) {
	if (!g_ready) return;
	if (!g_reported.exchange(true))
		Log("tblcache: at the first frame: %d tables replayed in %.0f ms, %d loaded from XML in %.0f ms (%d new records, %d checked, %d "
		    "differ)", g_hits.load(), Ms(0, g_hitTicks.load()), g_misses.load(), Ms(0, g_missTicks.load()), g_new.load(),
		    g_checked.load(), g_bad.load());
	static bool sawLoad = false;
	if (levelState >= 1) sawLoad = true;
	if (g_done.load() || levelState != 0 || g_writing.load()) return;
	// after the first level load the tables are all in: the records (tens of MB) go
	if (sawLoad && g_new.load() == 0) {
		g_done = true;
		std::lock_guard<std::mutex> l(g_mu);
		std::unordered_map<std::string, std::shared_ptr<Rec>> e;
		g_recs.swap(e);
		Log("tblcache: records freed after the first level load");
		return;
	}
	// new records are written once nothing has loaded for 5 s and no level load is running
	if (g_new.load() == 0 || Ms(g_lastNew.load(), Now()) < 5000) return;
	g_writing = true;
	g_new = 0;
	HANDLE t = CreateThread(nullptr, 0, Writer, nullptr, 0, nullptr);
	if (t) CloseHandle(t);
	else g_writing = false;
}
