#pragma once
#include <cstdint>
// rttr's custom-name registration made fast (launch -> menu work, 2026-10-25). WHGame's static initializers give ~6,500
// types their names through FUN_1804f353c (rttr type_register_private::register_custom_name). After naming one type it
// copies the whole type list and, for every non-raw type (pointers, wrappers ...), re-derives its name from its raw
// type's name (FUN_1804f3cbc) and hands it to the name update (FUN_1804f1dd8), which does nothing when the name is the
// same. So every call rebuilds thousands of strings that almost never change: boot 113's early sampler had 84 of its 108
// start-up samples (~1.3 s of 1.67 s) inside it.
// Here the same function, with one change: for types that are not wrappers (flag 4 clear) the derived name is a pure
// function of the type's compiler name, its raw type's compiler name and the raw type's current name. Each type's last
// derivation is remembered with those inputs; when they are unchanged and the type still has that name, the derivation
// would give the same string and the update would do nothing, so both are skipped. Everything else (wrappers, the
// template-instance pass, the order, the locks) calls the game's own functions as before.
// Checked: the first 4,000 skips, then every 64th, also derive the game's way and must give the remembered string;
// one mismatch turns the memory off for the session (the full game work runs again from then on).
// fastload_rttrname.txt next to the plugin: 1 on (default), 0 off, 2 check every skip. Needs the plugin loaded before
// WHGame's static initializers (global.ini DontLoadFromDllMain=0). "rttrname:" line in the log.
bool Rttrname_EarlyInstall(uintptr_t gameBase, void* selfModule);   // DllMain, MinHook initialized; true if hooked
void Rttrname_Report();   // Init: one log line with the counts
