#pragma once
#include <cstdint>
// CryPak's open-files lock held only for the slot copy, not for the whole read (boot 47 profile, docs/LOADTIME.md).
// CCryPak::FReadRaw (WHGame 1.5.6 FUN_1804610a4) takes the read side of m_csOpenFiles (CCryPak+0x10) and keeps it while
// CZipPseudoFile::FRead copies or inflates the data. FOpen / FClose take the modify side, which waits until no read is
// in flight: Main waited ~1.1 s of LoadEntities' 4.2 s for other threads' reads (streaming, the render loading thread).
// Here FReadRaw copies the handle's 0x18-byte slot (position, the CCachedFileData smart pointer, flags) under the read
// lock, holds its own reference to the cached data, reads outside the lock, then writes the position back under the read
// lock if the slot still holds the same file. The cached data has its own locks for the read (+0x38, GetData's inflate
// lock) and frees itself under its own lock (Release FUN_180460840). Files outside paks take the game's own path.
// Stored (uncompressed) entries: the game reads them from disk on every read (a seek + a CRT fread under the pak's mutex);
// compressed ones are unpacked once into memory. The octree's object file is read two small pieces per octree node
// (153,000 reads of ~1.4 KB in level loading, ~0.5 s of Main). With a window size, a read that continues where the last
// read of the same file ended fills a window from there with the game's own ReadFile call, and the reads inside it are
// copies. At most 64 windows (least recently used goes first); a file's window is dropped with the file (its destructor).
// Reading a whole stored entry instead (tried, boot 51) read 605 MB of sound banks at the menu and kept them: never.
// Windows apply only to entries over 16 MB (Trosecko: terrain/terrain.dat 181 MB in level-pak terrain.pak, 130,000 reads
// of ~1.4 KB, terrain/hlods.dat 317 MB in level.pak): on small entries each fill was one more cold read (boots 141, 143).
// Level warm (boots 142-143): those reads wait on the disk, not on the calls: with or without windows, Main's reads of
// terrain.dat cost 0.6-0.8 s when the file is not in the Windows file cache (cold read 181 MB 0.4 s, cached 44 ms), and
// the cache does not keep it from one start to the next (game start-up repurposes it). So at LoadLevel's start (levelprof)
// a thread reads, through the file cache, the 1 MB pieces of stored pak entries that the last load of this level read on
// its thread (fastload_levelwarm_<level>.txt next to the plugin, first-read order, pieces that cost Main 0.2 ms or 8 reads,
// at most 512 MB), and every load rewrites that list. No memory is held: the pieces sit in the file cache (standby).
// fastload_paklock.txt: `<mode> <window KB> <warm>`: mode 1 on (default), 0 the game's own FReadRaw (both modes time the
// modify side's waits); window KB (16-1024), 0 = stored entries read the game's way (default), counted by size; warm 1 on
// (default), 0 off.
void Paklock_Install(uintptr_t gameBase, void* selfModule);
void Paklock_LevelStart(const char* level);   // Main, as CLevelSystem::LoadLevel begins: the level warm starts
void Paklock_LevelEnd();                      // Main, as it ends: the warm's result, the list for the next load
void Paklock_Report(const char* when);   // reads outside the lock, modify-side waits (all threads)
