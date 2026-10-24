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
// Mode, deagle_xmlahead.txt next to the plugin: 0 off, 1 record (default): deagle_xmlahead_trace.csv (time, thread,
// file, result, microseconds), first 10 minutes; 2 record + parse ahead: the files of deagle_xmlahead_list.txt
// (tools/xmlahead_list.py) are parsed by 4 workers from the first request for a "levels/" file on (the level is
// loading, so every mod pak is mounted: a mod's version of a file is what both the game and the workers get).
// Served once per file, on the first request, by exact path (lower case, '/'); anything else goes the normal way.
// Trees nobody asked for are released after 15 minutes.
void Xmlahead_Install(uintptr_t gameBase, void* selfModule);   // right after MH_Initialize
void Xmlahead_Command(const char* args);
