#pragma once
#include <cstdint>
// Faster pak unpacking (user 2026-10-23: "loading a save when u boot up the game is always slow").
// A boot's level load runs mostly on the main thread, and ~45-63% of that thread's time in the slow steps is zlib
// 1.2.11 unpacking pak entries plus their CRC32 (docs/PERFORMANCE.md, tools/threadsample.ps1). This swaps both for
// libdeflate (native/third_party/libdeflate, v1.26, MIT): measured on this PC's paks 1.7-2.35x faster unpacking and
// ~4x faster CRC32 than zlib 1.3.1.
//   unpack: FUN_1807675a8(src, dst, srcSize, dstSize, &outSize, &result, heap), the whole-entry raw-deflate unpack
//           (CryEngine's ZipRawUncompress shape). Overlapping buffers (in-place unpack) and anything libdeflate does
//           not finish cleanly go to the game's own code, so the game sees zlib's exact result.
//   crc32:  FUN_180612300(crc, buf, len), zlib's crc32 (same maths as libdeflate_crc32).
// Mode, read at start from deagle_fastzip.txt next to the plugin and set live by "[DN] fastzip N":
//   0 the game's zlib (still timed), 1 libdeflate (default), 2 verify: both run on every call, the game gets
//   zlib's result, every mismatch is logged, and the log shows both libraries' times on the same data.
// Every 2 s with activity the log gets one "fastzip:" line with the counts and milliseconds so far.
void Fastzip_Install(uintptr_t gameBase, void* selfModule);   // right after MH_Initialize, before the menu
void Fastzip_Command(const char* args);
