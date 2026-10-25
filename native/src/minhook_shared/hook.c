/*
 *  MinHook - The Minimalistic API Hooking Library for x64/x86
 *  Copyright (C) 2009-2017 Tsuda Kageyu.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *  TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *  PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER
 *  OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 *  EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 *  PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 *  PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 *  LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
 *  NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 *  SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <windows.h>
#include <tlhelp32.h>
#include <limits.h>

#include "MinHook.h"
#include "buffer.h"
#include "trampoline.h"
#include <stdio.h>

#ifndef ARRAYSIZE
    #define ARRAYSIZE(A) (sizeof(A)/sizeof((A)[0]))
#endif

// Initial capacity of the HOOK_ENTRY buffer.
#define INITIAL_HOOK_CAPACITY   32

// Initial capacity of the thread IDs buffer.
#define INITIAL_THREAD_CAPACITY 128

// Special hook position values.
#define INVALID_HOOK_POS UINT_MAX
#define ALL_HOOKS_POS    UINT_MAX

// Freeze() action argument defines.
#define ACTION_DISABLE      0
#define ACTION_ENABLE       1
#define ACTION_APPLY_QUEUED 2

// Thread access rights for suspending/resuming threads.
#define THREAD_ACCESS \
    (THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION | THREAD_SET_CONTEXT)

// Hook information.
typedef struct _HOOK_ENTRY
{
    LPVOID pTarget;             // Address of the target function.
    LPVOID pDetour;             // Address of the detour or relay function.
    LPVOID pTrampoline;         // Address of the trampoline function.
    UINT8  backup[8];           // Original prologue of the target function.

    UINT8  patchAbove  : 1;     // Uses the hot patch area.
    UINT8  isEnabled   : 1;     // Enabled.
    UINT8  queueEnable : 1;     // Queued for enabling/disabling when != isEnabled.

    UINT   nIP : 4;             // Count of the instruction boundaries.
    UINT8  oldIPs[8];           // Instruction boundaries of the target function.
    UINT8  newIPs[8];           // Instruction boundaries of the trampoline function.
} HOOK_ENTRY, *PHOOK_ENTRY;

// Suspended threads for Freeze()/Unfreeze().
typedef struct _FROZEN_THREADS
{
    LPDWORD pItems;         // Data heap
    UINT    capacity;       // Size of allocated data heap, items
    UINT    size;           // Actual number of data items
    HANDLE *pHandles;       // the suspended threads' handles, as pItems (NULL: not suspended); the last one first resumed
} FROZEN_THREADS, *PFROZEN_THREADS;

//-------------------------------------------------------------------------
// Global Variables:
//-------------------------------------------------------------------------

// Spin lock flag for EnterSpinLock()/LeaveSpinLock().
volatile LONG g_isLocked = FALSE;

// Private heap handle. If not NULL, this library is initialized.
HANDLE g_hHeap = NULL;

// Hook entries.
struct
{
    PHOOK_ENTRY pItems;     // Data heap
    UINT        capacity;   // Size of allocated data heap, items
    UINT        size;       // Actual number of data items
} g_hooks;

//-------------------------------------------------------------------------
// Returns INVALID_HOOK_POS if not found.
static UINT FindHookEntry(LPVOID pTarget)
{
    UINT i;
    for (i = 0; i < g_hooks.size; ++i)
    {
        if ((ULONG_PTR)pTarget == (ULONG_PTR)g_hooks.pItems[i].pTarget)
            return i;
    }

    return INVALID_HOOK_POS;
}

//-------------------------------------------------------------------------
static PHOOK_ENTRY AddHookEntry()
{
    if (g_hooks.pItems == NULL)
    {
        g_hooks.capacity = INITIAL_HOOK_CAPACITY;
        g_hooks.pItems = (PHOOK_ENTRY)HeapAlloc(
            g_hHeap, 0, g_hooks.capacity * sizeof(HOOK_ENTRY));
        if (g_hooks.pItems == NULL)
            return NULL;
    }
    else if (g_hooks.size >= g_hooks.capacity)
    {
        PHOOK_ENTRY p = (PHOOK_ENTRY)HeapReAlloc(
            g_hHeap, 0, g_hooks.pItems, (g_hooks.capacity * 2) * sizeof(HOOK_ENTRY));
        if (p == NULL)
            return NULL;

        g_hooks.capacity *= 2;
        g_hooks.pItems = p;
    }

    return &g_hooks.pItems[g_hooks.size++];
}

//-------------------------------------------------------------------------
static void DeleteHookEntry(UINT pos)
{
    if (pos < g_hooks.size - 1)
        g_hooks.pItems[pos] = g_hooks.pItems[g_hooks.size - 1];

    g_hooks.size--;

    if (g_hooks.capacity / 2 >= INITIAL_HOOK_CAPACITY && g_hooks.capacity / 2 >= g_hooks.size)
    {
        PHOOK_ENTRY p = (PHOOK_ENTRY)HeapReAlloc(
            g_hHeap, 0, g_hooks.pItems, (g_hooks.capacity / 2) * sizeof(HOOK_ENTRY));
        if (p == NULL)
            return;

        g_hooks.capacity /= 2;
        g_hooks.pItems = p;
    }
}

//-------------------------------------------------------------------------
static DWORD_PTR FindOldIP(PHOOK_ENTRY pHook, DWORD_PTR ip)
{
    UINT i;

    if (pHook->patchAbove && ip == ((DWORD_PTR)pHook->pTarget - sizeof(JMP_REL)))
        return (DWORD_PTR)pHook->pTarget;

    for (i = 0; i < pHook->nIP; ++i)
    {
        if (ip == ((DWORD_PTR)pHook->pTrampoline + pHook->newIPs[i]))
            return (DWORD_PTR)pHook->pTarget + pHook->oldIPs[i];
    }

#if defined(_M_X64) || defined(__x86_64__)
    // Check relay function.
    if (ip == (DWORD_PTR)pHook->pDetour)
        return (DWORD_PTR)pHook->pTarget;
#endif

    return 0;
}

//-------------------------------------------------------------------------
static DWORD_PTR FindNewIP(PHOOK_ENTRY pHook, DWORD_PTR ip)
{
    UINT i;
    for (i = 0; i < pHook->nIP; ++i)
    {
        if (ip == ((DWORD_PTR)pHook->pTarget + pHook->oldIPs[i]))
            return (DWORD_PTR)pHook->pTrampoline + pHook->newIPs[i];
    }

    return 0;
}

//-------------------------------------------------------------------------
static DWORD64 ProcessThreadIPs(HANDLE hThread, UINT pos, UINT action)
{
    // If the thread suspended in the overwritten area,
    // move IP to the proper address.

    CONTEXT c;
#if defined(_M_X64) || defined(__x86_64__)
    DWORD64 *pIP = &c.Rip;
#else
    DWORD   *pIP = &c.Eip;
#endif
    UINT count;

    c.ContextFlags = CONTEXT_CONTROL;
    if (!GetThreadContext(hThread, &c))
        return 0;

    if (pos == ALL_HOOKS_POS)
    {
        pos = 0;
        count = g_hooks.size;
    }
    else
    {
        count = pos + 1;
    }

    for (; pos < count; ++pos)
    {
        PHOOK_ENTRY pHook = &g_hooks.pItems[pos];
        BOOL        enable;
        DWORD_PTR   ip;

        switch (action)
        {
        case ACTION_DISABLE:
            enable = FALSE;
            break;

        case ACTION_ENABLE:
            enable = TRUE;
            break;

        default: // ACTION_APPLY_QUEUED
            enable = pHook->queueEnable;
            break;
        }
        if (pHook->isEnabled == enable)
            continue;

        if (enable)
            ip = FindNewIP(pHook, *pIP);
        else
            ip = FindOldIP(pHook, *pIP);

        if (ip != 0)
        {
            *pIP = ip;
            SetThreadContext(hThread, &c);
        }
    }
    return (DWORD64)*pIP;
}

//-------------------------------------------------------------------------
// This process's threads by NtGetNextThread (boot 123: the system-wide Toolhelp snapshot took 66 ms of the start-up
// freeze, 55 ms in boot 122); the snapshot if ntdll lacks it.
static BOOL AddThread(PFROZEN_THREADS pThreads, DWORD tid)
{
    if (pThreads->pItems == NULL)
    {
        pThreads->capacity = INITIAL_THREAD_CAPACITY;
        pThreads->pItems = (LPDWORD)HeapAlloc(g_hHeap, 0, pThreads->capacity * sizeof(DWORD));
        if (pThreads->pItems == NULL)
            return FALSE;
    }
    else if (pThreads->size >= pThreads->capacity)
    {
        LPDWORD p = (LPDWORD)HeapReAlloc(g_hHeap, 0, pThreads->pItems, (pThreads->capacity * 2) * sizeof(DWORD));
        if (p == NULL)
            return FALSE;
        pThreads->capacity *= 2;
        pThreads->pItems = p;
    }
    pThreads->pItems[pThreads->size++] = tid;
    return TRUE;
}

static VOID EnumerateThreads(PFROZEN_THREADS pThreads)
{
    typedef LONG (NTAPI *NextFn)(HANDLE, HANDLE, ACCESS_MASK, ULONG, ULONG, PHANDLE);
    static NextFn next = NULL;
    static BOOL looked = FALSE;
    if (!looked)
    {
        next = (NextFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtGetNextThread");
        looked = TRUE;
    }
    if (next != NULL)
    {
        HANDLE h = NULL, n = NULL;
        const DWORD self = GetCurrentThreadId();
        while (next(GetCurrentProcess(), h, THREAD_QUERY_LIMITED_INFORMATION, 0, 0, &n) >= 0)
        {
            const DWORD tid = GetThreadId(n);
            if (h != NULL)
                CloseHandle(h);
            h = n;
            if (tid != 0 && tid != self && !AddThread(pThreads, tid))
                break;
        }
        if (h != NULL)
            CloseHandle(h);
        if (pThreads->size != 0)
            return;
    }
    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (hSnapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 te;
        te.dwSize = sizeof(THREADENTRY32);
        if (Thread32First(hSnapshot, &te))
        {
            do
            {
                if (te.dwSize >= (FIELD_OFFSET(THREADENTRY32, th32OwnerProcessID) + sizeof(DWORD))
                    && te.th32OwnerProcessID == GetCurrentProcessId()
                    && te.th32ThreadID != GetCurrentThreadId())
                {
                    if (pThreads->pItems == NULL)
                    {
                        pThreads->capacity = INITIAL_THREAD_CAPACITY;
                        pThreads->pItems
                            = (LPDWORD)HeapAlloc(g_hHeap, 0, pThreads->capacity * sizeof(DWORD));
                        if (pThreads->pItems == NULL)
                            break;
                    }
                    else if (pThreads->size >= pThreads->capacity)
                    {
                        LPDWORD p = (LPDWORD)HeapReAlloc(
                            g_hHeap, 0, pThreads->pItems, (pThreads->capacity * 2) * sizeof(DWORD));
                        if (p == NULL)
                            break;

                        pThreads->capacity *= 2;
                        pThreads->pItems = p;
                    }
                    pThreads->pItems[pThreads->size++] = te.th32ThreadID;
                }

                te.dwSize = sizeof(THREADENTRY32);
            } while (Thread32Next(hSnapshot, &te));
        }
        CloseHandle(hSnapshot);
    }
}

//-------------------------------------------------------------------------
// KCD2 mods (deagle_native.asi, fastload.asi): each plugin links its own MinHook, and Freeze suspends every other
// thread of the process. Two plugins freezing at the same moment could suspend each other and hang the game, so
// both take one process-wide mutex from Freeze to Unfreeze (always paired below).
static HANDLE FreezeMutex(VOID)
{
    static HANDLE h = NULL;
    if (h == NULL)
    {
        char name[64];
        sprintf_s(name, sizeof(name), "Local\\KCD2_MinHook_Freeze_%lu", GetCurrentProcessId());
        HANDLE n = CreateMutexA(NULL, FALSE, name);
        if (n != NULL && InterlockedCompareExchangePointer(&h, n, NULL) != NULL)
            CloseHandle(n);
    }
    return h;
}

// How long this plugin's MinHook kept the game's threads suspended (fastload.asi logs it at start-up, 2026-10-24):
// from the first suspend to the last resume, per freeze. MH_FreezeStats reads the totals.
static volatile LONG64 g_freezeUs = 0, g_freezeLongestUs = 0, g_freezeCount = 0;
static volatile LONG g_freezeMaxThreads = 0;
static LARGE_INTEGER g_freezeStart;
void WINAPI MH_FreezeStats(ULONGLONG* us, ULONGLONG* count, ULONGLONG* longestUs, ULONG* maxThreads)
{
    if (us) *us = (ULONGLONG)g_freezeUs;
    if (count) *count = (ULONGLONG)g_freezeCount;
    if (longestUs) *longestUs = (ULONGLONG)g_freezeLongestUs;
    if (maxThreads) *maxThreads = (ULONG)g_freezeMaxThreads;
}

// Freeze order (fastload.asi, 2026-10-25): boots 96 and 97 kept the game's main thread suspended 1.25-1.36 s in the
// start-up freeze, with no thread of the process using CPU: the freeze was waiting for some thread to stop (a thread
// in the kernel stops only when it leaves there), and every thread already stopped, main among them, waited with it.
// Now every other thread is asked to stop first (SuspendThread only queues the stop), then each is waited for, and the
// thread named by MH_SetFreezeLast (main) is stopped last and resumed first: main is held only for the patching.
// Safe as before: no thread runs user code between its SuspendThread and its ResumeThread, and every one is stopped
// and its instruction pointer checked before any byte is written.
static volatile DWORD g_freezeLast = 0;
VOID WINAPI MH_SetFreezeLast(DWORD tid) { g_freezeLast = tid; }

// The freeze being made and the longest one so far (MH_FreezeSlowest).
typedef struct _FREEZE_INFO
{
    ULONGLONG totalUs, slowUs, mainWaitUs, mainHeldUs, patchUs;
    ULONGLONG enumUs, askUs, askSlowUs, waitUs;   // the thread list, the stop requests (and the slowest one), the waits
    DWORD     slowTid, askSlowTid;
    ULONGLONG slowRip, slowStart, askSlowStart;
} FREEZE_INFO;
static FREEZE_INFO   g_cur, g_longest;
static LARGE_INTEGER g_frozenAt, g_mainStopAt;

static ULONGLONG UsSince(const LARGE_INTEGER* t0)
{
    LARGE_INTEGER f, n;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&n);
    return (ULONGLONG)((n.QuadPart - t0->QuadPart) * 1000000 / f.QuadPart);
}

static ULONGLONG ThreadStart(HANDLE h)
{
    typedef LONG (NTAPI *QueryFn)(HANDLE, int, PVOID, ULONG, PULONG);
    static QueryFn q = NULL;
    ULONG_PTR start = 0;
    if (q == NULL)
        q = (QueryFn)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtQueryInformationThread");
    if (q != NULL && q(h, 9 /* ThreadQuerySetWin32StartAddress */, &start, sizeof(start), NULL) < 0)
        start = 0;
    return (ULONGLONG)start;
}

void WINAPI MH_FreezeSlowest(ULONGLONG* totalUs, ULONGLONG* mainHeldUs, ULONGLONG* mainWaitUs, ULONGLONG* patchUs,
                             ULONGLONG* slowUs, DWORD* slowTid, ULONGLONG* slowRip, ULONGLONG* slowStart)
{
    if (totalUs) *totalUs = g_longest.totalUs;
    if (mainHeldUs) *mainHeldUs = g_longest.mainHeldUs;
    if (mainWaitUs) *mainWaitUs = g_longest.mainWaitUs;
    if (patchUs) *patchUs = g_longest.patchUs;
    if (slowUs) *slowUs = g_longest.slowUs;
    if (slowTid) *slowTid = g_longest.slowTid;
    if (slowRip) *slowRip = g_longest.slowRip;
    if (slowStart) *slowStart = g_longest.slowStart;
}

// The longest freeze's parts before the patching (boot 119: 1231 ms with 1.1 ms of patching and 0.1 ms for the slowest
// thread to stop: the rest is in one of these).
void WINAPI MH_FreezeParts(ULONGLONG* enumUs, ULONGLONG* askUs, ULONGLONG* askSlowUs, DWORD* askSlowTid, ULONGLONG* askSlowStart,
                           ULONGLONG* waitUs)
{
    if (askSlowStart) *askSlowStart = g_longest.askSlowStart;
    if (enumUs) *enumUs = g_longest.enumUs;
    if (askUs) *askUs = g_longest.askUs;
    if (askSlowUs) *askSlowUs = g_longest.askSlowUs;
    if (askSlowTid) *askSlowTid = g_longest.askSlowTid;
    if (waitUs) *waitUs = g_longest.waitUs;
}

// The freezing thread runs at time-critical priority from the first stop request to the last resume (boots 119-123: the
// waits for the threads to stop took 194-1231 ms while each one took at most 0.1 ms: this thread waited for a CPU
// between them, behind the read-ahead workers, with the game's other threads stopped all that time).
static int g_freezePrio = THREAD_PRIORITY_NORMAL;

static VOID Freeze(PFROZEN_THREADS pThreads, UINT pos, UINT action)
{
    if (FreezeMutex() != NULL)
        WaitForSingleObject(FreezeMutex(), INFINITE);   // WAIT_ABANDONED: the other plugin's thread died holding it; ours now
    g_freezePrio = GetThreadPriority(GetCurrentThread());
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    QueryPerformanceCounter(&g_freezeStart);   // under the mutex: one freeze at a time
    ZeroMemory(&g_cur, sizeof(g_cur));
    pThreads->pItems   = NULL;
    pThreads->capacity = 0;
    pThreads->size     = 0;
    pThreads->pHandles = NULL;
    EnumerateThreads(pThreads);
    g_cur.enumUs = UsSince(&g_freezeStart);

    if (pThreads->pItems != NULL)
    {
        UINT i, n = pThreads->size, others = n;
        const DWORD last = g_freezeLast;
        for (i = 0; last != 0 && i < n; ++i)
        {
            if (pThreads->pItems[i] == last)
            {
                pThreads->pItems[i] = pThreads->pItems[n - 1];
                pThreads->pItems[n - 1] = last;
                others = n - 1;
                break;
            }
        }
        pThreads->pHandles = (HANDLE*)HeapAlloc(g_hHeap, HEAP_ZERO_MEMORY, n * sizeof(HANDLE));
        if (pThreads->pHandles == NULL)
        {
            // as MinHook did: one thread at a time
            for (i = 0; i < n; ++i)
            {
                HANDLE hThread = OpenThread(THREAD_ACCESS, FALSE, pThreads->pItems[i]);
                if (hThread != NULL)
                {
                    SuspendThread(hThread);
                    ProcessThreadIPs(hThread, pos, action);
                    CloseHandle(hThread);
                }
            }
            QueryPerformanceCounter(&g_frozenAt);
            return;
        }
        // 1: every other thread asked to stop (queued, no wait)
        LARGE_INTEGER a0;
        QueryPerformanceCounter(&a0);
        for (i = 0; i < others; ++i)
        {
            LARGE_INTEGER t1;
            ULONGLONG us1;
            HANDLE hThread;
            QueryPerformanceCounter(&t1);
            hThread = OpenThread(THREAD_ACCESS, FALSE, pThreads->pItems[i]);
            if (hThread == NULL)
                continue;
            if (SuspendThread(hThread) == (DWORD)-1)
            {
                CloseHandle(hThread);
                continue;
            }
            pThreads->pHandles[i] = hThread;
            us1 = UsSince(&t1);
            if (us1 > g_cur.askSlowUs)
            {
                g_cur.askSlowUs = us1;
                g_cur.askSlowTid = pThreads->pItems[i];
                g_cur.askSlowStart = ThreadStart(hThread);
            }
        }
        g_cur.askUs = UsSince(&a0);
        QueryPerformanceCounter(&a0);
        // 2: each waited for (GetThreadContext returns once it has stopped) and its instruction pointer checked
        for (i = 0; i < others; ++i)
        {
            LARGE_INTEGER t0;
            DWORD64 rip;
            ULONGLONG us;
            if (pThreads->pHandles[i] == NULL)
                continue;
            QueryPerformanceCounter(&t0);
            rip = ProcessThreadIPs(pThreads->pHandles[i], pos, action);
            us = UsSince(&t0);
            if (us > g_cur.slowUs)
            {
                g_cur.slowUs = us;
                g_cur.slowTid = pThreads->pItems[i];
                g_cur.slowRip = rip;
                g_cur.slowStart = ThreadStart(pThreads->pHandles[i]);
            }
        }
        g_cur.waitUs = UsSince(&a0);
        // 3: main last
        if (others < n)
        {
            HANDLE hThread = OpenThread(THREAD_ACCESS, FALSE, pThreads->pItems[others]);
            QueryPerformanceCounter(&g_mainStopAt);
            if (hThread != NULL && SuspendThread(hThread) != (DWORD)-1)
            {
                pThreads->pHandles[others] = hThread;
                ProcessThreadIPs(hThread, pos, action);
            }
            else if (hThread != NULL)
                CloseHandle(hThread);
            g_cur.mainWaitUs = UsSince(&g_mainStopAt);
        }
    }
    QueryPerformanceCounter(&g_frozenAt);
}

//-------------------------------------------------------------------------
static VOID Unfreeze(PFROZEN_THREADS pThreads)
{
    g_cur.patchUs = UsSince(&g_frozenAt);
    if (pThreads->pItems != NULL)
    {
        UINT i;
        if (pThreads->pHandles != NULL)
        {
            // the last one (main) first
            for (i = pThreads->size; i-- > 0;)
            {
                if (pThreads->pHandles[i] == NULL)
                    continue;
                ResumeThread(pThreads->pHandles[i]);
                CloseHandle(pThreads->pHandles[i]);
                if (i == pThreads->size - 1 && pThreads->pItems[i] == g_freezeLast)
                    g_cur.mainHeldUs = UsSince(&g_mainStopAt);
            }
            HeapFree(g_hHeap, 0, pThreads->pHandles);
        }
        else
        {
            for (i = 0; i < pThreads->size; ++i)
            {
                HANDLE hThread = OpenThread(THREAD_ACCESS, FALSE, pThreads->pItems[i]);
                if (hThread != NULL)
                {
                    ResumeThread(hThread);
                    CloseHandle(hThread);
                }
            }
        }

        HeapFree(g_hHeap, 0, pThreads->pItems);
    }
    {
        LONG64 us = (LONG64)UsSince(&g_freezeStart);
        g_freezeUs += us;
        g_freezeCount++;
        if (us > g_freezeLongestUs)
        {
            g_freezeLongestUs = us;
            g_cur.totalUs = (ULONGLONG)us;
            g_longest = g_cur;
        }
        if ((LONG)pThreads->size > g_freezeMaxThreads) g_freezeMaxThreads = (LONG)pThreads->size;
    }
    SetThreadPriority(GetCurrentThread(), g_freezePrio == THREAD_PRIORITY_ERROR_RETURN ? THREAD_PRIORITY_NORMAL : g_freezePrio);
    if (FreezeMutex() != NULL)
        ReleaseMutex(FreezeMutex());
}

//-------------------------------------------------------------------------
static MH_STATUS EnableHookLL(UINT pos, BOOL enable)
{
    PHOOK_ENTRY pHook = &g_hooks.pItems[pos];
    DWORD  oldProtect;
    SIZE_T patchSize    = sizeof(JMP_REL);
    LPBYTE pPatchTarget = (LPBYTE)pHook->pTarget;

    if (pHook->patchAbove)
    {
        pPatchTarget -= sizeof(JMP_REL);
        patchSize    += sizeof(JMP_REL_SHORT);
    }

    if (!VirtualProtect(pPatchTarget, patchSize, PAGE_EXECUTE_READWRITE, &oldProtect))
        return MH_ERROR_MEMORY_PROTECT;

    if (enable)
    {
        PJMP_REL pJmp = (PJMP_REL)pPatchTarget;
        pJmp->opcode = 0xE9;
        pJmp->operand = (UINT32)((LPBYTE)pHook->pDetour - (pPatchTarget + sizeof(JMP_REL)));

        if (pHook->patchAbove)
        {
            PJMP_REL_SHORT pShortJmp = (PJMP_REL_SHORT)pHook->pTarget;
            pShortJmp->opcode = 0xEB;
            pShortJmp->operand = (UINT8)(0 - (sizeof(JMP_REL_SHORT) + sizeof(JMP_REL)));
        }
    }
    else
    {
        if (pHook->patchAbove)
            memcpy(pPatchTarget, pHook->backup, sizeof(JMP_REL) + sizeof(JMP_REL_SHORT));
        else
            memcpy(pPatchTarget, pHook->backup, sizeof(JMP_REL));
    }

    VirtualProtect(pPatchTarget, patchSize, oldProtect, &oldProtect);

    // Just-in-case measure.
    FlushInstructionCache(GetCurrentProcess(), pPatchTarget, patchSize);

    pHook->isEnabled   = enable;
    pHook->queueEnable = enable;

    return MH_OK;
}

//-------------------------------------------------------------------------
static MH_STATUS EnableAllHooksLL(BOOL enable)
{
    MH_STATUS status = MH_OK;
    UINT i, first = INVALID_HOOK_POS;

    for (i = 0; i < g_hooks.size; ++i)
    {
        if (g_hooks.pItems[i].isEnabled != enable)
        {
            first = i;
            break;
        }
    }

    if (first != INVALID_HOOK_POS)
    {
        FROZEN_THREADS threads;
        Freeze(&threads, ALL_HOOKS_POS, enable ? ACTION_ENABLE : ACTION_DISABLE);

        for (i = first; i < g_hooks.size; ++i)
        {
            if (g_hooks.pItems[i].isEnabled != enable)
            {
                status = EnableHookLL(i, enable);
                if (status != MH_OK)
                    break;
            }
        }

        Unfreeze(&threads);
    }

    return status;
}

//-------------------------------------------------------------------------
static VOID EnterSpinLock(VOID)
{
    SIZE_T spinCount = 0;

    // Wait until the flag is FALSE.
    while (InterlockedCompareExchange(&g_isLocked, TRUE, FALSE) != FALSE)
    {
        // No need to generate a memory barrier here, since InterlockedCompareExchange()
        // generates a full memory barrier itself.

        // Prevent the loop from being too busy.
        if (spinCount < 32)
            Sleep(0);
        else
            Sleep(1);

        spinCount++;
    }
}

//-------------------------------------------------------------------------
static VOID LeaveSpinLock(VOID)
{
    // No need to generate a memory barrier here, since InterlockedExchange()
    // generates a full memory barrier itself.

    InterlockedExchange(&g_isLocked, FALSE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_Initialize(VOID)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap == NULL)
    {
        g_hHeap = HeapCreate(0, 0, 0);
        if (g_hHeap != NULL)
        {
            // Initialize the internal function buffer.
            InitializeBuffer();
        }
        else
        {
            status = MH_ERROR_MEMORY_ALLOC;
        }
    }
    else
    {
        status = MH_ERROR_ALREADY_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_Uninitialize(VOID)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        status = EnableAllHooksLL(FALSE);
        if (status == MH_OK)
        {
            // Free the internal function buffer.

            // HeapFree is actually not required, but some tools detect a false
            // memory leak without HeapFree.

            UninitializeBuffer();

            HeapFree(g_hHeap, 0, g_hooks.pItems);
            HeapDestroy(g_hHeap);

            g_hHeap = NULL;

            g_hooks.pItems   = NULL;
            g_hooks.capacity = 0;
            g_hooks.size     = 0;
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_CreateHook(LPVOID pTarget, LPVOID pDetour, LPVOID *ppOriginal)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        if (IsExecutableAddress(pTarget) && IsExecutableAddress(pDetour))
        {
            UINT pos = FindHookEntry(pTarget);
            if (pos == INVALID_HOOK_POS)
            {
                LPVOID pBuffer = AllocateBuffer(pTarget);
                if (pBuffer != NULL)
                {
                    TRAMPOLINE ct;

                    ct.pTarget     = pTarget;
                    ct.pDetour     = pDetour;
                    ct.pTrampoline = pBuffer;
                    if (CreateTrampolineFunction(&ct))
                    {
                        PHOOK_ENTRY pHook = AddHookEntry();
                        if (pHook != NULL)
                        {
                            pHook->pTarget     = ct.pTarget;
#if defined(_M_X64) || defined(__x86_64__)
                            pHook->pDetour     = ct.pRelay;
#else
                            pHook->pDetour     = ct.pDetour;
#endif
                            pHook->pTrampoline = ct.pTrampoline;
                            pHook->patchAbove  = ct.patchAbove;
                            pHook->isEnabled   = FALSE;
                            pHook->queueEnable = FALSE;
                            pHook->nIP         = ct.nIP;
                            memcpy(pHook->oldIPs, ct.oldIPs, ARRAYSIZE(ct.oldIPs));
                            memcpy(pHook->newIPs, ct.newIPs, ARRAYSIZE(ct.newIPs));

                            // Back up the target function.

                            if (ct.patchAbove)
                            {
                                memcpy(
                                    pHook->backup,
                                    (LPBYTE)pTarget - sizeof(JMP_REL),
                                    sizeof(JMP_REL) + sizeof(JMP_REL_SHORT));
                            }
                            else
                            {
                                memcpy(pHook->backup, pTarget, sizeof(JMP_REL));
                            }

                            if (ppOriginal != NULL)
                                *ppOriginal = pHook->pTrampoline;
                        }
                        else
                        {
                            status = MH_ERROR_MEMORY_ALLOC;
                        }
                    }
                    else
                    {
                        status = MH_ERROR_UNSUPPORTED_FUNCTION;
                    }

                    if (status != MH_OK)
                    {
                        FreeBuffer(pBuffer);
                    }
                }
                else
                {
                    status = MH_ERROR_MEMORY_ALLOC;
                }
            }
            else
            {
                status = MH_ERROR_ALREADY_CREATED;
            }
        }
        else
        {
            status = MH_ERROR_NOT_EXECUTABLE;
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_RemoveHook(LPVOID pTarget)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        UINT pos = FindHookEntry(pTarget);
        if (pos != INVALID_HOOK_POS)
        {
            if (g_hooks.pItems[pos].isEnabled)
            {
                FROZEN_THREADS threads;
                Freeze(&threads, pos, ACTION_DISABLE);

                status = EnableHookLL(pos, FALSE);

                Unfreeze(&threads);
            }

            if (status == MH_OK)
            {
                FreeBuffer(g_hooks.pItems[pos].pTrampoline);
                DeleteHookEntry(pos);
            }
        }
        else
        {
            status = MH_ERROR_NOT_CREATED;
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
// Deferred enables (fastload.asi's start-up, 2026-10-24): each MH_EnableHook froze every thread of the game, 94 times
// at start-up, 5.9 s in all (boot 94). While deferred, MH_EnableHook(target) only queues the hook and MH_DisableHook
// on a hook not yet on takes it off the queue; MH_ApplyQueued turns the queue on in one freeze. Off by default.
static volatile BOOL g_defer = FALSE;
VOID WINAPI MH_DeferEnables(BOOL defer)
{
    g_defer = defer;
}

static MH_STATUS EnableHook(LPVOID pTarget, BOOL enable)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL && g_defer && pTarget != MH_ALL_HOOKS)
    {
        UINT pos = FindHookEntry(pTarget);
        if (pos == INVALID_HOOK_POS)
        {
            LeaveSpinLock();
            return MH_ERROR_NOT_CREATED;
        }
        if (enable || !g_hooks.pItems[pos].isEnabled)
        {
            if (enable && g_hooks.pItems[pos].isEnabled)
                status = MH_ERROR_ENABLED;
            else
                g_hooks.pItems[pos].queueEnable = enable;
            LeaveSpinLock();
            return status;
        }
        // a hook already on is turned off now, as without deferring
    }

    if (g_hHeap != NULL)
    {
        if (pTarget == MH_ALL_HOOKS)
        {
            status = EnableAllHooksLL(enable);
        }
        else
        {
            FROZEN_THREADS threads;
            UINT pos = FindHookEntry(pTarget);
            if (pos != INVALID_HOOK_POS)
            {
                if (g_hooks.pItems[pos].isEnabled != enable)
                {
                    Freeze(&threads, pos, ACTION_ENABLE);

                    status = EnableHookLL(pos, enable);

                    Unfreeze(&threads);
                }
                else
                {
                    status = enable ? MH_ERROR_ENABLED : MH_ERROR_DISABLED;
                }
            }
            else
            {
                status = MH_ERROR_NOT_CREATED;
            }
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_EnableHook(LPVOID pTarget)
{
    return EnableHook(pTarget, TRUE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_DisableHook(LPVOID pTarget)
{
    return EnableHook(pTarget, FALSE);
}

//-------------------------------------------------------------------------
static MH_STATUS QueueHook(LPVOID pTarget, BOOL queueEnable)
{
    MH_STATUS status = MH_OK;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        if (pTarget == MH_ALL_HOOKS)
        {
            UINT i;
            for (i = 0; i < g_hooks.size; ++i)
                g_hooks.pItems[i].queueEnable = queueEnable;
        }
        else
        {
            UINT pos = FindHookEntry(pTarget);
            if (pos != INVALID_HOOK_POS)
            {
                g_hooks.pItems[pos].queueEnable = queueEnable;
            }
            else
            {
                status = MH_ERROR_NOT_CREATED;
            }
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_QueueEnableHook(LPVOID pTarget)
{
    return QueueHook(pTarget, TRUE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_QueueDisableHook(LPVOID pTarget)
{
    return QueueHook(pTarget, FALSE);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_ApplyQueued(VOID)
{
    MH_STATUS status = MH_OK;
    UINT i, first = INVALID_HOOK_POS;

    EnterSpinLock();

    if (g_hHeap != NULL)
    {
        for (i = 0; i < g_hooks.size; ++i)
        {
            if (g_hooks.pItems[i].isEnabled != g_hooks.pItems[i].queueEnable)
            {
                first = i;
                break;
            }
        }

        if (first != INVALID_HOOK_POS)
        {
            FROZEN_THREADS threads;
            Freeze(&threads, ALL_HOOKS_POS, ACTION_APPLY_QUEUED);

            for (i = first; i < g_hooks.size; ++i)
            {
                PHOOK_ENTRY pHook = &g_hooks.pItems[i];
                if (pHook->isEnabled != pHook->queueEnable)
                {
                    status = EnableHookLL(i, pHook->queueEnable);
                    if (status != MH_OK)
                        break;
                }
            }

            Unfreeze(&threads);
        }
    }
    else
    {
        status = MH_ERROR_NOT_INITIALIZED;
    }

    LeaveSpinLock();

    return status;
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_CreateHookApiEx(
    LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour,
    LPVOID *ppOriginal, LPVOID *ppTarget)
{
    HMODULE hModule;
    LPVOID  pTarget;

    hModule = GetModuleHandleW(pszModule);
    if (hModule == NULL)
        return MH_ERROR_MODULE_NOT_FOUND;

    pTarget = (LPVOID)GetProcAddress(hModule, pszProcName);
    if (pTarget == NULL)
        return MH_ERROR_FUNCTION_NOT_FOUND;

    if(ppTarget != NULL)
        *ppTarget = pTarget;

    return MH_CreateHook(pTarget, pDetour, ppOriginal);
}

//-------------------------------------------------------------------------
MH_STATUS WINAPI MH_CreateHookApi(
    LPCWSTR pszModule, LPCSTR pszProcName, LPVOID pDetour, LPVOID *ppOriginal)
{
   return MH_CreateHookApiEx(pszModule, pszProcName, pDetour, ppOriginal, NULL);
}

//-------------------------------------------------------------------------
const char * WINAPI MH_StatusToString(MH_STATUS status)
{
#define MH_ST2STR(x)    \
    case x:             \
        return #x;

    switch (status) {
        MH_ST2STR(MH_UNKNOWN)
        MH_ST2STR(MH_OK)
        MH_ST2STR(MH_ERROR_ALREADY_INITIALIZED)
        MH_ST2STR(MH_ERROR_NOT_INITIALIZED)
        MH_ST2STR(MH_ERROR_ALREADY_CREATED)
        MH_ST2STR(MH_ERROR_NOT_CREATED)
        MH_ST2STR(MH_ERROR_ENABLED)
        MH_ST2STR(MH_ERROR_DISABLED)
        MH_ST2STR(MH_ERROR_NOT_EXECUTABLE)
        MH_ST2STR(MH_ERROR_UNSUPPORTED_FUNCTION)
        MH_ST2STR(MH_ERROR_MEMORY_ALLOC)
        MH_ST2STR(MH_ERROR_MEMORY_PROTECT)
        MH_ST2STR(MH_ERROR_MODULE_NOT_FOUND)
        MH_ST2STR(MH_ERROR_FUNCTION_NOT_FOUND)
    }

#undef MH_ST2STR

    return "(unknown)";
}
