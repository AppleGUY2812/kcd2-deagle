#pragma once
#include <cstdint>
// Reading the load's streaming requests ahead (2026-10-23 decompile: the terrain step's mesh loaders, 14,153 CGF
// headers, and step 5's stream burst are read one request at a time by the "Streaming File IO HDD" thread, unbuffered,
// in 128 KB pieces, through ZipDir ReadFileStreaming FUN_180464928, which streamio.cpp hooks; the game seeks before
// every read, so a request served from memory can't move the next one).
// 1. Recording (mode 1 and 2): every unbuffered stream request (pak, entry data offset, offset, size) until gameplay
//    first starts goes to fastload_streamahead_trace.csv next to the plugin, and 5 s after gameplay first starts the
//    requests so far, first occurrence only, in time order, go to fastload_streamahead_list.txt for the next start.
// 2. Serving (mode 2, with a list): 4 threads read the list's ranges from the paks at start (their own unbuffered
//    handles, low I/O priority until the load asks for listed data; neighbouring requests merged into reads of up to
//    4 MB), at most 512 MB held, free RAM by ramgov.h (copies far ahead are freed and read again when RAM is short). The hook copies an exact match (same pak path, file size
//    and write time as recorded, same data offset, offset and size) into the game's buffer; anything else, or not read
//    yet, goes the game's own way. The first 100 copies are checked against the game's own read: one mismatch turns
//    serving off for the session. What the load never asked for is freed 20 s after gameplay starts.
// Mode in fastload_streamahead.txt next to the plugin: 0 off, 1 record (default), 2 record + serve.
using StreamReadFn = uint32_t(__fastcall*)(uint8_t* cache, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size);
void Streamahead_Install(void* selfModule);   // from Streamio_Install, once its hook is on
// streamio's hook, unbuffered branch. -1 = not served (the caller reads); otherwise the game's return value (0 = out
// holds the bytes). `want` is the byte count (size, or the entry's packed size when size is 0).
int Streamahead_Serve(uint8_t* cache, uint8_t* entry, uint8_t* out, int64_t offset, int64_t size, int64_t want,
                      StreamReadFn orig);
void Streamahead_Record(uint8_t* cache, uint8_t* entry, int64_t offset, int64_t want, long long ticks);
void Streamahead_Stats(long long* served, long long* gameFirst, long long* notListed);
void Streamahead_OnLoaded();                  // gameplay started (fastload_main.cpp: the level state back to 0)
// A save-load / fast-travel screen waits (readiness.cpp, every frame). While it does, the readers stop (the disk is the
// game's) and a copy handed to the game is kept, up to 256 MB, for when it asks the same bytes again; the kept copies
// are freed when the screen opens. Boots 34-36: ~4,000 such repeats per Settle, while reading ahead in the gate hurt.
void Streamahead_SetGate(bool open);
long long Streamahead_Ready();     // copies held now (tests)
long long Streamahead_KeptServed();   // repeats handed over from kept copies (tests)
long long Streamahead_HeldBytes();   // bytes held now (tests)
void Streamahead_Command(const char* args);
