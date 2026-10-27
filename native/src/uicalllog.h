#pragma once
#include <cstddef>
#include <cstdint>
// uicalllog.cpp: logs who calls a menu panel's Activate / Deactivate (a one-off for keepload, 2026-10-27).
void UiCallLog_Install(uintptr_t whgameBase, size_t whgameSize);
