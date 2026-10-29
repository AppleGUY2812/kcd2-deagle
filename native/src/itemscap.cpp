// itemscap: copy the inventory list's row data as the game hands it to Flash, so our own list (invlist) shows the
// game's exact values (names, columns, badges, prices) without computing them again.
// WHGame 1.5.6: SetItems FUN_180c49eac collects the rows into a std::vector of 16-byte values and passes it to
// FUN_1807f7114(element + 0x60, const CryString* name = "Items", vector*) (the UIElement array g_ItemsA,
// ApseInventoryList.xml); the value's +8 is a CryString (FUN_180f590d8 frees it when non-null). The rows are flat:
// the Flash script reads them with a stride. "itemscap dump" writes the next "Items" array to itemscap.txt (every
// value as int / float / string) so the stride and the fields can be matched against a screenshot.
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include "MinHook.h"
#include "log.h"

namespace {
constexpr uintptr_t kRvaSetArray = 0x7f7114;
constexpr uint8_t kSetArrayBytes[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xec, 0x50 };
typedef void(__fastcall* SetArrayFn)(void* element, void* name, void* vec);
SetArrayFn o_setArray = nullptr;
volatile LONG g_armed = 0;
std::string g_path;
unsigned g_seen = 0;

const char* NameOf(void* name) {
	__try { const char* s = *(const char**)name; return (s && !IsBadReadPtr(s, 8)) ? s : ""; } __except (1) { return ""; }
}

// One value as text: the +0 qword as two int32 and a float, the +8 string when it reads as one.
void Describe(const uint8_t* v, char* out, size_t n) {
	__try {
		int32_t lo = *(const int32_t*)v, hi = *(const int32_t*)(v + 4);
		float f = *(const float*)v;
		const char* s = *(const char* const*)(v + 8);
		char str[96] = "";
		if (s && !IsBadReadPtr(s, 1)) {
			size_t k = 0;
			for (; k < sizeof(str) - 1 && s[k]; k++) str[k] = (s[k] == '\t' || s[k] == '\n') ? ' ' : s[k];
			str[k] = 0;
		}
		snprintf(out, n, "%d\t%d\t%g\t%s", lo, hi, f, s ? str : "-");
	} __except (1) { snprintf(out, n, "(unreadable)"); }
}

void Dump(void* vec) {
	const uint8_t *b = nullptr, *e = nullptr;
	__try { b = *(const uint8_t**)vec; e = *(const uint8_t**)((char*)vec + 8); } __except (1) { return; }
	if (!b || e < b) return;
	size_t count = (size_t)(e - b) / 16;
	FILE* f = nullptr;
	if (fopen_s(&f, g_path.c_str(), "w") || !f) { Log("itemscap: can't write %s", g_path.c_str()); return; }
	fprintf(f, "# %zu values (index, +0 as int32, +4 as int32, +0 as float, +8 string)\n", count);
	char line[200];
	for (size_t i = 0; i < count; i++) { Describe(b + i * 16, line, sizeof(line)); fprintf(f, "%zu\t%s\n", i, line); }
	fclose(f);
	Log("itemscap: %zu Items values written to %s", count, g_path.c_str());
}

// 1 = the bytes match, 0 = they differ, -1 = unreadable (a helper: __try can't share a function with std::string)
int BytesMatch(const void* at) {
	__try { return memcmp(at, kSetArrayBytes, sizeof(kSetArrayBytes)) ? 0 : 1; } __except (1) { return -1; }
}

void __fastcall HkSetArray(void* element, void* name, void* vec) {
	if (g_armed && !strcmp(NameOf(name), "Items")) {
		InterlockedExchange(&g_armed, 0);
		Dump(vec);
	}
	g_seen++;
	o_setArray(element, name, vec);
}
}   // namespace

void Itemscap_Install(HMODULE self) {
	static bool done = false;
	if (done) return;
	done = true;
	char p[MAX_PATH];
	GetModuleFileNameA(self, p, MAX_PATH);
	g_path = p;
	g_path = g_path.substr(0, g_path.find_last_of('\\') + 1) + "itemscap.txt";
	uintptr_t base = (uintptr_t)GetModuleHandleA("WHGame.dll");
	if (!base) { Log("itemscap: no WHGame.dll"); return; }
	void* at = (void*)(base + kRvaSetArray);
	const int m = BytesMatch(at);
	if (m != 1) { Log(m ? "itemscap: can't read the array setter: off" : "itemscap: the array setter's bytes differ (another game version?): off"); return; }
	if (MH_CreateHook(at, (void*)&HkSetArray, (void**)&o_setArray) != MH_OK || MH_EnableHook(at) != MH_OK) { Log("itemscap: hook failed: off"); return; }
	Log("itemscap: hooked the UI array setter (+%llx)", (unsigned long long)kRvaSetArray);
}

void Itemscap_Command(HMODULE self, const char* rest) {
	Itemscap_Install(self);
	if (!o_setArray) return;
	if (rest && !strncmp(rest, "dump", 4)) { InterlockedExchange(&g_armed, 1); Log("itemscap: the next Items array goes to itemscap.txt (open the inventory)"); }
	else Log("itemscap: %u array sets seen; usage: itemscap dump", g_seen);
}
