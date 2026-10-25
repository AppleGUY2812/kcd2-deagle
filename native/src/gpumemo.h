#pragma once
#include <cstddef>
#include <cstdint>
// The GPU adapter query remembered (launch -> menu work, 2026-10-25). FUN_1805f8c78 (AutoDetectSpec.cpp, "Logging video
// adapters:") makes a DXGI factory, walks every adapter and asks D3D12 / D3D11 for each feature level until one fits,
// then writes the best adapter's DXGI description (0x138 bytes, no pointers) and its feature level. The start-up runs
// it 3 times (boot 106's kcd.log: two lists during BugSplat's init - its "GPU Info" / "GPU Memory" crash-report fields -
// and the renderer's at stamps 0.755-1.137 s, ~0.38 s with the driver already loaded). Its only other effect is the
// log lines. Here the first answer is kept per (r_HeadlessStartup, r_overrideDXGIAdapter), the two settings it reads,
// and later calls with the same settings get it at once. Failed queries are not kept.
// fastload_gpumemo.txt next to the plugin: 1 (default) on, 0 off. "gpumemo:" lines in the log.
void Gpumemo_Install(uintptr_t gameBase, size_t gameSize, void* selfModule);   // before ApplyHooks("early")
