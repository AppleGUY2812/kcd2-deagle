// The inventory search's keyboard (invsearch.h): Ctrl+F opens the bar in the inventory movie, and while it is open the
// game's window procedure is subclassed so the typed keys never reach KCD2's action maps. Without that, X (move item),
// I (leave the screen) and the other menu keys would fire on every letter. The bar itself (the matcher and its drawing)
// lives in the movie: inventory/tools/weightmap/search.as.
//
// The game's actions read the keyboard through DirectInput (WHGame imports DirectInput8Create; CryInput's CDXInput),
// which window messages never reach: swallowing WM_KEYDOWN alone let Backspace close the screen and X move an item
// (2026-10-27). So while the bar types, the DirectInput keyboard's key-downs are dropped too (DiBlock below); the text
// still comes from the window's WM_CHAR. Key-ups always pass, so no key the game saw go down sticks.
// Why a window-procedure subclass for the text and not a keyboard hook: WHGame.dll imports no raw-input calls, it runs the message
// loop itself (GetMessage/TranslateMessage/DispatchMessage) and its keyboard is fed by window messages — so a
// WH_KEYBOARD_LL hook would work too, but this way the swallow happens in the game's own thread, after
// TranslateMessage, which gives us the WM_CHAR text (layout, Shift, Caps Lock) for free.
// Alt combinations are never eaten (Alt+F4, Alt+Tab keep working) and they end the typing, as does the screen closing.
#include "invsearch.h"
#include "log.h"
#include <windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include <cstring>
#include "MinHook.h"

namespace {

InvSearchSend g_send = nullptr;
HWND g_hwnd = nullptr;       // KCD2's window
WNDPROC g_old = nullptr;     // what it had before us (the game's own, or another mod's)
WNDPROC g_base = nullptr;    // the first procedure we chained (the game's own): the floor of the chain
int g_depth = 0;             // > 0 inside Proc: another mod's procedure may be chained back into ours
// freeze watch (2026-10-28: the inventory stopped answering after a search while frames kept coming): for 60 s after
// any search step, a line every 2 s says whether keys and the mouse still reach the game (window messages and
// DirectInput events), so the next freeze shows which side stops
volatile LONG g_wKeys = 0, g_wMouse = 0, g_diKbDown = 0, g_diKbUp = 0, g_diMouse = 0;
DWORD g_watchUntil = 0;
void Watch() { g_watchUntil = GetTickCount() + 60000; }
bool g_active = false;       // the bar is open: capture and swallow
bool g_keep = false;         // the bar shows a kept filter: only Backspace is taken (it edits the query)
char g_text[80];
int g_len = 0;
bool g_ctrlF = false;        // the Ctrl+F edge (the bar's open / close key)
volatile DWORD g_graceUntil = 0;   // the Enter / Escape / Ctrl+F that ended typing: its DirectInput key-down is dropped too
DWORD g_rehookAt = 0;        // rate limit for the "someone replaced the procedure" path

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l);

// Nesting guard: if a mod's procedure was installed on top of ours and calls whatever it found (us), the chain is
// ours -> theirs -> ours -> ... The second entry goes straight to the game's own procedure, so it cannot loop.
struct Depth {
	Depth() { ++g_depth; }
	~Depth() { --g_depth; }
};

void Send(const char* cmd) {
	if (g_send && cmd && *cmd) g_send(cmd);
}

void SendText() {
	char cmd[112] = "inv_search_text";   // 15 chars: text at +16, 79 more + NUL fits
	if (g_len > 0) {
		cmd[15] = ' ';
		memcpy(cmd + 16, g_text, (size_t)g_len + 1);
	}
	Send(cmd);
}

bool Accept(char c) {   // what the console command and the matcher can carry: no quotes, brackets or '%'
	return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == ' ' || c == '\'' || c == '-' || c == '.' ||
	       c == ',' || c == '_' || c == '/';
}

void Append(char c) {
	if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
	if (g_len >= (int)sizeof(g_text) - 1 || !Accept(c)) return;
	g_text[g_len++] = c;
	g_text[g_len] = 0;
}

void EditBack() {   // Backspace on a kept filter: the box looks like a text field, so edit it again
	g_keep = false;
	g_active = true;
	if (g_len > 0) g_text[--g_len] = 0;
	Send("inv_search_edit");   // Lua shows the bar again so its text follows the buffer (the next line is the new text)
	SendText();
}

void EndTyping(const char* cmd) {   // Escape / Alt: typing off, the text is dropped
	g_graceUntil = GetTickCount() + 250;
	g_active = false;
	g_keep = false;
	g_len = 0;
	g_text[0] = 0;
	g_ctrlF = true;   // the Ctrl+F that closed it is still down: no toggle back open
	Send(cmd);
}

void EndCommit() {   // Enter / Ctrl+F: typing off, the text stays (the bar shows it as "Filtered: ...")
	g_graceUntil = GetTickCount() + 250;
	g_active = false;
	g_keep = true;      // and Backspace edits it instead of running the game's back action
	g_ctrlF = true;
	Send("inv_search_commit");
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
	Depth depth;
	if (g_depth > 2) return DefWindowProcW(h, m, w, l);   // a chain that is not a chain: stop here
	if (g_depth == 1) {
		if (m >= WM_KEYFIRST && m <= WM_KEYLAST) InterlockedIncrement(&g_wKeys);
		else if (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) InterlockedIncrement(&g_wMouse);
	}
	if (g_depth > 1) return CallWindowProcW(g_base ? g_base : g_old, h, m, w, l);   // see Depth
	if (h == g_hwnd && g_keep && !g_active) {
		// "Filtered: ..." is still on screen: Backspace must edit it, not run the game's back action. Every other key
		// stays the game's (E, X, I ... and Alt+F4), so the inventory is usable while a filter is applied.
		if ((m == WM_CHAR || m == WM_SYSCHAR) && w == 8) return 0;
		if ((m == WM_KEYDOWN || m == WM_SYSKEYDOWN) && (int)w == VK_BACK) {
			EditBack();
			return 0;
		}
		if ((m == WM_KEYUP || m == WM_SYSKEYUP) && (int)w == VK_BACK) return 0;
	}
	else if (g_active && h == g_hwnd) {
		const bool down = m == WM_KEYDOWN || m == WM_SYSKEYDOWN;
		const bool up = m == WM_KEYUP || m == WM_SYSKEYUP;
		if ((down || up) && (GetKeyState(VK_MENU) & 0x8000)) {   // Alt+F4 / Alt+Tab: the game keeps them
			EndTyping("inv_search_cancel");
			return CallWindowProcW(g_old, h, m, w, l);
		}
		if (m == WM_CHAR || m == WM_SYSCHAR) {
			const unsigned c = (unsigned)w;
			if (c >= 0x20 && c < 0x7F) {
				const int before = g_len;
				Append((char)c);
				if (g_len != before) SendText();
			}
			return 0;
		}
		if (down) {
			const int vk = (int)w;
			if (vk == VK_RETURN || (vk == 'F' && (GetKeyState(VK_CONTROL) & 0x8000))) {
				EndCommit();
				return 0;
			}
			if (vk == VK_ESCAPE) {
				EndTyping("inv_search_cancel");
				return 0;
			}
			if (vk == VK_BACK) {
				if (g_len > 0) {
					g_text[--g_len] = 0;
					SendText();
				}
				return 0;
			}
			return 0;   // no game action from a typed letter (its text comes as WM_CHAR)
		}
		if (up || m == WM_DEADCHAR) return 0;
	}
	return CallWindowProcW(g_old, h, m, w, l);
}

bool HasFocus() {
	HWND w = GetForegroundWindow();
	DWORD pid = 0;
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId();
}

HWND GameWindow() {   // the inventory is open only while KCD2 has focus: the foreground window is enough
	HWND w = GetForegroundWindow();
	DWORD pid = 0;
	if (w) GetWindowThreadProcessId(w, &pid);
	return pid == GetCurrentProcessId() ? w : nullptr;
}

void Hook() {   // subclass the game's window; re-do it if another mod put its procedure on top of ours
	HWND h = GameWindow();
	if (!h) return;
	if (g_hwnd && !IsWindow(g_hwnd)) {
		g_hwnd = nullptr;
		g_old = nullptr;
	}
	if (h != g_hwnd) {
		g_hwnd = h;
		g_old = (WNDPROC)SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)&Proc);
		if (!g_old) {
			Log("invsearch: subclassing KCD2's window failed (error %lu)", GetLastError());
			g_hwnd = nullptr;
			return;
		}
		g_base = g_old;
		g_rehookAt = GetTickCount() + 5000;
		Log("invsearch: KCD2's window %p subclassed (the keys of the search bar are taken there)", (void*)h);
		return;
	}
	WNDPROC cur = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
	DWORD now = GetTickCount();
	if (cur != (WNDPROC)&Proc && (int)(now - g_rehookAt) >= 0) {
		g_old = cur;   // chain whatever runs now, ours on top
		SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)&Proc);
		g_rehookAt = now + 5000;
		Log("invsearch: the window procedure was replaced (%p): subclassed again", (void*)cur);
	}
}


// ---- DirectInput: the game's own keyboard (see the top). Both device interfaces (A and W) are hooked; a device's type
// is asked once (GetCapabilities) and kept, so the mouse and pads pass untouched.
using GetDataFn = HRESULT(__stdcall*)(void* dev, DWORD cb, DIDEVICEOBJECTDATA* d, DWORD* n, DWORD flags);
using GetStateFn = HRESULT(__stdcall*)(void* dev, DWORD cb, void* data);
GetDataFn g_getData[2] = {};
GetStateFn g_getState[2] = {};
void* g_devs[16];
bool g_devKb[16];
int g_devN = 0;
volatile LONG g_dropped = 0;

bool IsKeyboard(void* dev) {
	for (int i = 0; i < g_devN; i++) if (g_devs[i] == dev) return g_devKb[i];
	DIDEVCAPS caps = {};
	caps.dwSize = sizeof caps;
	using CapsFn = HRESULT(__stdcall*)(void*, DIDEVCAPS*);
	const bool kb = SUCCEEDED(((CapsFn)(*(void***)dev)[3])(dev, &caps)) && (caps.dwDevType & 0xFF) == DI8DEVTYPE_KEYBOARD;
	if (g_devN < 16) { g_devs[g_devN] = dev; g_devKb[g_devN] = kb; g_devN++; }
	return kb;
}
bool Grace() { return (int)(g_graceUntil - GetTickCount()) > 0; }
bool Blocks(DWORD dik) { return g_active || Grace() || (g_keep && dik == DIK_BACK); }

HRESULT GetData(int k, void* dev, DWORD cb, DIDEVICEOBJECTDATA* d, DWORD* n, DWORD flags) {
	const HRESULT r = g_getData[k](dev, cb, d, n, flags);
	if (SUCCEEDED(r) && d && n && *n && cb >= sizeof(DIDEVICEOBJECTDATA_DX3)) {
		if (!IsKeyboard(dev)) InterlockedAdd(&g_diMouse, (LONG)*n);
		else for (DWORD i = 0; i < *n; i++)
			InterlockedIncrement(((DIDEVICEOBJECTDATA*)((char*)d + (size_t)i * cb))->dwData & 0x80 ? &g_diKbDown : &g_diKbUp);
	}
	if (FAILED(r) || !d || !n || !(g_active || g_keep || Grace()) || cb < sizeof(DIDEVICEOBJECTDATA_DX3) || !IsKeyboard(dev)) return r;
	DWORD out = 0;
	for (DWORD i = 0; i < *n; i++) {
		DIDEVICEOBJECTDATA* e = (DIDEVICEOBJECTDATA*)((char*)d + (size_t)i * cb);
		if ((e->dwData & 0x80) && Blocks(e->dwOfs)) { InterlockedIncrement(&g_dropped); continue; }   // a key-down: dropped
		if (out != i) memmove((char*)d + (size_t)out * cb, e, cb);
		out++;
	}
	*n = out;
	return r;
}
HRESULT GetState(int k, void* dev, DWORD cb, void* data) {
	const HRESULT r = g_getState[k](dev, cb, data);
	if (SUCCEEDED(r) && data && cb == 256 && (g_active || g_keep || Grace()) && IsKeyboard(dev)) {
		if (g_active || Grace()) memset(data, 0, 256);
		else ((unsigned char*)data)[DIK_BACK] = 0;
	}
	return r;
}
HRESULT __stdcall GetDataA(void* dev, DWORD cb, DIDEVICEOBJECTDATA* d, DWORD* n, DWORD f) { return GetData(0, dev, cb, d, n, f); }
HRESULT __stdcall GetDataW(void* dev, DWORD cb, DIDEVICEOBJECTDATA* d, DWORD* n, DWORD f) { return GetData(1, dev, cb, d, n, f); }
HRESULT __stdcall GetStateA(void* dev, DWORD cb, void* data) { return GetState(0, dev, cb, data); }
HRESULT __stdcall GetStateW(void* dev, DWORD cb, void* data) { return GetState(1, dev, cb, data); }

void DiBlockInstall() {
	using CreateFn = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, void*);
	HMODULE dm = GetModuleHandleA("dinput8.dll");
	CreateFn create = dm ? (CreateFn)GetProcAddress(dm, "DirectInput8Create") : nullptr;
	if (!create) { Log("invsearch: no DirectInput8Create: typed keys still reach the game's menu actions"); return; }
	static const GUID iid[2] = { { 0xBF798030, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } },     // IDirectInput8A
	                             { 0xBF798031, 0x483A, 0x4DA2, { 0xAA, 0x99, 0x5D, 0x64, 0xED, 0x36, 0x97, 0x00 } } };   // IDirectInput8W
	static const GUID kb = { 0x6F1D2B61, 0xD5A0, 0x11CF, { 0xBF, 0xC7, 0x44, 0x45, 0x53, 0x54, 0x00, 0x00 } };          // GUID_SysKeyboard
	void* const dataHk[2] = { (void*)&GetDataA, (void*)&GetDataW };
	void* const stateHk[2] = { (void*)&GetStateA, (void*)&GetStateW };
	void* hooked[4] = {};
	int nh = 0;
	for (int k = 0; k < 2; k++) {
		void* di = nullptr;
		if (FAILED(create(GetModuleHandleA(nullptr), DIRECTINPUT_VERSION, iid[k], &di, nullptr)) || !di) continue;
		using CreateDevFn = HRESULT(__stdcall*)(void*, REFGUID, void**, void*);
		void* dev = nullptr;
		if (SUCCEEDED(((CreateDevFn)(*(void***)di)[3])(di, kb, &dev, nullptr)) && dev) {
			void** vt = *(void***)dev;
			void* fns[2] = { vt[10], vt[9] };   // GetDeviceData, GetDeviceState
			void** orig[2] = { (void**)&g_getData[k], (void**)&g_getState[k] };
			void* hk[2] = { dataHk[k], stateHk[k] };
			for (int j = 0; j < 2; j++) {
				bool dup = false;
				for (int q = 0; q < nh; q++) dup |= hooked[q] == fns[j];
				if (dup) {   // A and W share the code: one hook, the second interface's slot uses the same original
					*orig[j] = j == 0 ? (void*)g_getData[0] : (void*)g_getState[0];
					continue;
				}
				if (MH_CreateHook(fns[j], hk[j], orig[j]) == MH_OK && MH_EnableHook(fns[j]) == MH_OK) hooked[nh++] = fns[j];
				else Log("invsearch: couldn't hook the DirectInput keyboard (%s %c)", j ? "GetDeviceState" : "GetDeviceData", k ? 'W' : 'A');
			}
			((IUnknown*)dev)->Release();
		}
		((IUnknown*)di)->Release();
	}
	Log("invsearch: DirectInput keyboard hooked (%d functions): keys typed into the search bar no longer reach the game", nh);
}

}  // namespace

void InvSearch_Install(InvSearchSend send) {
	g_send = send;
	DiBlockInstall();
}

void InvSearch_Frame() {
	Hook();
	static DWORD next = 0;
	static unsigned frames = 0;
	frames++;
	const DWORD now = GetTickCount();
	if ((int)(g_watchUntil - now) > 0 && (int)(now - next) >= 0) {
		next = now + 2000;
		Log("invsearch hb: %u frames; window keys %ld mouse %ld; DirectInput keys down %ld up %ld, mouse %ld; %s%s%s, focus %d",
		    frames, (long)InterlockedExchange(&g_wKeys, 0), (long)InterlockedExchange(&g_wMouse, 0),
		    (long)InterlockedExchange(&g_diKbDown, 0), (long)InterlockedExchange(&g_diKbUp, 0),
		    (long)InterlockedExchange(&g_diMouse, 0), g_active ? "typing" : g_keep ? "kept" : "off",
		    Grace() ? " +grace" : "", g_hwnd && GetWindowLongPtrW(g_hwnd, GWLP_WNDPROC) == (LONG_PTR)&Proc ? "" : " (not subclassed)",
		    HasFocus() ? 1 : 0);
		frames = 0;
	}
	if (!HasFocus()) {
		g_ctrlF = false;
		return;
	}
	const bool d = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('F') & 0x8000);
	if (d && !g_ctrlF && !g_active) {
		Send("inv_search_toggle");   // Lua decides: the bar exists only in the inventory
		Watch();
		Log("invsearch: Ctrl+F: asking Lua to open the search bar");
	}
	g_ctrlF = d;
}

void InvSearch_Command(const char* rest) {
	Watch();
	if (!strcmp(rest, "on")) {
		// the buffer is kept: Lua reopens the bar with the query it already has (no jump when the first key comes)
		g_keep = false;
		g_active = true;
		Log("invsearch: typing on");
	} else if (!strcmp(rest, "keep")) {
		// Enter: typing stops but the bar stays up with the filter, so Backspace can take it back into editing
		g_active = false;
		g_keep = true;
		Log("invsearch: typing off, Backspace still edits the kept filter");
	} else if (!strcmp(rest, "off")) {
		if (g_dropped) Log("invsearch: %ld key presses kept from the game while typing", (long)InterlockedExchange(&g_dropped, 0));
		g_active = false;
		g_keep = false;
		g_len = 0;
		g_text[0] = 0;
		Log("invsearch: typing off");
	}
}

void InvSearch_Reset() {
	g_active = false;
	g_keep = false;
	g_len = 0;
	g_text[0] = 0;
	g_ctrlF = false;
}

bool InvSearch_Typing() { return g_active; }
