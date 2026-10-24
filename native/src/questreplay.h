#pragma once
#include <cstdint>
// Quest build step 4 (re_quest_replay_spec.md, 2026-10-24): phase 2a (the node build, ~4.2 s of the menu build) replayed
// from a record. A record build runs the game's deserializer and writes, per project, what it did as a compact op stream:
// which property of which element got which attribute string, which object type or definition was created, sequence
// items, pending Edge / Constant / Asset entries, definition paths, C_Objective line numbers. A replay build walks that
// stream and calls the same leaf functions of the game in the same order (object creation, value conversion, set_value,
// sequence views, OnBefore/OnAfterDeserialization, the pending-entry handler), without the XML walk, metadata lookups,
// child searches and consumed lists around them. Same calls in the same order = same objects, same resolver ids.
// Attribute values go through the game's own converter (FUN_1804fb74c) with a stand-in XML node that returns the
// recorded string; the pending-entry handler gets one too. Every element checks its type against the record; any
// difference fails the element the way the game does (the build then counts as failed).
// Only on the quest job's thread inside phase 2a (loadwatch marks it). Checked with questdump (questbench N recdump).
void Questreplay_Install(uintptr_t gameBase);   // hooks created; enabled only while armed
void Questreplay_Phase2a(bool enter);           // loadwatch, around FUN_18051ab28 (any thread)
bool Questreplay_Arm(int mode);                 // 1 = record the next build, 2 = replay the record; false = cannot
void Questreplay_Disarm();                      // after the build: logs what happened
bool Questreplay_HaveRecord();
bool Questreplay_LastFailed();                  // the last replay build failed somewhere
