#pragma once
#include <cstdint>
// "Ready at the menu" (the 10 s route, docs/LOADTIME.md): once the main menu's preloads are done (quest build, animations,
// terrain meshes) and nobody has touched keyboard or mouse for a while, the newest save's normal load starts by itself and
// ends in the pause menu: Resume = control at once. The load is the game's own "Loading last saved game..." call
// FUN_182ba7c10(C_UISaveLoad), the one the Continue button and wh_sys_AutoLoadLastSave make; the C_UISaveLoad comes from
// a hook on its start-up method FUN_180da1d54 (it shows the main menu). It does not start once a load was started any
// other way, or while the menu's own busy flag (C_UISaveLoad+0x72, Continue's check) is set. At gameplay start after our
// load the pause menu opens ((C_UIMenu+0x58)->vt[1](3), the "open_pause_menu" action's own call).
// fastload_readymenu.txt next to the plugin: "mode idle_s" - mode 0 off (default), 1 on; idle_s 2-120 (default 5): seconds
// with no keyboard or mouse input (any window) before the load starts. `readymenu:` lines in fastload.log.
void Readymenu_Install(uintptr_t gameBase, void* self);
void Readymenu_Frame(int levelState);   // main thread, every frame (fastload_main.cpp Tick)
void Readymenu_Command(const char* args);   // "readymenu [0|1] [idle_s]"
