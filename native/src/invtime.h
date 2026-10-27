#pragma once
// Inventory / map screen timing (WHGame 1.5.6), log only by default: where the black screen between the key and the room goes.
// Off by default; nothing is hooked until "invtime 1". Modes: 1 = log the steps, 2 = also skip the dummy-clothing gate,
// 3 = 2 + pass the Apse/shop readiness mask (0x81) as 0, 4 = 2 + answer the readiness service "ready" for mask 0x81
// while the screen opens (the observer then fires at once; 3 measured no gain over 2).
// Lua: "[INV] invtime 1|2|3|4|0" (inventory_main.lua's inv_invtime); inventory.asi only.
void Invtime_Command(const char* args);
