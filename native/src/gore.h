#pragma once
#include <cstdint>
// Gore helpers: list / hide a character's runtime attachments (helmet, hair, beard, hood...), which Lua can't enumerate.
// Commands (from Command() in main.cpp): listatt <entityHex>, hideatt <entityHex> <substr|*head*> <0|1>,
// clothcopy <dst entityHex> [arms,hands|off] [src entityHex] (Henry's worn clothing skins onto another character),
// clothtrace 1|0 (log callers of C_MaterialInstance::GetPackedIds; diagnostic only),
// cloth 0|1|status and clothsync [dst entityHex] [src entityHex] (Henry's worn clothing on a Henry-arms viewmodel).
// Returns true when cmd was one of ours. env = SSystemGlobalEnvironment*, vmId = the viewmodel entity. Main thread only.
bool GoreCommand(const char* cmd, const char* rest, uintptr_t env, uint32_t vmId);
// Henry's worn clothing on the viewmodel's arms: a cheap check of what he wears (a few dozen pointer reads), and a
// sync of the viewmodel's pieces only when that changed. Call it a few times a second on the main thread.
void ClothTick(uintptr_t env, uint32_t vmId);
void ClothReset();   // a new viewmodel: sync on the next frame, not up to 29 frames later (bare arms in the draw clip)
bool ClothPending();
void ClothKick();    // look again on the next frame (armpose went on, off, or stopped applying)
