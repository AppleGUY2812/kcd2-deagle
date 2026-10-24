// Outside-the-game test of src/streamahead.cpp: record -> list -> read ahead -> serve, on a fake 32 MB pak.
// usage (run in this order from one folder; every file lands next to the exe):
//   streamahead_test record    records 700 requests, "gameplay starts", the list must appear 5 s later
//   streamahead_test serve     reads the list ahead and serves the same requests: every byte checked, most served
//   streamahead_test shed      RAM gets short after the read-ahead and stays short: every copy is freed, the game reads them itself
//   streamahead_test revive    copies RAM shedding dropped for good are read again once a loading screen waits
//   streamahead_test changed   the pak's write time changed since the list: nothing may be served
//   streamahead_test gate      RAM short the whole time, but a loading screen waits, requests in a shuffled order: most served,
//                              read by up to 16 readers at once ("most reads in flight" in the status line)
// Exit code 0 = pass.
#include "../src/streamahead.h"
#include "../src/ramgov.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void Log(const char* fmt, ...) {
	va_list a;
	va_start(a, fmt);
	printf("  log: ");
	vprintf(fmt, a);
	printf("\n");
	va_end(a);
	fflush(stdout);
}

namespace {
constexpr int64_t kPak = 32ll << 20;
uint8_t Byte(int64_t i) { return (uint8_t)(((uint64_t)i * 2654435761ull) >> 13); }
HANDLE g_buffered = INVALID_HANDLE_VALUE;   // the "game's" own reads in this test

uint32_t __fastcall GameRead(uint8_t*, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size) {
	LARGE_INTEGER li;
	li.QuadPart = offset + *(uint32_t*)(entry + 0xc);
	SetFilePointerEx(g_buffered, li, nullptr, FILE_BEGIN);
	DWORD got = 0;
	return ReadFile(g_buffered, out, (DWORD)size, &got, nullptr) && got == (DWORD)size ? 0 : 1;
}

struct Req { uint32_t dataOff; int64_t offset, size; };
std::vector<Req> Requests() {
	std::vector<Req> v;
	uint64_t s = 12345;
	auto rnd = [&](uint64_t n) { s = s * 6364136223846793005ull + 1442695040888963407ull; return (s >> 33) % n; };
	for (int i = 0; i < 400; i++) {   // scattered
		const int64_t size = 1 + (int64_t)rnd(300 << 10);
		const uint32_t dataOff = (uint32_t)rnd(kPak - size - (64 << 10));
		v.push_back({ dataOff, (int64_t)rnd(64 << 10), size });
	}
	int64_t at = 20ll << 20;            // a run of neighbours: merged reads
	for (int i = 0; i < 250; i++) {
		const int64_t size = 1 + (int64_t)rnd(40 << 10);
		v.push_back({ (uint32_t)at, 0, size });
		at += size + (int64_t)rnd(8 << 10);
	}
	for (int i = 0; i < 3; i++) v.push_back({ (uint32_t)(i * (5 << 20)), 0, 5 << 20 });   // bigger than one 4 MB read
	for (int i = 0; i < 47; i++) v.push_back(v[i * 7]);   // asked again: the list keeps the first only
	return v;
}

std::string Dir() {
	char p[MAX_PATH] = {};
	GetModuleFileNameA(nullptr, p, MAX_PATH);
	std::string d = p;
	return d.substr(0, d.find_last_of('\\') + 1);
}
}  // namespace

int main(int argc, char** argv) {
	const std::string mode = argc > 1 ? argv[1] : "", dir = Dir(), pak = dir + "fake.pak";
	if (mode == "record") {
		std::vector<uint8_t> data((size_t)kPak);
		for (int64_t i = 0; i < kPak; i++) data[(size_t)i] = Byte(i);
		FILE* f = fopen(pak.c_str(), "wb");
		fwrite(data.data(), 1, data.size(), f);
		fclose(f);
		DeleteFileA((dir + "deagle_streamahead_list.txt").c_str());
		if (FILE* m = fopen((dir + "deagle_streamahead.txt").c_str(), "w")) { fprintf(m, "2\n"); fclose(m); }
	}
	if (mode == "changed") {   // touch the pak: same bytes, new write time
		HANDLE t = CreateFileA(pak.c_str(), FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
		FILETIME ft; GetSystemTimeAsFileTime(&ft);
		SetFileTime(t, nullptr, nullptr, &ft);
		CloseHandle(t);
	}
	// the game opens paks unbuffered; the cache object keeps that handle at +0x10
	const HANDLE h = CreateFileA(pak.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING, nullptr);
	g_buffered = CreateFileA(pak.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	if (h == INVALID_HANDLE_VALUE || g_buffered == INVALID_HANDLE_VALUE) { printf("FAIL: no fake pak (run record first)\n"); return 1; }
	alignas(16) uint8_t cache[0x1000] = {};
	*(HANDLE*)(cache + 0x10) = h;
	// this PC's free RAM doesn't decide the test; "gate": below the take level all along, so only the gate window reads
	if (mode == "gate") ramgov::SetForTest(1ll << 40, 1ll << 40), Streamahead_SetGate(true);
	else ramgov::SetForTest(0, -1);
	Streamahead_Install(nullptr);
	const std::vector<Req> reqs = Requests();
	if (mode != "record") Sleep(3000);   // the reader reads the list meanwhile (the main menu)
	if (mode == "revive") {      // freed for RAM, read again, freed again (= dropped for good), then a loading screen
		long long sv = 0, gf = 0, nl = 0;
		auto ready = [&] { Streamahead_Stats(&sv, &gf, &nl); (void)sv; return Streamahead_Ready(); };
		ramgov::SetForTest(1ll << 40, 1ll << 40);
		for (int i = 0; i < 100 && ready() > 0; i++) Sleep(100);   // freed once (the writer sheds once a second)
		ramgov::SetForTest(0, -1);
		for (int i = 0; i < 200 && ready() < 600; i++) Sleep(100);  // read again
		ramgov::SetForTest(1ll << 40, 1ll << 40);
		for (int i = 0; i < 100 && ready() > 0; i++) Sleep(100);   // freed a second time: dropped for good
		Streamahead_SetGate(true);   // RAM still short: only the loading-screen rule reads them again
		for (int i = 0; i < 200 && ready() < 600; i++) Sleep(100);
	}
	if (mode == "shed") {        // RAM short: everything held is freed (the writer checks once a second), and while it
		ramgov::SetForTest(1ll << 40, 1ll << 40);   // stays short nothing is read again (it is once there is room: revive)
		Sleep(2500);
	}
	std::vector<uint8_t> out;
	long long served = 0, bad = 0, fell = 0;
	std::vector<Req> order = reqs;
	if (mode == "gate") {   // another order than the list's (boot 33): far jumps both ways
		uint64_t r = 99;
		for (size_t i = order.size(); i > 1; i--) { r = r * 6364136223846793005ull + 1; std::swap(order[i - 1], order[(r >> 33) % i]); }
	}
	for (const Req& q : order) {
		uint8_t entry[0x20] = {};
		*(uint32_t*)(entry + 0xc) = q.dataOff;
		out.assign((size_t)q.size + 16, 0xcc);
		const int r = Streamahead_Serve(cache, entry, out.data(), q.offset, q.size, q.size, GameRead);
		if (r < 0) { fell++; if (GameRead(cache, entry, out.data(), q.offset, q.size)) { printf("FAIL: game read failed\n"); return 1; } }
		else if (r == 0) served++;
		const int64_t abs = q.offset + q.dataOff;
		for (int64_t i = 0; i < q.size; i++) if (out[(size_t)i] != Byte(abs + i)) { bad++; break; }
		for (int i = 0; i < 16; i++) if (out[(size_t)q.size + i] != 0xcc) { bad++; break; }   // nothing past the end
		Streamahead_Record(cache, entry, q.offset, q.size, 1000);
		if (mode == "gate") Sleep(2);   // the game's own pace, so the window can stay ahead
	}
	{   // a request that isn't listed
		uint8_t entry[0x20] = {};
		*(uint32_t*)(entry + 0xc) = 7;
		out.assign(100, 0);
		if (Streamahead_Serve(cache, entry, out.data(), 3, 99, 99, GameRead) >= 0) { printf("FAIL: served a request that isn't listed\n"); return 1; }
	}
	printf("%s: %zu requests, %lld from the read-ahead (incl. the self-check), %lld the game's own way, %lld wrong\n",
	       mode.c_str(), reqs.size(), served, fell, bad);
	Streamahead_Command("");
	if (bad) { printf("FAIL: wrong bytes\n"); return 1; }
	if (mode == "record") {
		if (served) { printf("FAIL: served without a list\n"); return 1; }
		Streamahead_OnLoaded();
		Sleep(6500);
		FILE* l = fopen((dir + "deagle_streamahead_list.txt").c_str(), "r");
		if (!l) { printf("FAIL: no list 5 s after gameplay started\n"); return 1; }
		int lines = 0; char b[1500];
		while (fgets(b, sizeof b, l)) if (b[0] != '#') lines++;
		fclose(l);
		printf("list: %d requests (want %zu: the repeats dropped)\n", lines, reqs.size() - 47);
		if (lines != (int)reqs.size() - 47) { printf("FAIL: list size\n"); return 1; }
	}
	if (mode == "serve" && served < (long long)(reqs.size() - 47) * 9 / 10) { printf("FAIL: too few served\n"); return 1; }
	if (mode == "gate" && served < (long long)(reqs.size() - 47) * 7 / 10) { printf("FAIL: the gate window served too few\n"); return 1; }
	if (mode == "revive" && (served < (long long)(reqs.size() - 47) * 7 / 10 || Streamahead_Revived() < 600)) { printf("FAIL: dropped copies weren't read again\n"); return 1; }
	if (mode == "shed" && served) { printf("FAIL: served after everything was freed\n"); return 1; }
	if (mode == "changed" && served) { printf("FAIL: served from a pak that changed since the list\n"); return 1; }
	printf("PASS %s\n", mode.c_str());
	return 0;
}
