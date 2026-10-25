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
// Boot 110 (on): menu only ~0.1 s sooner (16.5 s vs 16.6), and the same boot's PSO precache took 28.4 s instead of
// ~1.9 s; boot 111, the same build with it off: 1.9 s again. The query only logs besides its output, so the likely
// cause is an injector in the game folder (OptiScaler / dlss-enabler / fakenvapi hook DXGI) answering differently once
// the renderer is up, and the renderer then getting BugSplat-time adapter data (not checked). At ~0.1 s it isn't worth
// more: OFF by default. The "saved" ms in its lines are the first query's time, not what the later calls cost.
// fastload_gpumemo.txt next to the plugin: 1 on, anything else / no file off (the default). "gpumemo:" lines in the log.
void Gpumemo_Install(uintptr_t gameBase, size_t gameSize, void* selfModule);   // before ApplyHooks("early")
