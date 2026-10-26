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
// v2 (mode 3, 2026-10-25): v1 still looked at every type in every call (boot 164b1: 1.1 M plain and 1.8 M template looks
// for ~3.7 k changes, 305 ms, inside the static initializers that hold the loader lock until ~0.7 s). The game's rules
// make the changes traceable: a derived name changes only when its raw type's name does; a template's name is re-derived
// only while it is still its compiler name, from its arguments' current names (FUN_1804f2510, arguments at class_data
// +0x98). So v2 keeps, per type, its derived types and the templates using it; a rename marks those, and each pass
// visits only the marked ones, in the list's order (the list is sorted by name: a 256-bit filter over the copy finds
// them), new types (the list grew) included. Templates renamed late in a call mark earlier ones for the next call, as
// the game's full pass would. Checked: the first 8 calls and every 64th run the game's full algorithm instead and count
// any rename v2 had not marked; one turns v2 off for the session (v1 from then on). Mode 4 checks every call.
// fastload_rttrname.txt next to the plugin: 3 v2 (default since queue 171: 1935 of 1935 namings, no miss; launch ->
// menu -0.2 s in queue 170), 1 v1, 0 off, 2 v1 check every skip, 4 v2 checked every call. Needs the plugin loaded before
// WHGame's static initializers (global.ini DontLoadFromDllMain=0). "rttrname:" line in the log.
bool Rttrname_EarlyInstall(uintptr_t gameBase, void* selfModule);   // DllMain, MinHook initialized; true if hooked
void Rttrname_Report();   // Init: one log line with the counts
