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
// fastload_paklock.txt: 1 on (default), 0 the game's own FReadRaw. Both modes time the modify side's waits.
void Paklock_Install(uintptr_t gameBase, void* selfModule);
void Paklock_Report(const char* when);   // reads outside the lock, modify-side waits (all threads)
