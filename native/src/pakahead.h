#pragma once
#include <cstdint>
// Unpacking ahead (user 2026-10-23: "start unpacking as soon as kcd boots up on the main menu"). The level load reads
// and unpacks pak entries one by one on the main thread; the other threads sit idle, and so does the whole PC while
// the main menu is up. Plan: (1) record which entries are read, by which thread, in what order (this file, now);
// (2) on later starts, read + unpack that list on idle threads from the main menu on, and hand the finished bytes
// to the game when it asks.
// Hook: ZipDir::Cache::ReadFile, FUN_1804617fc(cache, FileEntry*, compressed buf, uncompressed buf, decompress,
// part offset, part size, skip crc). FileEntry: +0x0 crc32, +0x4 packed size, +0x8 size, +0xC data offset in the pak,
// +0x18 u16 method. cache+8 is the pak's CZipFile: [0] FILE* (the game's ucrtbase), [7] non-zero = pak held in memory.
// Mode, read at start from deagle_pakahead.txt next to the plugin: 0 off, 1 record (default) to
// deagle_pakahead_trace.csv next to the plugin, the first 10 minutes after start only; 2 record + unpack ahead: 6
// background workers read + unpack the entries of deagle_pakahead_list.txt (tools/pakahead_list.py makes it from a
// recording) from game start, at most 2 GB held, and the hook copies a finished entry into the game's buffer. Served
// only on an exact match: same pak file, data offset, packed and unpacked size and crc32 (the list's crc is checked
// again after the unpack). Anything else, or not finished yet, goes the game's normal way. Freed after 15 minutes.
void Pakahead_Install(uintptr_t gameBase, void* selfModule);   // right after MH_Initialize
void Pakahead_Command(const char* args);
