#pragma once
// Sampling profile of a quest build thread (questbench N filesample | sample). A watcher thread suspends the build thread
// about every millisecond, copies its registers and the top 128 KB of its stack, resumes it, then unwinds the copy with the
// image's own unwind data (RtlLookupFunctionEntry / RtlVirtualUnwind): a full call stack per sample, every function
// named by its start (WHGame as its Ghidra address 0x18xxxxxxx). Each sample also carries the quest phase loadwatch is in.
// End logs, per phase, the functions with the most samples on top of the stack (self) and anywhere on it (inclusive),
// and appends every sample to deagle_qsample.csv next to the plugin (tag,phase,frame;frame;... innermost first).
void Questsample_Begin();                  // samples the calling thread until End
void Questsample_End(const char* tag);
