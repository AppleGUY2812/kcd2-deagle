#pragma once
#include <cstdint>
// Parsing XML ahead (user 2026-10-23: "why cant we split across cores" / "go"). The 2026-10-23 boot sampler with
// thread names: during the terrain step only Main, JobWorker_03 and RenderLoadingThread are busy while 8 job workers
// idle; Main and JobWorker_03 spend ~1.2-1.8 s each per load step parsing XML (the parser family at 0x1806e5000-
// 0x1806eb000, "CDATA"/"IDREF"), one file after another (~3,300 files per load).
// Hook: FUN_180979360(self, XmlNodeRef* ret, const char* file, ?, char reportErrors): makes an XmlParserReadOnlyImp on
// the stack, FUN_180463d38 reads the file through CryPak and parses it (text or binary XML), returns the tree.
// Self-contained, so idle threads can call it ahead of time; XmlNodeRef = IXmlNode*, vtable +0x18 AddRef, +0x10
// Release. (XmlParser::ParseFile, FUN_18247cecc, the path with a caller-owned parser, is only counted.)
// Mode, fastload_xmlahead.txt next to the plugin: 0 off, 1 record (default): fastload_xmlahead_trace.csv (time, thread,
// file, result, microseconds), first 10 minutes; 2 record + parse ahead: the files of fastload_xmlahead_list.txt
// (tools/xmlahead_list.py, from a recording) are parsed ahead: files outside levels/ by 4 workers once the main menu
// is idle (1,500+ XML loads done, then none for 3 s: every mod pak is mounted by then), levels/ files by 3 workers
// from the first request for one (the level paks mount with the load). Workers stay at most 3,000 list files ahead of
// the furthest file the game has asked for, so what they hold ahead is bounded; they pause below 1 GB free RAM.
// Served once per file, on the first request, by exact path (lower case, '/'); anything else goes the normal way. A
// request for a file a worker is parsing right now waits for it (up to 100 ms, then the game parses it itself).
// 2026-10-23 22:00 boot: the load makes 35,894 XML loads (9.1 s of thread time; the first list had 3,258 files, 48.4 s
// load, no gain).
// Trees nobody asked for are released after 15 minutes.
void Xmlahead_Install(uintptr_t gameBase, void* selfModule);   // right after MH_Initialize
void Xmlahead_Command(const char* args);
// The main menu has loaded and waits: 1,500+ XML loads, none for quietMs, and no levels/ file asked for yet. Needs
// mode 1 or 2 (the hook counts the loads); false in mode 0.
bool Xmlahead_MenuQuiet(unsigned quietMs);
// A level load has begun (the game asked for a levels/ file). Needs mode 1 or 2; false in mode 0.
bool Xmlahead_LevelSeen();
// The first load reached gameplay: 20 s later (mode 2) the list for the next start is rewritten from this run's
// recording, so it follows changes to the load (fastload_xmlahead_list.txt).
void Xmlahead_OnLoaded();
// Start the menu workers now instead of at "main menu idle" (questmenu: the quest job at the menu loads XML, so the
// menu never looks idle). No effect once started or with no list.
void Xmlahead_StartMenuNow(const char* why);
// The quest build's quests/ files will not be asked for (questreplay skips its XML phase): not parsed ahead, no boost.
// quests/ files listed after the level load starts (the save load's place graphs) are kept.
void Xmlahead_SkipQuests(const char* why);
