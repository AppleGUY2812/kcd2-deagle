#pragma once
#include <cstdint>
// Gore helpers: list / hide a character's runtime attachments (helmet, hair, beard, hood...), which Lua can't enumerate.
// Commands (from Command() in main.cpp): listatt <entityHex>, hideatt <entityHex> <substr|*head*> <0|1>,
// clothcopy <dst entityHex> [arms,hands|off] [src entityHex] (Henry's worn clothing skins onto another character),
// clothtrace 1|0 (log callers of C_MaterialInstance::GetPackedIds; diagnostic only).
// Returns true when cmd was one of ours. env = SSystemGlobalEnvironment*. Main thread only.
bool GoreCommand(const char* cmd, const char* rest, uintptr_t env);
