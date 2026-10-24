// Outside-the-game benchmark for src/fastzip.cpp: libdeflate, built exactly as in the plugin (MSVC), unpacking the
// raw-deflate entries of a KCD2 pak and checking each entry's zip CRC32. Prints MB/s of unpacked data.
// usage: zbench.exe <file.pak> [MB of unpacked data to use, default 300]
#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <vector>
#include "libdeflate.h"

struct Entry { std::vector<uint8_t> packed; uint32_t size, crc; };

static double Now() { LARGE_INTEGER t, f; QueryPerformanceCounter(&t); QueryPerformanceFrequency(&f); return (double)t.QuadPart / f.QuadPart; }

int main(int argc, char** argv) {
	if (argc < 2) { printf("usage: zbench <file.pak> [MB]\n"); return 1; }
	const double limit = (argc > 2 ? atof(argv[2]) : 300) * 1e6;
	FILE* f = fopen(argv[1], "rb");
	if (!f) { printf("cannot open %s\n", argv[1]); return 1; }
	_fseeki64(f, 0, SEEK_END); const int64_t len = _ftelli64(f);
	// end of central directory: last 64 KB + 22
	const int64_t tail = len < 65557 ? len : 65557;
	std::vector<uint8_t> t(tail); _fseeki64(f, len - tail, SEEK_SET); fread(t.data(), 1, tail, f);
	int64_t eocd = -1;
	for (int64_t i = tail - 22; i >= 0; i--) if (!memcmp(&t[i], "PK\x05\x06", 4)) { eocd = i; break; }
	if (eocd < 0) { printf("no zip directory\n"); return 1; }
	uint32_t cdSize, cdOff; uint16_t n;
	memcpy(&n, &t[eocd + 10], 2); memcpy(&cdSize, &t[eocd + 12], 4); memcpy(&cdOff, &t[eocd + 16], 4);
	std::vector<uint8_t> cd(cdSize); _fseeki64(f, cdOff, SEEK_SET); fread(cd.data(), 1, cdSize, f);
	std::vector<Entry> es; double total = 0, packed = 0;
	for (size_t p = 0, k = 0; k < n && p + 46 <= cd.size() && total < limit; k++) {
		uint16_t method, nl, xl, cl; uint32_t crc, cs, us, lho;
		memcpy(&method, &cd[p + 10], 2); memcpy(&crc, &cd[p + 16], 4); memcpy(&cs, &cd[p + 20], 4); memcpy(&us, &cd[p + 24], 4);
		memcpy(&nl, &cd[p + 28], 2); memcpy(&xl, &cd[p + 30], 2); memcpy(&cl, &cd[p + 32], 2); memcpy(&lho, &cd[p + 42], 4);
		p += 46 + nl + xl + cl;
		if (method != 8) continue;
		uint8_t lh[30]; _fseeki64(f, lho, SEEK_SET); fread(lh, 1, 30, f);
		uint16_t lnl, lxl; memcpy(&lnl, lh + 26, 2); memcpy(&lxl, lh + 28, 2);
		Entry e; e.size = us; e.crc = crc; e.packed.resize(cs);
		_fseeki64(f, (int64_t)lho + 30 + lnl + lxl, SEEK_SET); fread(e.packed.data(), 1, cs, f);
		total += us; packed += cs; es.push_back(std::move(e));
	}
	fclose(f);
	printf("%zu entries, %.0f MB packed -> %.0f MB\n", es.size(), packed / 1e6, total / 1e6);
	libdeflate_decompressor* d = libdeflate_alloc_decompressor();
	std::vector<uint8_t> out;
	double best = 1e9, bestCrc = 1e9; int bad = 0;
	for (int rep = 0; rep < 3; rep++) {
		double un = 0, cr = 0;
		for (auto& e : es) {
			if (out.size() < e.size) out.resize(e.size);
			size_t got = 0;
			const double a = Now();
			const auto r = libdeflate_deflate_decompress(d, e.packed.data(), e.packed.size(), out.data(), e.size, &got);
			const double b = Now();
			const uint32_t c = libdeflate_crc32(0, out.data(), got);
			const double cc = Now();
			un += b - a; cr += cc - b;
			if (rep == 0 && (r != LIBDEFLATE_SUCCESS || got != e.size || c != e.crc)) bad++;
		}
		if (un < best) best = un;
		if (cr < bestCrc) bestCrc = cr;
	}
	printf("unpack %.3f s  %.0f MB/s   crc32 %.3f s  %.0f MB/s   bad entries %d\n", best, total / 1e6 / best, bestCrc,
	       total / 1e6 / bestCrc, bad);
	return bad ? 2 : 0;
}
