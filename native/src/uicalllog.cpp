// uicalllog: who calls a menu panel's "Activate" / "Deactivate" (inventory.asi, a one-off for kcd2-deagle-8a's keepload).
//
// 2026-10-27: a kept-loaded inventory reopens without the game's Activate (its movie's fc_activate, mapped in
// UIElements/ApseInventoryList.xml), so every mouse button on it stays dead. keepload needs the game's own Activate path
// to call it on a kept reopen. WHGame 1.5.6 FUN_180566bcc(panel, &name) calls a panel's movie function without
// arguments: panel->vtbl+0x60, then element = panel+0x48, element->vtbl+0x228(element, name, args, 0, 0). This logs,
// for the names Activate / Deactivate only, the panel (its RTTI class), the element and the call stack as WHGame RVAs.
// At most 200 lines a run.
// 2026-10-28: the inventory panel's Activate never came through FUN_180566bcc (only C_UIMapContent did), so the
// element's own CallFunction (element->vtbl+0x228) is hooked too, for every element: it is installed the first time a
// panel call shows an element (each distinct function behind that slot, up to 4), and logs Activate / Deactivate with
// the element, its RTTI class and 8 caller frames as WHGame RVAs ("uicall el:", at most 300 lines).
#include <windows.h>
#include <cstdio>
#include <cstring>
#include "MinHook.h"
#include "log.h"
#include "uicalllog.h"

namespace {
constexpr uintptr_t kRva = 0x566bcc;
const unsigned char kBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x60,0x48,0x8b,0x01,0x48,0x8b,0xfa };
using Fn = unsigned char(__fastcall*)(void* panel, void* name);
Fn g_orig = nullptr;
uintptr_t g_base = 0, g_end = 0;
volatile LONG g_lines = 0;

bool ReadName(void* nameRef, char* out, size_t n) {
	__try {
		const char* s = *(const char* const*)nameRef;
		if (!s) return false;
		size_t i = 0;
		for (; i + 1 < n && s[i]; i++) out[i] = s[i];
		out[i] = 0;
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// MSVC x64 RTTI: vtable[-1] = complete object locator (signature 1: RVAs), +0x0c the type descriptor's RVA, name at +0x10
void Rtti(const void* obj, char* out, size_t n) {
	out[0] = 0;
	__try {
		const uintptr_t vt = *(const uintptr_t*)obj;
		const unsigned char* col = *(const unsigned char* const*)(vt - 8);
		if (*(const int*)col != 1) return;
		const uintptr_t imageBase = (uintptr_t)col - *(const int*)(col + 0x14);
		const char* name = (const char*)(imageBase + *(const int*)(col + 0x0c) + 0x10);
		size_t i = 0;
		for (; i + 1 < n && name[i]; i++) out[i] = name[i];
		out[i] = 0;
	} __except (EXCEPTION_EXECUTE_HANDLER) { out[0] = 0; }
}

void Stack(char* stack, size_t size) {
	void* frames[9] = {};
	const USHORT got = RtlCaptureStackBackTrace(2, 8, frames, nullptr);
	size_t len = 0;
	stack[0] = 0;
	for (USHORT i = 0; i < got && len + 24 < size; i++) {
		const uintptr_t a = (uintptr_t)frames[i];
		len += (a >= g_base && a < g_end) ? snprintf(stack + len, size - len, " WHGame+%llx", (unsigned long long)(a - g_base))
		                                  : snprintf(stack + len, size - len, " %p", frames[i]);
	}
}

// The element call's name may be a C string or a CryString (a pointer to the characters): accept either.
bool ReadEither(void* p, char* out, size_t n) {
	__try {
		const unsigned char* s = (const unsigned char*)p;
		if (s && s[0] >= 'A' && s[0] <= 'z') {
			size_t i = 0;
			for (; i + 1 < n && s[i] >= 0x20 && s[i] < 0x7f; i++) out[i] = (char)s[i];
			out[i] = 0;
			if (!s[i]) return true;
		}
	} __except (EXCEPTION_EXECUTE_HANDLER) {}
	return ReadName(p, out, n);
}

using ElFn = uintptr_t(__fastcall*)(void*, void*, void*, void*, void*, void*);
struct ElHook { void* target = nullptr; ElFn orig = nullptr; };
ElHook g_el[4];
volatile LONG g_elLines = 0;
template <int I>
uintptr_t __fastcall HkEl(void* el, void* name, void* a3, void* a4, void* a5, void* a6) {
	char n[32];
	if (g_elLines < 300 && ReadEither(name, n, sizeof n) && (!strcmp(n, "Activate") || !strcmp(n, "Deactivate"))) {
		InterlockedIncrement(&g_elLines);
		char stack[288], cls[128];
		Stack(stack, sizeof stack);
		Rtti(el, cls, sizeof cls);
		Log("uicall el: %s element %p %s fn %d thread %lu; from%s", n, el, cls, I, GetCurrentThreadId(), stack);
	}
	return g_el[I].orig(el, name, a3, a4, a5, a6);
}
void* const g_elHooks[4] = { (void*)&HkEl<0>, (void*)&HkEl<1>, (void*)&HkEl<2>, (void*)&HkEl<3> };
SRWLOCK g_elLock = SRWLOCK_INIT;

void HookElement(void* element) {
	void* fn = nullptr;
	__try { fn = (*(void***)element)[0x228 / 8]; } __except (EXCEPTION_EXECUTE_HANDLER) { return; }
	if (!fn) return;
	AcquireSRWLockExclusive(&g_elLock);
	int slot = -1;
	for (int i = 0; i < 4; i++) {
		if (g_el[i].target == fn) { slot = -2; break; }
		if (!g_el[i].target && slot == -1) slot = i;
	}
	if (slot >= 0) {
		g_el[slot].target = fn;   // also marks a failed hook, so it is tried once
		if (MH_CreateHook(fn, g_elHooks[slot], (void**)&g_el[slot].orig) == MH_OK && MH_EnableHook(fn) == MH_OK)
			Log("uicall el: element CallFunction %d hooked at %p (WHGame+%llx)", slot, fn,
			    (unsigned long long)((uintptr_t)fn - g_base));
		else Log("uicall el: hooking element CallFunction at %p failed", fn);
	}
	ReleaseSRWLockExclusive(&g_elLock);
}

unsigned char __fastcall Hk(void* panel, void* nameRef) {
	char name[32];
	if (g_lines < 200 && ReadName(nameRef, name, sizeof name) && (!strcmp(name, "Activate") || !strcmp(name, "Deactivate"))) {
		InterlockedIncrement(&g_lines);
		void* frames[8] = {};
		const USHORT got = RtlCaptureStackBackTrace(1, 8, frames, nullptr);
		char stack[256] = "";
		size_t len = 0;
		for (USHORT i = 0; i < got && len + 24 < sizeof stack; i++) {
			const uintptr_t a = (uintptr_t)frames[i];
			len += (a >= g_base && a < g_end) ? snprintf(stack + len, sizeof stack - len, " WHGame+%llx", (unsigned long long)(a - g_base))
			                                  : snprintf(stack + len, sizeof stack - len, " %p", frames[i]);
		}
		char cls[128];
		Rtti(panel, cls, sizeof cls);
		void* element = nullptr;
		__try { element = *(void**)((char*)panel + 0x48); } __except (EXCEPTION_EXECUTE_HANDLER) {}
		Log("uicall: %s panel %p %s element %p thread %lu; from%s", name, panel, cls, element, GetCurrentThreadId(), stack);
	}
	void* el = nullptr;
	__try { el = *(void**)((char*)panel + 0x48); } __except (EXCEPTION_EXECUTE_HANDLER) {}
	if (el) HookElement(el);
	return g_orig(panel, nameRef);
}
}  // namespace

void UiCallLog_Install(uintptr_t base, size_t size) {
	g_base = base;
	g_end = base + size;
	void* at = (void*)(base + kRva);
	if (memcmp(at, kBytes, sizeof kBytes) != 0) { Log("uicall: +0x%x isn't the 1.5.6 code: off", (unsigned)kRva); return; }
	if (MH_CreateHook(at, (void*)&Hk, (void**)&g_orig) != MH_OK || MH_EnableHook(at) != MH_OK) { Log("uicall: hook failed: off"); return; }
	Log("uicall: on (menu panels' Activate / Deactivate calls are logged with their callers, at most 200 lines)");
}
