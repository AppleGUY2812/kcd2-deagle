# Ready or Not's Niagara impacts, run offline

RoN's bullet impacts are Niagara GPU systems. Their particle numbers are compiled into compute shaders, so we run those
shaders on the CPU and measure what they do. Everything stays on the E: drive (RoN's files are private use).

1. `python3 -I tools/ron/niagara_gpu.py extract ReadyOrNot/Content/ReadyOrNot/VFX/VFXImpacts/Niagara/<NS name> <dir>`
   extracts the package and the chunk's SM5 shader archive (ronx.py), writes `<emitter>.dxbc` / `.asm` (disassembly via
   `tools/ron/dxbc_dis.ps1`, Windows) and `system.json` (layout, curves, renderers, user colours, spawn-burst counts).
2. `python3 -I tools/ron/niagara_gpu.py sim <dir> [seconds] [bursts] [wall]` runs every emitter (tools/ron/sm5emu.py)
   at 60 Hz -> `sim.npy`. `wall` turns the effect so its normal faces +X (a wall facing the camera).
3. `python3 -I tools/ron/fxrender.py <dir> <out.png>` draws a contact sheet (Unreal-style sprites, HDR, filmic curve).
4. `python3 -I tools/ron/ns_stats.py <dir>` prints each emitter in KCD2 particle terms (count, lifetime, size, speed,
   angle off the normal, drag, gravity, HDR colour, curves over life, flipbook or random frame).

Mapping to KCD2 (`src/Libs/Particles/deagle_impacts.xml`): emitter y = the hit normal; Size = half the sprite width in
m; Random is one-sided (Base = max, Random = 1 - min / max); HDR nits x the material's Emissive Intensity / 1000 =
EmissiveLighting, Color = the hue; velocity-aligned streaks = camera-facing + Stretch (total length = 2 x speed x
Stretch). Facts and the engine layout: MODLOG 2026-10-14.
