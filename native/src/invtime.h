#pragma once
// Inventory / map screen timing (WHGame 1.5.6), log only: where the black screen between the key and the room goes.
// Off by default; nothing is hooked until "invtime 1". Lua: "[DN] invtime 1|0" (deagle_invtime).
void Invtime_Command(const char* args);
