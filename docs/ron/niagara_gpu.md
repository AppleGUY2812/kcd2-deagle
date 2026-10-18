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

5. `python3 -I tools/ron/ron_mat.py <material>...` says what a particle material draws: the parent chain, blend mode,
   parameters set on the way, the base material's parameter defaults (UE 5.3 keeps them unnamed in
   FMaterialCachedExpressionData after the properties; the names are its RuntimeEntries) and the pixel shader of the
   Niagara-sprite permutation, cut to the texture samples and the particle-colour use (`mat_<material>/ps_NNN.asm`).
   Which texture sits in which shader slot is NOT the ReferencedTextures order (Shards_Mat: reversed, DebrisMaster:
   not): read it from what the shader does with each one (a normal map is used as xy x 2 - 1).
6. `python3 -I tools/ron/ns2pfx.py <dir> <effect> --into src/Libs/Particles/deagle_impacts.xml --note '...'` writes the
   KCD2 effect: every number from a run of the shaders (one random seed for all frames, 48 bursts), one material table
   (`MATS` in the script: RoN material -> KCD2 texture and lighting, a missing one stops the run). It replaces the
   effect's block and switches off old layer names. Textures: `tools/ron/mk_ron_fx_tex.py` (the bakes) +
   `tools/fx_impact_textures.ps1`.

Mapping to KCD2 (`src/Libs/Particles/deagle_impacts.xml`): emitter y = the hit normal (RoN: the system's +Z, the
normal on entry, the travel direction on the far side); Size = half the sprite width in m (velocity-aligned: half the
length along the flight, Aspect = width / length); Random is one-sided (Base = max, Random = 1 - min / max); curves
over age are the mean over particles x a per-particle factor; HDR nits x the material's Emissive Intensity / 1000 =
EmissiveLighting, Color = the hue. Alpha (KCD2 Engine/Shaders.pak, Particles.cfi): particle alpha only acts through
AlphaClip (clip = Min + (Max - Min) x alpha; out = saturate(min(tex.a - SourceMin, SourceWidth) x Scale /
SourceWidth), discard <= 0.004), so RoN alpha above 1 goes into Scale, and RoN's masked materials (clip at tex.a x
alpha < 1/3) become a threshold texture 0.004 + 0.996 (1 - 1/(3 tex.a)) with SourceMin 1 -> 0.004: exact.
Facts and the engine layout: MODLOG 2026-10-14 and 2026-10-18.

Emulator limits (2026-10-18): data interfaces are matched to their constants in DataInterfaceInfo order (each spawn
script's ResolvedDataInterfaces names the object); vector fields (VectorField DI), collision queries and mesh
renderers aren't emulated, so gravel's mesh pebbles aren't converted. Vector fields are (Niagara's engine
fields, e.g. TilingCurl32: FFloat16Color voxels, trilinear + wrap in sm5emu); KCD2 can't sample one, so the converter
puts the measured push into Turbulence3DSpeed (not the same paths).
