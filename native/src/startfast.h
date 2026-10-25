#pragma once
#include <cstdint>
// Start-up pieces (launch -> main menu), user 2026-10-24: "just put me in the game".
// Controller list: the game's input init (FUN_180da1fcc, "Initializing DirectInput") registers its gamepad callback with
// IGameInput::RegisterDeviceCallback(..., GameInputBlockingEnumeration, ...): Main waited 1.3 s in it on boot 81 (inside
// the Steam overlay's hook), with the render thread idle. Mode 1 asks for GameInputAsyncEnumeration instead (the same
// callbacks, from GameInput's own thread). The game's callback adds / removes the device in the input system with no
// lock, so every callback is queued and handed to the game on the main thread at the next frame tick (also hot plugs).
// fastload_startfast.txt next to the plugin: 1 (default) on, 0 the game's own. "startfast:" lines in the log.
void Startfast_Install(uintptr_t gameBase, void* selfModule);   // before the game's input init (~8 s after the plugin)
void Startfast_TickReady(bool ok);   // fastload_main.cpp: the frame tick is patched (true) or failed (false)
void Startfast_Frame();              // the main thread, once a frame
