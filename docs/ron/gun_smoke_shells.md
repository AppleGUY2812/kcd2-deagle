# Ready or Not: gun smoke and shell ejection, number by number (Grizzly / MK-V .50)

What RoN's .50 pistol (`BP_Secondary_LARGrizzly`) does for smoke and spent cases, read from the cooked assets and the
decompiled exe. Written 2026-10-18 for Claude 4 (session kcd2-deagle-4a). Read-only on the RoN install; extracted files
are in `E:\Tools\deagle-work\ron\work\ronout` (textures as PNG in `...\work\smoke_tex\`).

## Source tags
- **DATA**: read from a cooked asset (`tools/ron/dump.py`, `cascade_dump.py`, or a scratch variant of them).
- **CODE**: read from `ReadyOrNotSteam-Win64-Shipping.exe` (Ghidra project on the LAN server, plus the same addresses
  read locally from the installed exe; they match). Addresses are virtual addresses in that exe (build 24942528).
- **UNKNOWN**: not found; see the last section.
- "derived" = simple arithmetic on tagged numbers, shown with the formula.
- A field missing from an asset means "not overridden": the value is the class default from C++. Where that default was
  read from the exe it is tagged CODE; where it is a stock Unreal default that was not read, it says so.

Asset keys used below (all under `ReadyOrNot/Content/` unless the path starts with `Plugins/`):

| Key | Asset |
|---|---|
| G | `Plugins/GameFeatures/ReadyOrNotDLC3/Content/Weapons/Grizzly/Blueprints/BP_Secondary_LARGrizzly` (parent: native `BaseMagazineWeapon`) |
| B | `ReadyOrNot/VFX/VFXBarrel/P_BarrelSmokeTrail` (Cascade) |
| M | `ReadyOrNot/Assets/Particles/WeaponFlash/Particle/Gunsmoke/fx/P_WeaponSmoke_M2011_Muzzle` (Cascade) |
| E | `ReadyOrNot/Assets/Particles/WeaponFlash/Particle/Gunsmoke/fx/P_WeaponSmoke_M2011_Ejection` (Cascade) |
| C | `ThirdParty/BallisticsVFX/Particles/Muzzle/MuzzleSmoke/CO2_Smoke_Small` (Cascade) |
| S | `Blueprints/Items/Projectiles/BulletShell` (parent: native `BaseShell` -> `PooledActor`) |
| P | `Blueprints/Info/BulletShellPool` (parent: native `ObjectPoolBase`) |
| K | `Plugins/GameFeatures/ReadyOrNotDLC3/Content/Weapons/Grizzly/Meshes/50AE_Casing` (StaticMesh) |
| Mesh | `Plugins/.../Weapons/Grizzly/Meshes/SK_PIS_GRIZZLY`, skeleton `.../Grizzly/Shared/SKEL_PIS_GRIZZLY` |
| ini | `ReadyOrNot/Config/DefaultEngine.ini`, `DefaultGame.ini` (from the paks) |

Exe functions used (names are mine, from what the code does):

| Address | What it is |
|---|---|
| `FUN_14497e4b0` | `ABaseMagazineWeapon` constructor (C++ defaults of every heat / shell field) |
| `FUN_1449a71d0` | weapon Tick (vtable `0x1471cca00` slot `0x4f0`, takes DeltaSeconds): heat decay, starts / stops HeatSmoke |
| `FUN_144992310` | fire routine (vtable slot `0x998`): adds heat, fires the rounds, calls the shell spawn, then the fire effects |
| `FUN_144997050` | fire effects (vtable slot `0x9e0`): MuzzleFlash and MuzzleSmoke |
| `FUN_1449a3710` | shell gate: `bSpawnShell`, `SpawnShellDelay` (timer or direct call) |
| `FUN_144997810` | SpawnShell (vtable slot `0x9d8`): takes a shell from the pool, owner-only visibility, calls the init |
| `FUN_1449d27e0` | shell init on every spawn: lifespan, mesh, position, random rotation, impulse, spin, timers |
| `FUN_1449abcf0` | `ABaseShell` constructor |
| `0x1449d9fe0`, `FUN_1449bece0` | shell timers (4.0 s and 0.3 s) |
| `FUN_1438f0040` | `PooledActor` set max lifespan (stores it, starts the expiry timer) |
| `0x1438ed940..9e3` | `ObjectPoolBase` constructor tail (pool defaults) |
| `0x144e590a9..` | `ReadyOrNotGameUserSettings` constructor (shell settings defaults) |
| `0x1431baea7` | Cascade component tick: `DeltaTime *= CustomTimeDilation` |

Field offsets come from the UE reflection tables in the exe (e.g. `HeatPerShot` params at `0x1471c97a0`, offset
`0x1910`). Bool offsets come from each bool's setter function (e.g. `bSpawnShell` setter `0x144798de0` writes byte
`0x1715`).

---

## 1. Smoke

### 1.1 Which smoke the Grizzly has
Three particle components on the gun, plus one on the shell spawn point (DATA, G):

| Component | Template | Attached to | Offset (cm, ItemMesh space; barrel = +Y, up = +Z) | Rotation (pitch, yaw, roll) | Other |
|---|---|---|---|---|---|
| `HeatSmoke` | **B** `P_BarrelSmokeTrail` | ItemMesh | (0, 13.889, 2.567) | none | **CustomTimeDilation 5.0**; tick off at start |
| `MuzzleSmoke` | **M** `P_WeaponSmoke_M2011_Muzzle` | ItemMesh | (-0.0019, 14.727, 1.773) | (0, 90.06, 0) | tick off at start |
| `MuzzleFlash` | `WeaponFlash_Pistol_V3` (see `ron_flash_pistol_v3.md`) | ItemMesh | (0, 13.596, 2.517) | (0, 90, 0) | |
| `ShellParticle` | **E** `P_WeaponSmoke_M2011_Ejection` | `ShellSpawn_Code` | (0, 0, 0) | none | |

- The C++ constructor creates `MuzzleSmoke` and `HeatSmoke` and attaches both to ItemMesh socket `P_MuzzleSmoke`
  (MuzzleFlash to `P_MuzzleFlash`), with auto-activate off (CODE, `FUN_14497e4b0`; `SetAutoActivate(false)` = vtable
  `0x2e0`). Neither socket exists on the Grizzly mesh or skeleton (DATA, Mesh: sockets are `tag_brass`, `tag_muzzle`,
  `tag_mag_*`, `grizzly_laser_socket`, `pointer_socket`), so the offsets above are from the mesh origin.
- `CO2_Smoke_Small` (**C**) is **not** used by the Grizzly. Among the 214 extracted weapon blueprints only `Sniper_M24`
  uses it, as its `HeatSmoke` (DATA, `Blueprints/Items/WeaponsRevised/Sniper_M24`). Its numbers are in 1.6.
- A Niagara copy `NS_WeaponSmoke_M2011_Muzzle` exists in the paks; the Grizzly uses the Cascade one (DATA, G).
- Most RoN guns run `HeatSmoke` at CustomTimeDilation 5.0 (AK102, F90, M16A4, MP5 family, G19, Five-seveN, SR16, UMP45,
  870, ...); M320 uses 6.0; M14 and the breaching shotguns leave it at 1.0 (DATA, `Blueprints/Items/WeaponsRevised/*`).

### 1.2 When each smoke plays (CODE)
**Muzzle smoke (M): every shot.** The fire routine (`FUN_144992310`) calls the fire effects (`FUN_144997050`), which
calls `MuzzleSmoke->ActivateSystem(false)` (vtable `0xa00`) unconditionally. A muzzle attachment with its own smoke
swaps the template first (`SetTemplate`, attachment fields `0x1100` / `0x10f8`).
- Side finding (CODE, same function): the MuzzleFlash only plays when `RandomBoolWithWeight(MuzzleFlashChance)` is true
  (`0x143028b10`); `MuzzleFlashChance` (offset `0x160c`) defaults to **0.65** in the constructor and the Grizzly does not
  override it (DATA, G). So about 65 % of shots flash. Worth checking against `ron_flash_pistol_v3.md`.

**Ejection puff (E): every spawned shell.** The shell init (`FUN_1449d27e0`) calls `weapon->ShellParticle->Activate(true)`
(vtable `0x2c0`). If no shell spawns (pool empty, `bSpawnShell` false), no puff.

**Barrel smoke (B): heat model.** All values are `BaseMagazineWeapon` C++ defaults; the Grizzly overrides none of them
(DATA, G CDO has no `Heat*` field).

| Field | Offset | Grizzly value | Source |
|---|---|---|---|
| HeatPerShot | `0x1910` | **0.4** | CODE `FUN_14497e4b0` (`0x14497e9f3`) |
| HeatThreshold | `0x1914` | **1.0** | CODE same |
| HeatMax | `0x1918` | **2.0** | CODE same |
| HeatDissipation | `0x191c` | **0.2 per second** | CODE same |
| CurrentHeat | `0x1920` | 0 at start | CODE same |
| HeatMinimumTime | `0x1924` | **0.5 s** | CODE same |
| HeatTime | `0x1928` | 0 at start | CODE same |
| bHeatEffectPlayed | `0x192c` | false | CODE same |

On every shot (CODE, `FUN_144992310`, before the rounds are fired):
```
CurrentHeat = clamp(CurrentHeat + HeatPerShot, 0, HeatMax)
HeatSmoke->Deactivate()          // a new shot always stops the barrel smoke
HeatTime = 0
bHeatEffectPlayed = false
```
Every frame (CODE, `FUN_1449a71d0`), while the weapon has an owner:
```
if (weapon is the owner's current item) or (HeatSmoke not activated):
    CurrentHeat = max(CurrentHeat - dt * HeatDissipation, 0)
    HeatTime += dt
    if not bHeatEffectPlayed and CurrentHeat >= HeatThreshold and HeatTime >= HeatMinimumTime:
        HeatSmoke->Activate(true); bHeatEffectPlayed = true      // starts once per pause in firing
    elif CurrentHeat < HeatThreshold and HeatSmoke activated:
        HeatSmoke->Deactivate()
else:                                     // put away while smoking
    HeatSmoke->Deactivate()
```
"Activated" is bit `0x08` of byte `0x599` in the particle component; the component's ActivateSystem (vtable slot `0xa00`,
`FUN_14318c840`) sets it (CODE). With no owner (dropped gun) Tick returns before this, so heat freezes (CODE).

What that means for the Grizzly (derived, assuming shots at the CDO `FireRate` 0.295 s apart, DATA G; decay per gap
0.2 x 0.295 = 0.059):

| Shots in a burst | Heat after last shot | Heat 0.5 s later | Smoke? |
|---|---|---|---|
| 1 / 2 / 3 | 0.40 / 0.74 / 1.08 | 0.30 / 0.64 / 0.98 | no (below 1.0) |
| 4 | 1.42 | 1.32 | yes, starts 0.5 s after the last shot; heat falls under 1.0 at +2.1 s |
| 7 (full mag) | 2.00 (capped) | 1.90 | yes, starts at +0.5 s; heat under 1.0 at +5.0 s |

The particle system is one-shot (all emitters `EmitterLoops` 1, see 1.3), so what you see is decided by the emitter
times below, not by how long heat stays above 1.0.

**Time dilation (CODE + DATA).** The Cascade component tick multiplies its delta time by the component's
`CustomTimeDilation` (`0x1431baea7`: `mulss xmm6, [rdi+0x680]`; field `0x680` = `ParticleSystemComponent.CustomTimeDilation`).
The Grizzly's HeatSmoke has **5.0** (DATA, G). So every time in B runs 5x faster in real time: real seconds = asset
seconds / 5, and per-second rates, velocities and drag act 5x faster (derived).

### 1.3 Barrel smoke B `P_BarrelSmokeTrail` (Cascade)
System (DATA, B): 7 emitters, one LOD (`LODDistances` [0]), no fixed bounds, every emitter `DetailModeBitmask` 15 (all
quality levels), every emitter `EmitterLoops` 1, `EmitterRotation` (-90, 0, 90) (pitch, yaw, roll). With that rotation
the emitter's local +Z points down the barrel (ItemMesh +Y) (derived). No emitter sets `bUseLocalSpace` (world-space
particles; Unreal default false).

Which emitters run. Cascade stores two "enabled" flags: the LOD level's and the Required module's (DATA, B):

| # | Name | LOD level enabled | Required module enabled | Runs? |
|---|---|---|---|---|
| 6 | LingerSmoke | yes | yes | **yes** |
| 3 | SmokeEnd (lit ribbon, sends the "LingerSmoke" events) | yes | **no** | UNKNOWN |
| 1 | HeatDistortion | yes | **no** | UNKNOWN |
| 0 | SmokeStart | no | no | no |
| 2 | SmokeEnd (ink sprite) | no | no | no |
| 4, 5 | SmokeEnd (flipbook ribbons) | no | no | no |

**Emitter 6, LingerSmoke (runs).** Numbers in asset seconds; real time with the Grizzly's dilation 5 in brackets.

| Brick | Value | Source |
|---|---|---|
| material | `MI_WispySmoke` (parent `M_Particle_LitTransparent`, BLEND_Translucent; switch CameraFade on, `CamFadeDistance` 50) | DATA B, `ReadyOrNot/VFX/Materials/MI_WispySmoke` |
| texture | `T_smokeWispy_01_8x4`, 1024 x 512 DXT5 sRGB, **8 x 4 = 32 frames of 128 x 128** | DATA `ReadyOrNot/VFX/Textures/T_smokeWispy_01_8x4` |
| flipbook | frame 0 -> 31 linearly over the particle's life; interpolation Linear_Blend; random U flip | DATA B |
| emitter duration | 8.0 s [1.6 s], delay 0.1 s [0.02 s], 1 loop | DATA B |
| spawn | rate **2 /s** [10 /s] -> 16 particles per activation (derived 2 x 8); plus 1 per "LingerSmoke" event from emitter 3 | DATA B |
| lifetime | 5-8 s [1.0-1.6 s] | DATA B |
| start size | 20-45 cm (X only is used: square sprite) | DATA B |
| size over life (X) | x0.05 -> 0.14 (10 %) -> 0.28 (25 %) -> 0.75 (50 %) -> 1.25 (75 %) -> ~1.73, last sample 2.15 | DATA B (128-sample curve) |
| size by speed | x speed x 0.1, capped at x2 (`SizeScaleBySpeed`) | DATA B |
| start velocity | (0, 0, 1) cm/s world (up) + (0, 0, 10) cm/s emitter-local (down the barrel) [x5 real] | DATA B |
| inherit gun velocity | x0.15 | DATA B |
| follows gun (SourceMovement) | x1.0 at birth -> 0.75 (7.5 % life) -> 0.48 -> 0.37 -> 0.26 -> 0.15 (75 % life, then holds) | DATA B |
| acceleration | random (-1..1, -1..1, 0.5..2) cm/s^2 world | DATA B |
| drag | 0.05 /s world | DATA B |
| rotation | start 0-1 turn (any angle); rate -0.01..0.01 turn/s | DATA B |
| colour | 0.65 grey | DATA B |
| start alpha (by emitter time at birth) | 0 (t 0) -> 0.05 (t 2.67 s) -> 0.027 (t 5.33 s) -> 0 (t 8 s) | DATA B |
| alpha x over life | 0 -> 0.81 (1/7) -> 0.66 -> 0.48 -> 0.36 -> 0.24 -> 0.12 -> 0 | DATA B |
| peak alpha | about 0.04 (0.05 x 0.81, derived): very faint | derived |
| dynamic param 1 | 0.9 -> 1.0 over life (Param2-4 = 1) | DATA B |
| pivot | (0, 0.05) | DATA B |

**Emitter 3, lit ribbon trail (UNKNOWN whether it runs).** Material `M_Ribbon_LitTransparent` (BLEND_Translucent,
two-sided). Duration 8 s, spawn rate **0**: it spawns **only when the gun moves, 1 point per 35 cm** (MovementTolerance 1,
"ignore spawn rate when moving"). Lifetime 1.85 s. Width 22.5 cm; size x0.25 -> 0.8 (50 %) -> 1.2. Start velocity (0, 0, 2)
world + (0, 0, 10) local; acceleration (+-0.5, +-0.5, 2) world; drag 0.025; follows the gun x1 -> 0.2 over life. Colour 0.65;
start alpha by emitter time 0 -> 0.60 (0.8 s) -> 0.5 (2 s) -> 0.2 (4 s) -> 0 (8 s); alpha x over life 0 -> 0.66 (22 %) ->
0.41 -> 0.12 -> 0.02 (90 %). Ribbon: 5 sheets, max 100 points per trail, tiling 50 cm, tessellation step 0.25,
tangent scalar 0.5. Every spawned point fires a spawn event named "LingerSmoke" (emitter 6 adds 1 particle per event).
All DATA, B.

**Emitter 1, HeatDistortion (UNKNOWN whether it runs).** Material `M_HeatDistortion` (BLEND_Additive). Duration 5 s, rate
3 /s, lifetime 1-1.5 s, start size 15-30 cm at emitter time 0 falling to 10-15 cm at 1 s, size x0.15 -> 1.25 over life,
alpha 0.1, velocity (0, 0, 7) world + (0, 0, 10) local, acceleration (+-0.5, +-0.5, 2) world, drag 0.025, follows the gun
x1 -> 0.2. All DATA, B.

**Switched-off emitters (DATA, B), for reference only:** SmokeStart (`M_LightSmoke` 8 x 8, duration 4.5 s, rate 20 /s,
life 2-4 s, size 35-75 cm, 15-30 cm/s along local +Z = down the barrel); SmokeEnd sprite (`M_InkSmoke` 12 x 12, 3 s, 7-15 /s, life 2-4 s, 15-40 cm);
two flipbook ribbons (`M_Ribbon_Flipbook`, 15 /s, life 2.75 s and 2 s, width 15 and 25 cm).

### 1.4 Muzzle smoke M `P_WeaponSmoke_M2011_Muzzle` (Cascade)
System (DATA, M): 7 emitters, 2 LODs (`LODDistances` 0 and 3500 cm; LOD 1 reuses LOD 0's modules), fixed bounds on,
`DetailModeBitmask` 15, every emitter `bUseLegacySpawningBehavior` true, `EmitterLoops` 1, no `bUseLocalSpace` (world
space), no `EmitterDuration` set (Unreal default applies, not read). At LOD 0 emitters 0, 1 ("Puff Left") and 2
("Puff Center") have both flags off: **4 emitters run**.

Frame: emitters 3-5 have `EmitterRotation` (180, 0, 0), which turns their local -X into the component's +X; the component
is yawed 90 deg so its +X is the barrel. So "-X" values below mean **forward out of the muzzle** (derived).

Spawn for all four: **1 particle at t = 0** (burst CountLow 1, Count 1). The spawn module also holds a rate of 1 /s
(DATA); whether that ever adds a second particle near the end of the (default) emitter duration is UNKNOWN.

| Brick | ScreenPuff (#3) | BigPuff (#4) | TinyPuff (#5) | Puff Left (#6) |
|---|---|---|---|---|
| material | `MI_Muzzle_Puff_Front` | `MI_Muzzle_Puff_Front` | `MI_Muzzle_Puff_Front` | `MI_Muzzle_puff_Side` |
| facing | camera (square, X size only) | camera | camera | **along velocity** (PSA_Velocity) |
| lifetime | 0.1-0.2 s | **0.5-1.0 s** | 0.15-0.2 s | 0.25-0.45 s |
| start position | 15 cm forward | 10 cm forward | 5 cm forward | at the component |
| start velocity | 100 cm/s forward | **800 cm/s forward** | 500 cm/s forward | 35 cm/s along local X (emitter roll -90) |
| inherit gun velocity | x0.2 | x0.2 | x0.5 | none |
| drag | 5 /s | 5 /s | 5 /s | 5 /s |
| start size | X 50-60 | X 45-55 | X 8-18 | 8-30 (X = Y = Z, locked) |
| size x over life | X 2.75 -> 3.06 (25 %) -> 3.38 (50 %) -> 4 | X 1.5 -> 3.98 (10 %) -> 5.78 (25 %) -> 7.10 (50 %) -> 7.19 -> 7.0 | X 1 -> 3 (10 %) -> 4.47 -> 5.65 -> 6.82 -> 8 | width x3 -> 6, length x10 -> 12 over the first third of life, then held |
| colour | 0.65 | 0.65 | **0.35** | 0.65 |
| alpha over life | 0.2 -> 0 linear | 0.25 -> 0 linear | 1 -> 0.91 (10 %) -> 0.67 -> 0.34 -> 0.18 -> 0 | 0.5 -> 0.47 (25 %) -> 0.33 -> 0.14 -> 0 |
| flipbook frames (of 32) | 0 -> 31, eased (26 at mid-life) | 0 -> 31, eased (25 at mid-life) | 0 -> 31, eased (23 at mid-life) | 5 -> 31 linear |
| start rotation | 0-1 turn | 0-1 turn | 0-1 turn | fixed 0.5 turn |
| pivot | (-0.06, -0.06) | (-0.03, 0.03) | none | (0, -0.45) |
| UV flip | random U and V | random U and V | random U and V | random U only |
| alpha threshold | 0.025 | 0.025 | 0.025 | 0.01 |

All DATA, M. Rotation-rate and acceleration modules are present but switched off (`bEnabled` false) in all four, so the
puffs do not spin or drift sideways (DATA, M). TinyPuff alone has `bEmitterDurationUseRange` true (DATA).

Materials and textures (DATA):
- `MI_Muzzle_Puff_Front` (`ReadyOrNot/VFX/Materials/`): parent `M_Particle_LitTransparent`, BLEND_Translucent,
  DepthFadeDistance 75, FresnelPower 0.125, FresnelFade off; texture `T_muzzlePuff_Front_01`, **1024 x 512 DXT5 sRGB,
  8 x 4 = 32 frames of 128 x 128**, interpolation Linear_Blend.
- `MI_Muzzle_puff_Side` (object name `MI_Muzzle_Puff_Side`): same parent, BLEND_Translucent, FresnelPower 0.125,
  FresnelFade on; texture `T_muzzlePuff_Side_01`, **1024 x 1024 DXT5 sRGB, 8 x 4 = 32 frames of 128 x 256** (tall cells).

The three switched-off emitters (DATA, M): "Puff Left" x2 and "Puff Center", `MI_Muzzle_Puff_Side_01/02` from
`Gunsmoke/data/` (4 x 4 sheets). Beyond 35 m (LOD 1) their LOD flag is on but the Required flag is off, the same open
question as 1.3; it does not matter in first person.

### 1.5 Ejection puff E `P_WeaponSmoke_M2011_Ejection` (Cascade)
One emitter "Puff Left", both flags on, 2 LODs (0 / 3500 cm, same modules), world space, `bUseLegacySpawningBehavior`
true, `EmitterLoops` 1, `bEmitterDurationUseRange` true (DATA, E). It sits on `ShellSpawn_Code` (1.1), so its +X is the
shell's eject direction (2.3).

| Brick | Value |
|---|---|
| material | `MI_Muzzle_puff_Side` (see 1.4; 8 x 4 frames of 128 x 256) |
| facing | along velocity (PSA_Velocity) |
| spawn | 1 at t = 0 (spawn module rate 1 /s too, see 1.4) |
| lifetime | 0.1-0.25 s |
| velocity | cone 0-0.1 around +X (Cascade unit: 1 = 180 deg, so 0-18 deg), speed 25-75 cm/s |
| drag | 5 /s world |
| start size | 5-10 (locked X = Y = Z) |
| size x over life | width x0.5 -> 1.5 (10 %) -> 2.1 (25 %) -> 2.4 -> 2.7 -> 3; length x1 -> 3 -> 4.2 -> 4.8 -> 5.4 -> 6 |
| colour / alpha | 0.65 grey; alpha 0.75 -> 0.55 (10 %) -> 0.35 (25 %) -> 0.24 (50 %) -> 0.08 (75 %) -> 0 |
| flipbook | frames 8 -> 31 of 32, eased (26 at mid-life); random U flip |
| start rotation | -0.4..0.4 turn |
| pivot | (0, 0.075) |

All DATA, E.

### 1.6 CO2_Smoke_Small C (M24 only; requested, not used by the Grizzly)
One emitter "Trail", both flags on, LODs 0 / 3500 cm (LOD 1 differs only in acceleration: (-200..200, -50..-100,
75..300)), world space, `bUseLegacySpawningBehavior` true (DATA, C).
- Material `MuzzleFlash_Master_Inst` (parent `MuzzleFlash_Master`, **BLEND_Additive**; Fade Distance 255, Hardness 0.882,
  Opacity 0.1, Radius 55); texture **`MuzzleSmokeOne_1024_11x11`, 1024 x 1024 DXT5 sRGB, 11 x 11 = 121 frames of about
  93 x 93 px** (DATA, `ThirdParty/BallisticsVFX/Particles/Muzzle/Textures/`).
- Emitter duration random 0.1-0.3 s (`EmitterDuration` 0.1, `EmitterDurationLow` 0.3, range on), **12 loops**; burst
  exactly 1 at t = 0 of each loop (CountLow -1, Count 1); spawn rate 1 /s; spawn-per-unit (5 per 100 cm) present but off.
- Lifetime 0.5-1 s; start size X 10 (Y 250-600, Z 250-600 unused by a square sprite); size x 0.1 -> 0.92 (10 %) -> 2.4
  (25 %) -> 3.75 (50 %) -> 4.66 (75 %) -> 5; velocity (50..100, -10..10, -10..10) cm/s; inherit gun velocity x0.2;
  acceleration (-100..100, -50..-100, 10..100) cm/s^2 world; drag 1 /s; colour 0.369 grey; alpha 0.2 -> 0; frames
  1 -> 63 linear; start rotation -0.4..0.4 turn, rate -0.2..0.2 turn/s fading to x0.15 by 31 % of life.

All DATA, C.

---

## 2. Shells

### 2.1 What the Grizzly sets (DATA, G, unless marked)
| Field | Value | Source |
|---|---|---|
| ShellClass | `BulletShell_C` (S) | DATA G |
| ShellMesh | `50AE_Casing` (K) | DATA G |
| ShellBounceFMODAudio | `/Game/FMOD/Events/Weapons/Shared/9mmShell` | DATA G |
| ShellSpawn | `ShellSpawn_Code`: ItemMesh offset (-1.029, -1.711, 4.207) cm, rotation **(pitch 40.0, yaw 180.0, roll 0)** | DATA G |
| ShellParticle | `ShellParticle` component (= the ejection puff E, 1.5) | DATA G |
| bSpawnShell | true (constructor; Grizzly does not override) | CODE `FUN_14497e4b0`, byte `0x1715` |
| SpawnShellDelay | **0** (constructor; not overridden) -> shell spawns in the same frame as the shot | CODE same, `0x1718` |

For comparison (DATA, `WeaponsRevised/*`): G19 `SpawnShellDelay` 0.015 s; B1301 / Benelli 0.01 s; 870 / S590 0.4 s;
Supernova / W870 0.5 s; M24 0.6 s (pump / bolt guns delay the case to the cycling animation).

Eject direction (derived from the rotation above): the shell flies along `ShellSpawn_Code`'s +X, which in ItemMesh space
is (cos 40 x cos 180, 0, sin 40) = **(-0.766, 0, 0.643)**: 90 deg to the barrel, toward mesh -X (the right side when the
barrel points +Y in Unreal's left-handed frame), **40 deg above horizontal**.

### 2.2 Spawn path (CODE)
1. Fire routine `FUN_144992310` calls `FUN_1449a3710(weapon, true)` after firing the rounds.
2. `FUN_1449a3710`: if `bSpawnShell` is false, nothing. If `SpawnShellDelay` == 0, call SpawnShell now; if > 0, set a
   one-shot timer of `SpawnShellDelay` s that calls it.
3. SpawnShell `FUN_144997810(weapon, firstPerson, thirdPerson)`: the fire path calls it as (true, false), which only
   proceeds when the owner exists and the owner's check at owner-vtable `0xbc8` is true (by context the locally
   viewed first-person case; see Not found); (false, true) returns early when that check is true. The
   `Multicast_SpawnShell` exec (`0x14479b410`) also calls this vtable slot.
   - `ShellClass` must be a `BaseShell`; its `ShellType` (CDO byte `0x300`; 0 = Bullet, 1 = Shotgun) picks the pool:
     **"Bullet Shell Pool"** or "Shotgun Shell Pool".
   - Takes an actor from that pool. If none comes back, no shell (and no ejection puff).
   - First-person call: `ShellMesh->SetOnlyOwnerSee(true)` (`0x142dc97c0`, matched to the `SetOnlyOwnerSee` UFUNCTION
     thunk): **only the shooter sees his own first-person shells**.
   - Adds the actor tag "Shell", passes the weapon's owner (weapon field `0x148`) to `FUN_142b4f620` (probably
     SetOwner; not matched), runs the init (2.3), then sets the material parameter **`DisableWeaponFOV` = 0.0** on
     every dynamic material of the shell (`MID_ShellMesh`, shell `0x2f0`).

So the shell is never a particle: in first person and third person alike it is a pooled physics actor (static mesh).
The component named `ShellParticle` is the ejection-port smoke puff.

### 2.3 Init on every spawn `FUN_1449d27e0(shell, weapon)` (CODE)
In order:
1. Lifespan = max(`ShellLifeSpanPC`, 0) -> `PooledActor` max lifespan (`FUN_1438f0040`), which starts a one-shot timer.
   `ShellLifeSpanPC` = **6.0 s** (CODE, `ABaseShell` constructor `FUN_1449abcf0`, offset `0x318`). It is a config
   property; no RoN ini sets it (DATA: `DefaultGame.ini`, `DefaultEngine.ini`, and the user's `Saved/Config/Windows/*.ini`
   have no `ShellLifeSpanPC`).
2. Clears `bReturnToPoolAfterPhysicsHit` (byte `0x2c4` bit 2): on PC a shell is not recycled when it hits something.
3. Clears both shell timers, `SetSimulatePhysics(false)`, `SetStaticMesh(weapon.ShellMesh)` (Grizzly: 50AE_Casing), copies
   the weapon's `ShellBounceFMODAudio` (weapon `0x1720`) to the shell (`0x308`), copies a world time value into the
   shell (`0x160`), then `SetSimulatePhysics(true)`.
4. Base speed: `v_side = dot(owner.GetVelocity(), owner.RootComponent.GetRightVector())` (the owner's sideways speed;
   (0, 1, 0) if the owner has no root, 0 if no owner). `base = max(v_side, 200)` cm/s (constant `200.0` at
   `0x14651d0c4`).
5. `SetActorLocation(ShellSpawn_Code world location)`, no sweep.
6. Adds a random rotation to the shell's current rotation: **pitch = integer -10..+10 deg, yaw = integer -10..+10 deg,
   roll 0** (two `rand()` draws, each `RandRange(0, 20) - 10`; scale `21/32767` at `0x14751a628`; the add goes through
   `FUN_142b38870` -> `0x142de53d0`, a scene-component rotation add). The shell is not first aligned to the port: it keeps
   whatever rotation the pooled actor had and gets this random nudge.
7. `SetWorldScale3D(weapon actor scale)`.
8. **Linear kick: `AddImpulse(dir x base x U(1, 3), bVelChange = true)`** (vtable `0x678`, matched to the `AddImpulse`
   thunk). `dir` = forward vector of `ShellSpawn_Code`'s world rotation (2.1). `U(1, 3)` = `rand()` x 2/32767 + 1
   (constants `0x14654cda0` = 2/32767, `0x14643f8c4` = 1.0). So a standing shooter ejects at **200-600 cm/s**
   (derived); a shooter strafing right faster than 200 cm/s raises the base to his sideways speed.
9. **Spin: `AddAngularImpulseInRadians((r, r, r), bVelChange = true)`** with **r = one integer 0..360**, the same on all
   three axes (vtable `0x680`; the Degrees thunk converts with `mulpd` before calling the same slot, the Radians thunk does
   not, so `0x680` is the radians version; scale `361/32767` at `0x14751a634`). That asks for up to 360 rad/s per axis;
   the physics engine's max angular velocity clamps it (RoN's `DefaultEngine.ini` `[/Script/Engine.PhysicsSettings]` does
   not set `MaxAngularVelocity`; the engine default was not read: UNKNOWN).
10. `weapon->ShellParticle->Activate(true)`: the ejection puff (1.5).
11. Timer A, one-shot **0.3 s** (`0x14645feec`) -> `FUN_1449bece0`: sets **`DisableWeaponFOV` = 1.0** on every dynamic
    material of the shell mesh. So for the first 0.3 s the shell is drawn with the first-person weapon FOV (like the
    view model) and after that with the world FOV.
12. Timer B, one-shot **4.0 s** (`0x146460108`) -> `0x1449d9fe0`: `SetSimulatePhysics(false)`: the shell freezes where it
    lies.
13. At **6.0 s** (step 1) the `PooledActor` lifespan timer calls `OnLifeSpanExpired` (`0x1438efb10`), whose native body
    (`0x1438ef950`) calls the event `PooledActor_EndPlay`: the shell goes back to the pool. (Pool hand-out calls
    `PooledActor_BeginPlay`, `0x1438efb50`.)

### 2.4 Shell actor and physics
| Item | Value | Source |
|---|---|---|
| actor | `BaseShell` -> `PooledActor` -> `Actor`; root = StaticMeshComponent "Shell" | CODE `FUN_1449abcf0`; DATA S |
| simulate physics | on (constructor), then off/on per spawn (2.3) | CODE `FUN_1449abcf0`, `FUN_1449d27e0` |
| collision profile | **"Item"** (constructor `SetCollisionProfileName`, vtable `0x6f8`, name string at `0x1469c1e30`) | CODE |
| "Item" profile | QueryAndPhysics, object type Item; ignores Pawn, Visibility, Camera, PhysicsBody, Vehicle, Destructible, Projectile, Item, Door, Cover, Doorway, Yellout, BulletEffect, Sound; blocks the rest (WorldStatic / WorldDynamic) | DATA ini (`DefaultEngine.ini` line 602) |
| extra response | Projectile channel (GameTraceChannel1) -> Ignore (`SetCollisionResponseToChannel(14, 0)`, vtable `0x908`) | CODE `FUN_1449abcf0`; channel name DATA ini |
| hit notify | on (`SetNotifyRigidBodyCollision(true)`, vtable `0x6e8`) | CODE `FUN_1449abcf0` |
| overlap events | off (`SetGenerateOverlapEvents(false)`, `0x142dc88a0`) | CODE `FUN_1449abcf0` |
| linear damping | **1.0** | DATA S (`Shell.BodyInstance`) |
| angular damping | **1.0** | DATA S |
| mass | not set anywhere RoN-side (no mass override in S or K); the engine derives it from the collision volume | UNKNOWN |
| shadows | `bCastDynamicShadow` false; lightmap ForceVolumetric; lighting channels: channel 0 off, channel 1 on | DATA S |
| default mesh / sound in S | `9mm_Casing`, `762Shell` (both replaced per spawn by the weapon's) | DATA S |
| ShellNormalizeMax / Min | 26.9 / 0.5 (purpose not traced; likely the bounce-sound range) | DATA S |
| ShellType | Bullet (0; constructor default, S does not override) | CODE `FUN_1449abcf0` |
| actor fields `0x3a` / `0x3c` | constructor clears 2 bits of byte `0x3a` and sets float `0x3c` = 9999.0 (inside the actor's tick settings; field names not identified) | CODE `FUN_1449abcf0` |

The .50 AE case mesh K (DATA): material `MI_Shell_Casing_45ACP`; render bounds origin (0, -0.095, 1.878), half-extent
(0.807, 0.807, 1.907) cm; collision = **one convex hull of 16 points, 1.83 x 1.81 x 4.00 cm** (X -0.92..0.91,
Y -1.00..0.81, Z -0.13..3.875), volume **11.19 cm^3** (derived, convex hull of the stored points); the asset's
BodySetup default profile is BlockAll (the component's "Item" profile is what applies).

### 2.5 Pool and how many live at once
| Item | Value | Source |
|---|---|---|
| pool manager | "Bullet Shells" (holds `BulletShellPool` and `ShotgunShellPool`), enabled | DATA ini (`DefaultGame.ini` line 252, `[/Script/ObjectPooler.ObjectPoolerSettings]`) |
| pool name | "Bullet Shell Pool" | DATA P; CODE `FUN_144997810` |
| pooled class | `BulletShell_C` | DATA P |
| **PoolSize (PC)** | **1024** | DATA P |
| PS5 | PoolSize 1024 | DATA P |
| PS4, Xbox One | ActorMaxLifetime 5.0 s, return to pool after a physics hit | DATA P |
| ObjectPoolBase defaults | PoolSize 100, ReuseSetting 0 = `Reuse`, bFillPoolOnBeginPlay true | CODE `0x1438ed98a..0x1438ed9d4` |
| ReuseSetting (P) | not overridden -> `Reuse` (enum: Reuse, DoNotReuse, CreateNewActor) | DATA P + CODE |
| shell life on PC | 6.0 s total; physics frozen at 4.0 s | CODE 2.3 |

So on PC the number of live Grizzly shells is "shots fired in the last 6 s", capped by the 1024 pool (derived; the
Grizzly's 7 rounds never get near it). What `Reuse` does when all 1024 are out was not traced (UNKNOWN).

User settings: `ReadyOrNotGameUserSettings.MaxShellsInWorld` (offset `0x254`) and `MaxShellLifeTime` (`0x258`) default
to **100** and **300.0 s** (CODE, constructor at `0x144e59109` / `0x144e59113`); the user's own
`GameUserSettings.ini` holds the same 100 / 300. No native code reads them: the only readers are the Blueprint
functions `LoadMaxShellsInWorld` / `GetShellLifetime` (CODE: Ghidra refs to `0x144deb1b0` and `0x144ddc770` are only
their exec thunks). Which Blueprint applies them, and how, is UNKNOWN; the native shell path above uses 6 s and the
pool size.

---

## 3. Not found
- **Two barrel-smoke emitters**: whether UE 5.3 runs an emitter whose LOD level is enabled but whose Required module is
  disabled (B #3 lit ribbon, the only source of "LingerSmoke" events, and B #1 HeatDistortion). Not resolved in the exe.
  `tools/ron/cascade_sim.py` assumes the Required flag rules (so it treats both as off).
- Cascade `EmitterDuration` where the asset leaves it unset (M, E): the engine default was not read, so whether M/E's
  1 /s spawn rate adds a second puff near the end of the emitter is unknown.
- Shell mass (engine-computed from the 11.19 cm^3 hull and the physical material; neither RoN asset sets it) and the
  physics max angular velocity that clamps the up-to-360 rad/s spin.
- Which Blueprint applies `MaxShellsInWorld` (100) / `MaxShellLifeTime` (300 s), and what pool `Reuse` does when the pool
  is exhausted.
- The exact meaning of owner check vtable `0xbc8` (first-person vs third-person shell path) and of `ShellNormalizeMax/Min`.
- The name of the particle-component flag bit `0x599 & 0x08` (behaves as "system was activated").
- Whether the rotation add (step 6) is world-space or local-space (both start from the pooled actor's old rotation).
- Material graphs (`M_Particle_LitTransparent`, `M_HeatDistortion`, `MuzzleFlash_Master`): how DepthFade, Fresnel,
  CameraFade and the dynamic parameter are used was not decoded.
