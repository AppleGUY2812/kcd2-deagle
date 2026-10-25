#pragma once
// Who holds the game's memory (a diagnostic, off by default: fastload_memsite.txt "1" next to the plugin, read at start).
// ntdll's NtAllocateVirtualMemory(Ex) / NtFreeVirtualMemory are hooked for this process: every commit is kept with the
// code that asked for it (the first 6 return addresses after ntdll / KernelBase / kernel32 / ucrtbase), every decommit /
// release takes it off. Heap growth is charged to the allocation that made the heap grow, so heap memory is spread over
// its callers roughly by how much they allocate. Nothing here allocates from a heap (the records live in their own pages,
// taken with the original function), so a thread that holds a heap lock and grows the heap can't deadlock against it.
// `memsite:` lines at the level load's start, when the save is applied (state 5), at state 6, at gameplay start and
// 20 s later: the process's memory (commit charge, working set; committed private / mapped / image by VirtualQuery),
// the tracked total, the modules and the 30 call sites holding the most RAM (pages in the working set, by
// QueryWorkingSetEx) with what they have committed; every site holding 1 MB or more goes to fastload_memsite_<n>.csv
// next to the plugin. Mapped views (NtMapViewOfSection) are not tracked, only totalled.
void Memsite_Install(void* self);   // after MH_Initialize
void Memsite_Command(const char* args);   // "memsite": a report now
