// The inventory search's keyboard (invsearch.h): Ctrl+F opens the bar in the inventory movie, and while it is open the
// game's window procedure is subclassed so the typed keys never reach KCD2's action maps. Without that, X (move item),
// I (leave the screen) and the other menu keys would fire on every letter. The bar itself (the matcher and its drawing)
// lives in the movie: inventory/tools/weightmap/search.as.
//
// Why a window-procedure subclass and not a keyboard hook: WHGame.dll imports no raw-input calls, it runs the message
// loop itself (GetMessage/TranslateMessage/DispatchMessage) and its keyboard is fed by window messages — so a
// WH_KEYBOARD_LL hook would work too, but this way the swallow happens in the game's own thread, after
// TranslateMessage, which gives us the WM_CHAR text (layout, Shift, Caps Lock) for free.
// Alt combinations are never eaten (Alt+F4, Alt+Tab keep working) and they end the typing, as does the screen closing.
#include "invsearch.h"
#include "log.h"
#include <windows.h>
#include <cstring>

namespace {

InvSearchSend g_send = nullptr;
HWND g_hwnd = nullptr;       // KCD2's window
WNDPROC g_old = nullptr;     // what it had before us (the game's own, or another mod's)
WNDPROC g_base = nullptr;    // the first procedure we chained (the game's own): the floor of the chain
int g_depth = 0;             // > 0 inside Proc: another mod's procedure may be chained back into ours
bool g_active = false;       // the bar is open: capture and swallow
bool g_keep = false;         // the bar shows a kept filter: only Backspace is taken (it edits the query)
char g_text[80];
int g_len = 0;
bool g_ctrlF = false;        // the Ctrl+F edge (the bar's open / close key)
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
	g_active = false;
	g_keep = false;
	g_len = 0;
	g_text[0] = 0;
	g_ctrlF = true;   // the Ctrl+F that closed it is still down: no toggle back open
	Send(cmd);
}

void EndCommit() {   // Enter / Ctrl+F: typing off, the text stays (the bar shows it as "Filtered: ...")
	g_active = false;
	g_keep = true;      // and Backspace edits it instead of running the game's back action
	g_ctrlF = true;
	Send("inv_search_commit");
}

LRESULT CALLBACK Proc(HWND h, UINT m, WPARAM w, LPARAM l) {
	Depth depth;
	if (g_depth > 2) return DefWindowProcW(h, m, w, l);   // a chain that is not a chain: stop here
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

}  // namespace

void InvSearch_Install(InvSearchSend send) {
	g_send = send;
}

void InvSearch_Frame() {
	Hook();
	if (!HasFocus()) {
		g_ctrlF = false;
		return;
	}
	const bool d = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState('F') & 0x8000);
	if (d && !g_ctrlF && !g_active) {
		Send("inv_search_toggle");   // Lua decides: the bar exists only in the inventory
		Log("invsearch: Ctrl+F: asking Lua to open the search bar");
	}
	g_ctrlF = d;
}

void InvSearch_Command(const char* rest) {
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
