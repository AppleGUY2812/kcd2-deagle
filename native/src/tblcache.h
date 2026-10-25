#pragma once
#include <cstdint>
// The object database's XML tables recorded once and replayed (launch -> menu work, 2026-10-24). At start-up Main
// spends ~3 s in the database (boot 95, plugin +10.1 -> 13.6 s). For the ~1,300 plain tables most of it is the XML
// serializer's row building (C_XMLTableSerializer load FUN_180d1a11c: attribute lookups, sscanf, a string pool), and
// its whole result is two position-independent blobs the serializer hands to FUN_180d19f38 (rows with string offsets,
// and the strings) plus counts and the table version. The first boot records those bytes per table file
// (fastload_tblcache.bin, keyed like questreplay: WHGame.dll, every .pak in Data, Localization, Mods/*, the Workshop mods,
// the manifests and mod_order.txt); later boots skip the XML and the row building and hand the same bytes to the same
// FUN_180d19f38. Patch files (item__*.xml) go through the same load and are recorded the same way.
// fastload_tblcache.txt: 1 = record / replay (default), 2 = check (every table loads from XML as before and is compared
// with the record; mismatches logged), 0 = off.
void Tblcache_Install(uintptr_t gameBase, void* self);
void Tblcache_Frame(int levelState);   // main thread, each frame: writes new records once loading has been quiet a while
// true when the game will take this file's table from the record instead of reading the XML (path as xmlahead writes it:
// "libs/tables/rpg/buff.xml"); false before the record is read or when it is stale. For xmlahead: no parse ahead.
bool Tblcache_Replays(const char* path);
