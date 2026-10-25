#pragma once
#include <cstdint>
// NVIDIA NGX (DLSS) started early, off the render thread (2026-10-25, boot 96: the menu's first frame).
// The DLSS plugin's Init (FUN_182555bb8) calls FUN_181e0b7c0 once, which runs FUN_181dffa40 = NVSDK_NGX_D3D12_Init +
// GetCapabilityParameters (NVIDIA's DLL signature checks and driver-profile re-reads) and keeps the result in +0x8d4 and
// the NGX parameters in +0x8d8. Boot 96: 4.3 s on the render thread in the menu's first frame, Main waiting for it, so
// the menu showed (and its auto load started) 4.3 s late. The init needs only the D3D12 device (a global) and the
// constant app id, so a plugin thread runs the game's own FUN_181dffa40 as soon as the device exists (the render thread
// is idle until the menu); the hook on FUN_181e0b7c0 hands a good result to the DLSS plugin, waiting if it is still
// running. A failed early start is shut down and the game runs its own. Not taken within 120 s (DLSS off): NGX shut
// down again, and the next launch skips the early start until the game starts DLSS itself.
// fastload_ngxearly.txt next to the plugin: 1 (default) on, 0 off. fastload_ngxearly_used.txt is the plugin's own
// record (0 = DLSS not started last launch). "ngxearly:" lines in the log.
void Ngxearly_Install(uintptr_t gameBase, void* selfModule);   // before the start-up hooks go on
