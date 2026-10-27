#pragma once
// The inventory search bar's typing (invsearch.cpp). KCD2's menus pass Lua no keys, and its action maps would act on
// them (X moves an item, I closes the screen), so the keys have to be taken from the game's own window procedure:
// Ctrl+F asks Lua to open the bar ("inv_search_toggle"), Lua opens it and answers "search on", and from then on every
// key goes into this module's buffer, is sent to Lua ("inv_search_text <text>") and is swallowed.
//   "search on"    start typing (Lua opened the bar; the buffer is kept, so a reopen edits the same query)
//   "search keep"  the bar stays up with a kept filter: only Backspace is taken, and it edits the query again
//   "search off"   stop, drop the text (Lua closed it)
// Enter / Ctrl+F sends "inv_search_commit" (filter kept), Escape "inv_search_cancel" (filter cleared).
using InvSearchSend = void (*)(const char* command);
void InvSearch_Install(InvSearchSend send);   // once, from Init
void InvSearch_Frame();                       // main thread, every frame: find KCD2's window, watch Ctrl+F
void InvSearch_Command(const char* rest);     // Lua: search on|off
void InvSearch_Reset();                       // a load ("hello"): typing off
bool InvSearch_Typing();                      // the bar has the keyboard: the fast-transfer key must not fire
