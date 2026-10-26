#pragma once
#include <cstdint>
// The heads' facial morph data parsed on a pool of threads instead of the stream engine's one callback thread (Settle
// work, 2026-10-26). Each head's joint-based facial animation (wh::animationmodule::JointPoseMorphData) is an XML file
// the stream engine reads; its completion (FUN_1806e3820, on the "Streaming AsyncCallback" thread) parses the buffer
// (FUN_1806e3ac4: the game's LoadXmlFromBuffer, then its reflection-based fill), frees the stream's buffer and puts the
// model into the cache under the cache's own lock (FUN_1806e3764, the mutex at cache+0x48). The heads' components look
// the cache up by name until it is there.
// Why: with buffered pak reads (sys_UncachedStreamReads 0, build 178) the save load's settle step is bound by that one
// thread, not the disk. Boots 178U1-4 + 179s, the first 1.87 s of the settle gate: the callback thread busy 79 %, more
// than half of it (191 of 351 samples) this parse; the reader asleep with work queued 78-87 % of the first second and
// the stream engine's temp memory (freed as each completion finishes) full 36-72 %; the gate's last must-have tasks
// (animations, clothing, objects, terrain) all finish at 1.6-1.8 s, then "let me in early" opens at 1.8-1.9 s.
// Without buffered reads (178a) the reader never slept. Expected: the callback thread's ~0.8 s of morph parsing moves to
// idle cores (the 8 job workers are ~70 % idle in the window); settle shorter by up to that if the callback thread is
// the chain (the boot pair decides).
// Pooled: on the callback thread the buffer and the file name are copied, then the game's own error path runs (the
// stream's buffer freed through its virtual +0x80, the stream released), so the stream engine's temp memory is back at
// once; a pool thread then makes the same calls the game makes after its parse (the parse, the shared pointer copy, the
// name as a CryString, the cache insert under the cache's lock, the releases). A failed stream, an empty buffer or a
// queue over 256 MB of copies runs the game's way. The settle gate's "let me in early" also waits for the pool to be
// empty (readiness.cpp), so the faces are in the cache at the same point as before.
// fastload_morphpool.txt next to the plugin: 0 timed only (the default: the game's way, the callback's time while a
// load screen waits logged), 1 pooled while a save-load / fast-travel load screen waits, 2 pooled always; a second
// number = pool threads (default 4, 1-16). "morphpool:" lines in the log.
void Morphpool_Install(uintptr_t gameBase, void* selfModule);
void Morphpool_SetGate(bool loadScreen);   // readiness.cpp, main thread, every frame: a load screen waits
int Morphpool_Pending();                   // files handed to the pool and not yet in the game's cache; any thread
