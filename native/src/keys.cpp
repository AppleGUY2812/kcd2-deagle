// Key presses from Lua through KCD2's input system (keys.h). WHGame 1.5.6, read in the binary 2026-10-20:
//   CKeyboard's symbols are a static array SInputSymbol* Symbol[256] by DirectInput scan code, filled in its init
//     (0x1813D7408: MapSymbol(&Symbol[DIK], DIK, keyId, "name", ...); keyId = DIK - 1 for the main block, w = 16).
//     Found by the init's "escape" mapping: lea rdx, &Symbol[1].
//   SInputSymbol: +0x00 keyId, +0x08 name (const char*), +0x14 state, +0x1C value, +0x24 deviceType (byte).
//   The keyboard posts a key (0x1838350B4 / 0x183835190, CryEngine's SInputSymbol::AssignTo): event.pSymbol = sym,
//     deviceType, modifiers 0, state, value, keyName, keyId; then IInput +0x68 PostInputEvent(event, bForce).
//   PostInputEvent (CBaseInput 0x18085BFE8): returns unless bForce or posting is enabled (+0xD8); sends the event to
//     the listeners (the action maps), then a pressed symbol becomes Down (4) and joins the hold list, which repeats
//     it every frame until a release (2) takes it out (0x18085C264). We post with bForce like the peer asked: the same
//     path the keyboard takes, posting switched off or not.
//   SInputEvent: 0x30 bytes (copied as three 16-byte moves in SendEventToListeners 0x18085C13C).
//   IInput: gEnv holds a CDXInput (found by its RTTI name at run time, the slot is logged).
#include "keys.h"
#include "log.h"
#include <windows.h>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace {
struct SInputEvent {
	uint8_t deviceType; uint8_t pad0[3];
	int32_t state;
	const char* keyName;
	int32_t keyId;
	int32_t modifiers;
	float value; uint32_t pad1;
	void* pSymbol;
	uint8_t deviceIndex; uint8_t pad2[7];
};
static_assert(sizeof(SInputEvent) == 0x30, "SInputEvent is 0x30 bytes in WHGame 1.5.6");
enum { kPressed = 1, kReleased = 2, kDown = 4 };
constexpr int kSymState = 0x14, kSymValue = 0x1C, kSymName = 0x08, kSymDevType = 0x24;
constexpr int kVtPostInputEvent = 0x68;

Module g_mod;
uintptr_t g_env = 0;
uintptr_t g_symbols = 0;   // &Symbol[0]
uintptr_t g_input = 0;     // CDXInput
bool g_inputSearched = false;
struct Held { int dik; bool hold; };
std::vector<Held> g_held;  // keys we pressed and haven't released (main thread)
struct Req { std::string name; int what; };   // what: 0 up, 1 down, 2 hold, 3 allup
std::mutex g_mu;
std::vector<Req> g_reqs;

bool SafeRead(uintptr_t a, void* out, size_t n) {
	__try { memcpy(out, (const void*)a, n); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
uintptr_t Sym(int dik) { uintptr_t s = 0; return g_symbols && SafeRead(g_symbols + dik * 8, &s, 8) ? s : 0; }

int FindDik(const char* name) {   // the symbol whose KCD2 name is this (lowercase, like the keybind files)
	for (int dik = 1; dik < 256; dik++) {
		uintptr_t s = Sym(dik), np = 0; char buf[32] = {};
		if (!s || !SafeRead(s + kSymName, &np, 8) || !np || !SafeRead(np, buf, sizeof(buf) - 1)) continue;
		if (!_stricmp(buf, name)) return dik;
	}
	return 0;
}

uintptr_t FindInput() {   // gEnv's CDXInput: scanned once, its slot logged
	if (g_input || g_inputSearched || !g_env) return g_input;
	g_inputSearched = true;
	for (int off = 0; off < 0x400; off += 8) {
		uintptr_t p = 0, vt = 0;
		if (!SafeRead(g_env + off, &p, 8) || !p || !SafeRead(p, &vt, 8) || !vt) continue;
		if (RttiName(g_mod, vt) == ".?AVCDXInput@@") {
			g_input = p;
			Log("keys: IInput (CDXInput) %p at gEnv+0x%X", (void*)p, off);
			return p;
		}
	}
	Log("keys: no CDXInput in gEnv's first 0x400 bytes");
	return 0;
}

bool CallPost(uintptr_t input, SInputEvent* ev) {
	__try {
		uintptr_t vt = *(uintptr_t*)input;
		auto post = (void(__fastcall*)(uintptr_t, const SInputEvent*, bool))(*(uintptr_t*)(vt + kVtPostInputEvent));
		post(input, ev, true);
		return true;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// what KCD2's keyboard does for a key going down or up: the symbol's state and value, then the event built from it
bool Post(int dik, bool down) {
	uintptr_t s = Sym(dik), input = FindInput();
	if (!s || !input) return false;
	SInputEvent ev = {};
	__try {
		*(int32_t*)(s + kSymState) = down ? kPressed : kReleased;
		*(float*)(s + kSymValue) = down ? 1.0f : 0.0f;
		ev.pSymbol = (void*)s;
		ev.deviceType = *(uint8_t*)(s + kSymDevType);
		ev.state = *(int32_t*)(s + kSymState);
		ev.value = *(float*)(s + kSymValue);
		ev.keyName = *(const char**)(s + kSymName);
		ev.keyId = *(int32_t*)s;
	} __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	return CallPost(input, &ev);
}

int State(int dik) { uintptr_t s = Sym(dik); int32_t st = 0; return s && SafeRead(s + kSymState, &st, 4) ? st : -1; }

bool GameInBackground() {
	HWND fg = GetForegroundWindow();
	DWORD pid = 0;
	if (fg) GetWindowThreadProcessId(fg, &pid);
	return pid != GetCurrentProcessId();
}

bool IsDown(int dik) { int st = State(dik); return st == kPressed || st == kDown; }

void Release(size_t i) {   // only a key still down: KCD2 may have let go of it already (focus loss)
	int dik = g_held[i].dik;
	if (IsDown(dik)) { bool ok = Post(dik, false); Log("keys: dik %d up%s", dik, ok ? "" : " (post FAILED)"); }
	g_held.erase(g_held.begin() + i);
}
}   // namespace

void Keys_Install(const Module& game, uintptr_t env) {
	g_mod = game; g_env = env;
	// the keyboard init's "escape" mapping: mov rbx,rdx; mov [rbp+40h],rax; lea rdx,&Symbol[1]; lea rax,[rbp+40h];
	// mov [r11-40h],r14d; lea r13d,[r14+1]
	if (uintptr_t a = FindUnique(game, "48 8B DA 48 89 45 40 48 8D 15 ?? ?? ?? ?? 48 8D 45 40 45 89 73 C0 45 8D 6E 01"))
		g_symbols = RipTarget(a + 7, 3, 7) - 8;
	Log("keys: CKeyboard Symbol[] %p", (void*)g_symbols);
}

void Keys_Command(const char* rest) {
	char name[32] = {}, act[16] = {};
	int n = sscanf_s(rest, "%31s %15s", name, (unsigned)sizeof(name), act, (unsigned)sizeof(act));
	int what = -1;
	if (n == 1 && !strcmp(name, "allup")) what = 3;
	else if (n == 2) what = !strcmp(act, "up") ? 0 : !strcmp(act, "down") ? 1 : !strcmp(act, "hold") ? 2 : -1;
	if (what < 0) { Log("keys: bad command: key %s (key <name> down|up|hold, key allup)", rest); return; }
	std::lock_guard<std::mutex> lk(g_mu);
	g_reqs.push_back({ name, what });
}

void Keys_Reset() {
	std::lock_guard<std::mutex> lk(g_mu);
	g_reqs.push_back({ "", 3 });
}

void Keys_Frame() {
	std::vector<Req> reqs;
	{ std::lock_guard<std::mutex> lk(g_mu); reqs.swap(g_reqs); }
	for (auto& r : reqs) {
		if (r.what == 3) { while (!g_held.empty()) Release(g_held.size() - 1); continue; }
		int dik = FindDik(r.name.c_str());
		if (!dik) { Log("keys: no key named '%s' (Symbol[] %p)", r.name.c_str(), (void*)g_symbols); continue; }
		size_t i = 0;
		while (i < g_held.size() && g_held[i].dik != dik) i++;
		if (r.what == 0) {
			if (i < g_held.size()) Release(i);
			else Log("keys: %s up: not ours, left alone", r.name.c_str());
			continue;
		}
		// a key already down isn't pressed again: a second press would put it on the hold list twice (0x18085C264)
		bool ok = IsDown(dik) || Post(dik, true);
		Log("keys: %s (dik %d) %s%s", r.name.c_str(), dik, r.what == 2 ? "hold" : "down", ok ? "" : " (post FAILED)");
		if (!ok) continue;
		if (i < g_held.size()) g_held[i].hold = r.what == 2;
		else g_held.push_back({ dik, r.what == 2 });
	}
	// held keys: KCD2 lets go of every key when its window loses focus; in the background ours goes down again
	if (g_held.empty() || !GameInBackground()) return;
	for (auto& h : g_held) {
		if (!h.hold) continue;
		if (IsDown(h.dik)) continue;
		int st = State(h.dik);
		bool ok = Post(h.dik, true);
		static int said = 0;
		if (said++ < 20) Log("keys: dik %d pressed again (state was %d)%s", h.dik, st, ok ? "" : " (post FAILED)");
	}
}
