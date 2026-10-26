#pragma once
#include <cstdint>
// Baked archive indexes (user 2026-10-25: "we do what it takes to change their game"; load-in-place, step 1).
// Every pak the game opens gets a ZipDir::Cache: one block of [0x9a8 header][file tree][pak path]. The tree is built
// from the zip central directory on each launch (FUN_1804d69b8 reads the end record and the directory with fread_s,
// FUN_1808b1fe0 / FUN_1808b21a4 parse it into a folder tree or a CRC-sorted entry array, FUN_1804d61b8 packs it).
// The packed tree uses relative offsets only (FUN_1804d61b8: names and sub-folders as offsets from the folder), so its
// bytes can be saved and copied back as they are.
// Hook: FUN_1804d5dd4 (CacheFactory::MakeCache(factory, Cache** out, const char* path)).
//   miss: the game builds the cache; we save the tree, the header's non-pointer fields (0x78..0x990, 0x9a0..0x9a8) and
//         the pak's size + last-write time to fastload_zipidx.bin next to the plugin (written when misses stop for 3 s).
//   hit:  same pak size + time + flags: allocate the block through the game's allocator, run its own header set-up
//         (FUN_1804d67e8), copy the saved bytes in, close the factory (FUN_1804d63b0). No directory read, no parse.
// Mode, fastload_zipidx.txt: 0 time only (default), 1 bake + use, 2 verify (the game builds every cache; a hit's
// saved bytes are compared with the game's and every difference logged; the game keeps its own).
void Zipidx_Install(uintptr_t gameBase, void* selfModule);   // as early as possible: DllMain, again from Init
void Zipidx_Report(const char* when);
