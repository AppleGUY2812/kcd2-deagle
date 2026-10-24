#pragma once
// Sampling profile of one thread (questbench N filesample | sample; levelprof's level load). A watcher thread suspends the
// sampled thread about every millisecond, copies its registers and the top 128 KB of its stack, resumes it, then unwinds the
// copy with the image's own unwind data (RtlLookupFunctionEntry / RtlVirtualUnwind): a full call stack per sample, every
// function named by its start (WHGame as its Ghidra address 0x18xxxxxxx). Each sample also carries a phase (the quest
// phase loadwatch is in, or the caller's own phase function).
// The report logs, per phase, the functions with the most samples on top of the stack (self) and anywhere on it
// (inclusive), and appends every sample to a CSV next to the plugin: fastload_qsample.csv (tag,phase,frame;frame;...
// innermost first) or the caller's file with the time (tag,phase,ms since the start,frames).
void Questsample_Begin();                  // samples the calling thread until End (quest phases)
void Questsample_End(const char* tag);
// Samples the calling thread for stopAfterMs (then the sampler reports by itself, on its own thread), with phase() read at
// each sample and phaseName() for the report. false = a profile already runs.
bool Questsample_BeginEx(int (*phase)(), const char* (*phaseName)(int), unsigned stopAfterMs, const char* tag, const char* csv);
