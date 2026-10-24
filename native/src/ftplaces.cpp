// The fast-travel arrival's random-event places, loaded ahead. See ftplaces.h.
#include "ftplaces.h"
#include "log.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {
// the game's module singleton (FUN_1809155c8 returns it), as in ftpreload.cpp
constexpr uintptr_t kRvaGame = 0x549d388;
// game+0x128 -> +0x28 fast-travel manager (+0x48 u8 walking; +0x110 ptr, its +0x68 u8 the arrival wait armed)
//            -> +0x30 random-event manager (+0x60/+0x68 vector of Place*: entered while fast travelling)
// game+0x120 -> +0x18 concept manager; +0xb0 its random-event concept streaming (the request's first argument)
// Place: +0x0c u16 reference count (FUN_181e38f20 adds one; the request releases the one it is handed)
constexpr uintptr_t kRvaRequest = 0x51a8e0;   // char (S, Place** slot, u8 sync, u8 notify)
constexpr uintptr_t kRvaAddRef = 0x1e38f20;   // void (Place*)
const unsigned char kRequestBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x83,0xec,0x30,0x41,0x8a,0xf1 };
const unsigned char kAddRefBytes[] = { 0x48,0x89,0x5c,0x24,0x08,0x57,0x48,0x83,0xec,0x20,0x0f,0xb7,0x41,0x0c };
using RequestFn = char(__fastcall*)(void* s, void** slot, unsigned char sync, unsigned char notify);
using AddRefFn = void(__fastcall*)(void* place);

uintptr_t g_base = 0, g_end = 0;
int g_mode = 1;
std::string g_modeFile;
bool g_checked = false, g_codeOk = false;
RequestFn g_request = nullptr;
AddRefFn g_addRef = nullptr;

struct Trip {
	bool on = false;
	bool walked = false;           // the walk flag was seen
	DWORD start = 0, walkEnd = 0;  // GetTickCount
	std::vector<uintptr_t> seen;   // places already noted this trip
	int asked = 0, already = 0, failed = 0;
	size_t maxList = 0;
};
Trip g_trip;
bool g_namesLogged = false;

bool InImage(uintptr_t p) { return p >= g_base && p < g_end; }

uintptr_t Ptr(uintptr_t a) { return a ? *(uintptr_t*)a : 0; }   // callers are inside __try (the plugin builds with /EHa)

// The class name from the game's type info: vtable[-1] -> locator -> type descriptor ".?AVC_Name@ns@@".
bool ClassRaw(uintptr_t obj, char* raw, size_t n) {   // guarded, no C++ objects (MSVC forbids __try next to unwinding)
	__try {
		const uintptr_t vt = Ptr(obj);
		if (!InImage(vt)) return false;
		const uintptr_t col = Ptr(vt - 8);
		const uint32_t td = *(uint32_t*)(col + 12);
		memcpy(raw, (void*)(g_base + td + 0x10), n - 1);
		raw[n - 1] = 0;
		return true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string ClassOf(uintptr_t obj) {
	char raw[96] = {};
	return ClassRaw(obj, raw, sizeof raw) ? std::string(raw) : std::string("?");
}

struct View { bool ok; uint8_t walking, waiting; uintptr_t ftre, stream; uintptr_t* list; size_t n; };

View Read() {
	View v{};
	__try {
		const uintptr_t game = Ptr(g_base + kRvaGame);
		if (!game) return v;
		const uintptr_t sub = Ptr(game + 0x128);
		const uintptr_t ft = sub ? Ptr(sub + 0x28) : 0;
		if (!ft || !InImage(Ptr(ft))) return v;
		v.walking = *(uint8_t*)(ft + 0x48);
		const uintptr_t obs = Ptr(ft + 0x110);
		v.waiting = obs ? *(uint8_t*)(obs + 0x68) : 0;
		v.ftre = Ptr(sub + 0x30);
		if (!v.ftre || !InImage(Ptr(v.ftre))) return v;
		const uintptr_t cm = Ptr(Ptr(game + 0x120) + 0x18);
		v.stream = cm ? cm + 0xb0 : 0;
		const uintptr_t b = Ptr(v.ftre + 0x60), e = Ptr(v.ftre + 0x68);
		if (b && e >= b && (e - b) % 8 == 0 && (e - b) / 8 <= 512) { v.list = (uintptr_t*)b; v.n = (e - b) / 8; }
		v.ok = true;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { v.ok = false; }
	return v;
}

uintptr_t At(const View& v, size_t i) {
	__try { return v.list[i]; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// A place we may hand to the game: its own vtable in WHGame and a sane reference count (it is held by the list).
bool PlaceOk(uintptr_t p) {
	__try { const uint16_t rc = *(uint16_t*)(p + 0x0c); return InImage(Ptr(p)) && rc > 0 && rc < 10000; }
	__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// 0 queued, 1 the game says loaded / streaming off (its return), -1 it threw (our reference may be lost: one place)
int Ask(uintptr_t stream, uintptr_t place) {
	__try {
		void* slot = (void*)place;
		g_addRef((void*)place);                 // the request releases the reference it is handed
		return g_request((void*)stream, &slot, 0, 0) ? 1 : 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

bool CheckCode() {
	g_codeOk = !memcmp((void*)(g_base + kRvaRequest), kRequestBytes, sizeof kRequestBytes) &&
	           !memcmp((void*)(g_base + kRvaAddRef), kAddRefBytes, sizeof kAddRefBytes);
	if (g_codeOk) {
		g_request = (RequestFn)(g_base + kRvaRequest);
		g_addRef = (AddRefFn)(g_base + kRvaAddRef);
	}
	Log("ftplaces: mode %d (%s)%s", g_mode, g_mode == 0 ? "off" : g_mode == 1 ? "log only" : "log + load the arrival's places ahead",
	    g_codeOk ? "" : " - WHGame.dll isn't the 1.5.6 code: log only");
	return g_codeOk;
}

double Since(DWORD t) { return t ? (GetTickCount() - t) / 1000.0 : -1; }
}  // namespace

void Ftplaces_Install(uintptr_t gameBase, HMODULE self) {
	g_base = gameBase;
	auto dos = (IMAGE_DOS_HEADER*)gameBase;
	auto nt = (IMAGE_NT_HEADERS64*)(gameBase + dos->e_lfanew);
	g_end = gameBase + nt->OptionalHeader.SizeOfImage;
	char path[MAX_PATH] = {};
	GetModuleFileNameA(self, path, MAX_PATH);
	std::string p(path);
	g_modeFile = p.substr(0, p.find_last_of('\\') + 1) + "deagle_ftplaces.txt";
	if (FILE* f = fopen(g_modeFile.c_str(), "r")) {
		int m = 1;
		if (fscanf(f, "%d", &m) == 1 && m >= 0 && m <= 2) g_mode = m;
		fclose(f);
	}
}

void Ftplaces_Command(const char* args) {
	while (args && *args == ' ') args++;
	if (args && *args >= '0' && *args <= '2' && !args[1]) {
		g_mode = *args - '0';
		if (FILE* f = fopen(g_modeFile.c_str(), "w")) { fprintf(f, "%d\n", g_mode); fclose(f); }
	}
	Log("ftplaces: mode %d (%s)%s  (ftplaces 0|1|2)", g_mode, g_mode == 0 ? "off" : g_mode == 1 ? "log only" : "log + load ahead",
	    g_checked && !g_codeOk ? " - code checks failed: log only" : "");
}

void Ftplaces_Frame() {
	if (g_mode <= 0 || !g_base) return;
	const View v = Read();
	const bool on = v.ok && (v.walking || v.waiting);
	Trip& t = g_trip;
	if (!on) {
		if (t.on) {
			Log("ftplaces: trip over after %.1f s (%.1f s after the walk ended): %zu places entered (list max %zu), "
			    "%d asked to load ahead, %d already loaded, %d failed", Since(t.start), Since(t.walkEnd), t.seen.size(),
			    t.maxList, t.asked, t.already, t.failed);
			t = Trip{};
		}
		return;
	}
	if (!g_checked) { g_checked = true; CheckCode(); }
	if (!t.on) {
		t.on = true;
		t.start = GetTickCount();
		if (!g_namesLogged) {
			g_namesLogged = true;
			Log("ftplaces: random-event manager %s, concept streaming %s", ClassOf(v.ftre).c_str(),
			    v.stream ? ClassOf(v.stream).c_str() : "-");
		}
		Log("ftplaces: trip starts (walking %d, arrival wait %d), %zu places in the entered list", v.walking, v.waiting, v.n);
	}
	if (v.walking) t.walked = true;
	if (t.walked && !v.walking && !t.walkEnd) t.walkEnd = GetTickCount();
	if (v.n > t.maxList) t.maxList = v.n;
	const bool canAsk = g_mode == 2 && g_codeOk && v.stream && ClassOf(v.stream).find("Concept") != std::string::npos;
	for (size_t i = 0; i < v.n; i++) {
		const uintptr_t p = At(v, i);
		if (!p || std::find(t.seen.begin(), t.seen.end(), p) != t.seen.end()) continue;
		t.seen.push_back(p);
		const char* what = "noted";
		if (canAsk && PlaceOk(p)) {
			const int r = Ask(v.stream, p);
			if (r == 0) { t.asked++; what = "asked to load ahead"; }
			else if (r == 1) { t.already++; what = "already loaded"; }
			else { t.failed++; what = "FAILED"; }
		}
		char when[48] = "during the walk";
		if (t.walkEnd) snprintf(when, sizeof when, "%.1f s after the walk ended", Since(t.walkEnd));
		Log("ftplaces: place %p entered the list (%zu in it), %s: %s", (void*)p, v.n, when, what);
	}
}
