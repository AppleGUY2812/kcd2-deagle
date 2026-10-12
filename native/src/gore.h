#pragma once
#include <cstdint>
// Gore helpers: list / hide a character's runtime attachments (helmet, hair, beard, hood...), which Lua can't enumerate.
// Commands (from Command() in main.cpp): listatt <entityHex>, hideatt <entityHex> <substr|*head*> <0|1>.
// Returns true when cmd was one of ours. env = SSystemGlobalEnvironment*. Main thread only.
bool GoreCommand(const char* cmd, const char* rest, uintptr_t env);
