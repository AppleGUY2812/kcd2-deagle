# Ready or Not's pistol muzzle flash, brick by brick (`WeaponFlash_Pistol_V3`)

The Grizzly (`BP_Secondary_LARGrizzly`, RoN's .50 pistol) fires `WeaponFlash_Pistol_V3`, a **Cascade** particle system
(not Niagara). Everything below is read from the game's cooked package with `tools/ron/cascade_dump.py` (recipe 9/10a
in docs/ron_extraction.md); the raw dump is the appendix. Owner: Claude 1, 2026-10-18.

## How to read the values
- Units: cm, seconds, cm/s. The emitters are **local space** (glued to the muzzle), **+X = down the barrel**.
- `a/b` on a scalar = random between a and b, picked once per particle. `x/y/z/X/Y/Z` on a vector = random between
  (x,y,z) and (X,Y,Z).
- `... (TimeScale 1)` = a curve over the particle's life, sampled from start to end (two samples = start → end).
- **Careful:** `cascade_dump.py` doesn't print a distribution's type. Several `a/b` values below are **uniform random**
  (Op 2 in the raw table) and the module's Update re-draws them **every frame** (no random stream), not a curve:
  corrected per brick from the raw tables and RoN's code (docs/ron/cascade_semantics.md, 2026-10-18).
- Colours are **HDR, linear**: 38,250 means ~38,000× white, i.e. it blows out to pure white under bloom.
- `Spawn bursts (CountLow, Count, Time)`: (-1, 1, 0) = exactly 1 particle at t = 0. The spawn *rate* never runs: the
  emitter's duration is 0, so the bursts are all there is.
- `DetailModeBitmask` 15 = shown at every quality setting, 12 = High/Epic only.

## The system
- Fixed bounds ±330 cm (no per-frame bounds work). 7 emitters, all spawned on the shot, all dead by **0.085 s**.

## Emitter 1: Pop, front (the main flash + the main LIGHT)
| brick | value |
|---|---|
| material | `MI_MuzzleFlash_SMG`: additive, texture `Rifle_MuzzleFlash_Macro_Rifle_-_Copy__2_` (4×4) |
| spawn | 1 at t = 0 (count low 0: 1 always) |
| lifetime | **0.04 s** |
| start position | 25 cm in front of the muzzle |
| velocity | 250 cm/s forward |
| size | 25-35 cm (uniform) |
| size over life | ×1 → ×1.5 (x, y), ×1 → ×2 (z) |
| rotation | random 0-25 (Cascade units of full turns: any angle) |
| sprite frames | **a random frame 0-23, re-drawn every frame** (truncated, no blending; the sheet has 16, and what 16-23 show is decided on the GPU: unverified) |
| colour | 38,250 / 450 / 200 (orange-white, blown out) |
| alpha over life | 1 → 0 |
| **light** | rolled once at spawn: on **80 %** of shots. Colour = particle colour × alpha × colour scale × brightness; brightness **random 70-80** and colour scale **one grey random 10-25**, both re-drawn every frame. Radius = 100 × (size x + size y) / 2 of the current size: **25-35 m at spawn, up to ~52 m as it grows**. Exponent 320 (very hard fall-off). Lives exactly as long as the particle; lights translucency too |
| event | sends "FlashCore" (emitter 2 listens) |
| quality | High/Epic |

## Emitter 2: Pop, side (spawned by emitter 1's event)
| brick | value |
|---|---|
| material | `MI_MuzzleFlash_Side`: additive, `T_muzzle_flashes` (2×2), **1 random frame** |
| spawn | 1 per "FlashCore" event (none on its own) |
| lifetime | 0.04 s |
| velocity | 5 cm/s forward |
| size | 20-30 cm |
| size over life | (×0.12, ×0.5) → (×1, ×2.5); z ×1 → ×2 |
| facing | stretched along its velocity (PSA_Velocity); pivot −0.35 (grows out of the muzzle) |
| colour / alpha | 38,250 / 450 / 200; alpha 1 → 0 |

## Emitter 3: Pop, pistol core (second LIGHT)
| brick | value |
|---|---|
| material | `MI_MuzzleFlash_Pistol`: additive, `MuzzleFlash_Macro_M2011` (4×4) |
| spawn | 1 at t = 0 |
| lifetime | **0.035 s** |
| start position | 12 cm in front |
| velocity | 100 cm/s forward |
| size | 8-12 cm; over life ×1 → ×1.5 (z ×1 → ×1.2) |
| rotation | random |
| sprite frames | **a random frame 0-11, re-drawn every frame** |
| colour | **random per channel, every frame**, between 0.85 / 0.07 / 0.02 and 38,250 / 450 / 200 (R 0.85-38,250, G 0.07-450, B 0.02-200), not a ramp |
| alpha | 1 → 0 |
| **light** | 80 % of shots (own roll); brightness **random 15-50 every frame**; grey colour scale random 10-25 every frame; colour = particle colour (itself random) × alpha × those; radius = 100 × size: 8-12 m at spawn, up to ~18 m; exponent 16 (soft) |
| quality | every setting |

## Emitter 4: SmallFlame (side tongues)
| brick | value |
|---|---|
| material | `MI_MuzzleFlash_Side` (2×2), 1 random frame |
| spawn | 1 at t = 0 |
| lifetime | 0.035 s |
| velocity | 25 cm/s forward |
| size | 15-20 cm; over life (×0.25, ×0.5) → (×0.75, ×1.5); z ×1 → ×2 |
| facing | along velocity; pivot −1.0 (base at the muzzle) |
| colour / alpha | **382.5 / 4.5 / 2** (deep orange, ~100× dimmer than the pops); alpha 1 → 0 |

## Emitter 5: Sparks (the only world-space physics)
| brick | value |
|---|---|
| material | `M_Particle_Additive`, `T_longSpark_02_8x2` (4×2), random frame |
| spawn | **10 at t = 0.01 s** |
| lifetime | 0.05-0.085 s |
| direction | cone of **0-13 whole degrees** around the barrel: RoN's code takes angle × 182.04 and truncates it to an integer number of degrees (docs/ron/cascade_semantics.md) |
| speed | **5-50 m/s** (500-5000 cm/s) + start velocity ±50 cm/s sideways + 5× the gun's own velocity |
| forces | constant acceleration 0 (does nothing); a random acceleration ±3000 cm/s² per axis drawn once at spawn, world space; drag v ← v × (1 − c·dt) every frame with **c re-drawn 2.5-15 every frame** |
| size | 0.25-0.75 cm; every frame length (y) = base × clamp(0.05 × speed in cm/s, 1, 100), then × the 16-sample life curve: x ×1, y ×0.5 → ×0.97 at mid-life → ×0.5 at the end |
| orbit | ±0.5 cm wobble, rotating ±10/s |
| facing | along velocity; pivot 0.15 |
| colour / alpha | 25 / 0.27 / 0.19 (dim red-orange); alpha 1.5 → 0 |
| quality | every setting |

## Emitter 6: LongSpark (one forward streak)
| brick | value |
|---|---|
| material | `M_Particle_Additive`, **a random frame 0-3, re-drawn every frame** |
| spawn | 1 at t = 0.01 s |
| lifetime | **0.03 s** |
| size | 25-50 cm; over life (×0.1, ×0.5, ×1) → (×0.05, ×1.25, ×2) |
| facing | along velocity, axis-locked (rotates only around Y); pivot 0.5 |
| velocity | −5 cm/s |
| colour / alpha | **1528 / 180 / 8** (hot yellow); alpha 1 → 0 |

## Emitter 7: CoreSpark (a star burst at the muzzle)
| brick | value |
|---|---|
| material | `M_Particle_Additive`, frame **7** (the star) |
| spawn | 1 at t = 0.01 s |
| lifetime | 0.03 s |
| start position | 10 cm in front |
| size | 6-12 cm; over life (×0.5, ×1.5, ×1) → (×0.6, ×1.8, ×2) |
| rotation | random |
| facing | camera-facing rectangle; pivot 0.09 |
| colour / alpha | 1528 / 180 / 8; alpha 1 → 0 |

## Timeline of one shot (60 fps: frame 1 = 0-0.017 s, frame 2 = 0.017-0.033 s, frame 3 = 0.033-0.05 s)
| frame | what's on screen |
|---|---|
| 1 | front pop (+ light, 80 %), side pop, pistol core (+ light), small flame: all at full start size; the pops and core show a random sheet frame and the core a random colour |
| 2 | the same, ×1.25-1.5 bigger and fading, **new random frames, colour and light brightness**; long streak, star burst and 10 sparks appear (born at 0.01 s) |
| 3 | pistol core and small flame are gone (0.035 s); pops end at 0.04 s; sparks fly on |
| 4-5 | only sparks, gone by 0.085 s |

## What the V2 flash (the other pistols) differs in
Front pop uses `MI_MuzzleFlash_Rifle` (`MuzzleFlash_Macro_M14`, frames 0-7) at 17-32 cm; small-flame pivot −0.5; see the
appendix for every V2 module.

## Appendix: raw dump (`cascade_dump.py`, V3 then V2)
```
########## WeaponFlash_Pistol_V3
SYSTEM {'CurveEdSetup': None, 'FixedRelativeBoundingBox': '0000000000a074c00000000000a074c00000000000a074c00000000000a074400000000000a074400000000000a0744001', 'bUseFixedRelativeBoundingBox': True, 'bShouldResetPeakCounts': True}
== emitter Sparks {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 15}
   Required mat M_Particle_Additive subUV 4 2 dur None loops 1 local None interp PSUVIM_Random screenAlign PSA_Velocity delay None
   Spawn rate 0 bursts [(0, 10, 0.01)]
   SubUV {'SubImageIndex': '0'} 
   VelocityCone {'Angle': '0/0.075', 'Velocity': '500/5000'} {'Direction': (1.0, 0.0, 0.0)}
   Lifetime {'Lifetime': '0.085/0.05'} 
   Size {'StartSize': '0.25/0.25/0.25/0.75/0.75/0.75'} 
   AccelerationConstant {} {'bAlwaysInWorldSpace': True}
   AccelerationDrag {'DragCoefficientRaw': '2.5/15'} {'bAlwaysInWorldSpace': True}
   SizeScaleBySpeed {} {'SpeedScale': (0.0, 0.05), 'MaxScale': (1.0, 100.0)}
   SizeMultiplyLife {'LifeMultiplier': '1/0.5/0/1/0.56667/0/1/0.63333/0/1/0.7/0/1/0.76667 (TimeScale 15)'} 
   ColorOverLife {'ColorOverLife': '25/0.27332/0.1936', 'AlphaOverLife': '1.5/0 (TimeScale 2)'} 
   Velocity {'StartVelocity': '-50/-50/0/50/50/0', 'StartVelocityRadial': '0'} 
   Acceleration {'Acceleration': '-3000/-3000/-3000/3000/3000/3000'} {'bAlwaysInWorldSpace': True}
   VelocityInheritParent {'Scale': '5/5/5'} {'bInWorldSpace': True}
   PivotOffset {} {'PivotOffset': (0.0, 0.15)}
   Orbit {'OffsetAmount': '-0.5/-0.5/-0.5/0.5/0.5/0.5', 'RotationAmount': '0/0/0/1/1/1', 'RotationRateAmount': '-10/-10/-10/10/10/10'} 
== emitter LongSpark {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 12}
   Required mat M_Particle_Additive subUV 4 2 dur None loops 1 local True interp PSUVIM_Linear screenAlign PSA_Velocity delay None
   Spawn rate 0 bursts [(-1, 1, 0.01)]
   SubUV {'SubImageIndex': '0/3'} 
   Lifetime {'Lifetime': '0.03'} 
   Rotation {'StartRotation': '0/25'} {'bEnabled': False}
   Size {'StartSize': '25/25/25/50/50/50'} 
   ColorOverLife {'ColorOverLife': '1528/180/8', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.1/0.5/1/0.05/1.25/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '-5/0/0', 'StartVelocityRadial': '0'} 
   OrientationAxisLock {} {'LockAxisFlags': 'EPAL_ROTATE_Y'}
   PivotOffset {} {'PivotOffset': (0.0, 0.5)}
== emitter CoreSpark {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 12}
   Required mat M_Particle_Additive subUV 4 2 dur None loops 1 local True interp PSUVIM_Linear screenAlign PSA_Rectangle delay None
   Spawn rate 0 bursts [(-1, 1, 0.01)]
   SubUV {'SubImageIndex': '7'} 
   Lifetime {'Lifetime': '0.03'} 
   Rotation {'StartRotation': '0/1'} 
   Size {'StartSize': '6/6/6/12/12/12'} 
   ColorOverLife {'ColorOverLife': '1528/180/8', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.5/1.5/1/0.6/1.8/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '-5/0/0', 'StartVelocityRadial': '0'} 
   PivotOffset {} {'PivotOffset': (0.0, 0.09)}
   Location {'StartLocation': '10/0/0'} 
== emitter SmallFlame {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 12}
   Required mat MI_MuzzleFlash_Side subUV 2 2 dur None loops 1 local True interp PSUVIM_Random screenAlign PSA_Velocity delay None
   Spawn rate 1 bursts [(-1, 1, 0)]
   SubUV {'SubImageIndex': '0'} 
   Lifetime {'Lifetime': '0.035'} 
   Rotation {'StartRotation': '0/25'} {'bEnabled': False}
   Size {'StartSize': '15/15/15/20/20/20'} 
   ColorOverLife {'ColorOverLife': '382.5/4.5/2', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.25/0.5/1/0.75/1.5/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '25/0/0', 'StartVelocityRadial': '0'} 
   PivotOffset {} {'PivotOffset': (0.0, -1.0)}
== emitter Pop {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 12}
   Required mat MI_MuzzleFlash_SMG subUV 4 4 dur None loops 1 local True interp PSUVIM_Linear screenAlign None delay None
   Spawn rate 1 bursts [(0, 1, 0)]
   EventGenerator {} 
   SubUV {'SubImageIndex': '0/23'} 
   Lifetime {'Lifetime': '0.04'} 
   Rotation {'StartRotation': '0/25'} 
   Size {'StartSize': '25/25/25/35/35/35'} 
   ColorOverLife {'ColorOverLife': '38250/450/200', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   Light {'ColorScaleOverLife': '10/10/10/25/25/25', 'BrightnessOverLife': '70/80', 'RadiusScale': '100', 'LightExponent': '320'} {'bAffectsTranslucency': True, 'SpawnFraction': 0.8}
   SizeMultiplyLife {'LifeMultiplier': '1/1/1/1.5/1.5/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '250/0/0', 'StartVelocityRadial': '0'} 
   Location {'StartLocation': '25/0/0'} 
== emitter Pop {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 12}
   Required mat MI_MuzzleFlash_Side subUV 2 2 dur None loops 1 local True interp PSUVIM_Random screenAlign PSA_Velocity delay None
   Spawn rate 1 bursts [(-1, 0, 0)]
   EventReceiverSpawn {'SpawnCount': '1'} {'EventName': 'FlashCore'}
   SubUV {'SubImageIndex': '0'} 
   Lifetime {'Lifetime': '0.04'} 
   Rotation {'StartRotation': '0/25'} {'bEnabled': False}
   Size {'StartSize': '20/20/20/30/30/30'} 
   ColorOverLife {'ColorOverLife': '38250/450/200', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.12/0.5/1/1/2.5/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '5/0/0', 'StartVelocityRadial': '0'} 
   PivotOffset {} {'PivotOffset': (0.0, -0.35)}
== emitter Pop {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 15}
   Required mat MI_MuzzleFlash_Pistol subUV 4 4 dur None loops 1 local True interp PSUVIM_Linear screenAlign None delay None
   Spawn rate 1 bursts [(-1, 1, 0)]
   SubUV {'SubImageIndex': '0/11'} 
   Lifetime {'Lifetime': '0.035'} 
   Rotation {'StartRotation': '0/25'} 
   Size {'StartSize': '8/8/8/12/12/12'} 
   ColorOverLife {'ColorOverLife': '0.85417/0.07355/0.0178/38250/450/200', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   Light {'ColorScaleOverLife': '10/10/10/25/25/25', 'BrightnessOverLife': '15/50', 'RadiusScale': '100', 'LightExponent': '16'} {'bAffectsTranslucency': True, 'SpawnFraction': 0.8}
   SizeMultiplyLife {'LifeMultiplier': '1/1/1/1.5/1.5/1.2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '100/0/0', 'StartVelocityRadial': '0'} 
   Location {'StartLocation': '12/0/0'} 
########## WeaponFlash_Pistol_V2
SYSTEM {'CurveEdSetup': None, 'FixedRelativeBoundingBox': '0000000000a074c00000000000a074c00000000000a074c00000000000a074400000000000a074400000000000a0744001', 'bUseFixedRelativeBoundingBox': True, 'bShouldResetPeakCounts': True, 'bRegenerateLODDuplicate': True}
== emitter SmallFlame {'bUseLegacySpawningBehavior': True, 'DetailModeBitmask': 12}
   Required mat MI_MuzzleFlash_Side subUV 2 2 dur None loops 1 local True interp PSUVIM_Random screenAlign PSA_Velocity delay None
   Spawn rate 1 bursts [(-1, 1, 0)]
   SubUV {'SubImageIndex': '0'} 
   Lifetime {'Lifetime': '0.035'} 
   Rotation {'StartRotation': '0/25'} {'bEnabled': False}
   Size {'StartSize': '15/15/15/20/20/20'} 
   ColorOverLife {'ColorOverLife': '382.5/4.5/2', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.25/0.5/1/0.75/1.5/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '25/0/0', 'StartVelocityRadial': '0'} 
   PivotOffset {} {'PivotOffset': (0.0, -0.5)}
== emitter Pop {'bUseLegacySpawningBehavior': True}
   Required mat MI_MuzzleFlash_Rifle subUV 4 4 dur None loops 1 local True interp PSUVIM_Linear screenAlign None delay None
   Spawn rate 1 bursts [(0, 1, 0)]
   EventGenerator {} 
   SubUV {'SubImageIndex': '0/7'} 
   Lifetime {'Lifetime': '0.04'} 
   Rotation {'StartRotation': '0/25'} 
   Size {'StartSize': '17/17/17/32/32/32'} 
   ColorOverLife {'ColorOverLife': '38250/450/200', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   Light {'ColorScaleOverLife': '10/10/10/25/25/25', 'BrightnessOverLife': '70/80', 'RadiusScale': '100', 'LightExponent': '320'} {'bAffectsTranslucency': True, 'SpawnFraction': 0.8}
   SizeMultiplyLife {'LifeMultiplier': '1/1/1/1.5/1.5/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '250/0/0', 'StartVelocityRadial': '0'} 
   Location {'StartLocation': '10/0/0'} 
== emitter Pop {'bUseLegacySpawningBehavior': True}
   Required mat MI_MuzzleFlash_Side subUV 2 2 dur None loops 1 local True interp PSUVIM_Random screenAlign PSA_Velocity delay None
   Spawn rate 1 bursts [(-1, 0, 0)]
   EventReceiverSpawn {'SpawnCount': '1'} {'EventName': 'FlashCore'}
   SubUV {'SubImageIndex': '0'} 
   Lifetime {'Lifetime': '0.04'} 
   Rotation {'StartRotation': '0/25'} {'bEnabled': False}
   Size {'StartSize': '20/20/20/30/30/30'} 
   ColorOverLife {'ColorOverLife': '38250/450/200', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.12/0.5/1/0.75/2/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '5/0/0', 'StartVelocityRadial': '0'} 
   PivotOffset {} {'PivotOffset': (0.0, -0.45)}
== emitter Pop {'bUseLegacySpawningBehavior': True}
   Required mat MI_MuzzleFlash_Pistol subUV 4 4 dur None loops 1 local True interp PSUVIM_Linear screenAlign None delay None
   Spawn rate 1 bursts [(-1, 1, 0)]
   SubUV {'SubImageIndex': '0/11'} 
   Lifetime {'Lifetime': '0.035'} 
   Rotation {'StartRotation': '0/25'} 
   Size {'StartSize': '8/8/8/12/12/12'} 
   ColorOverLife {'ColorOverLife': '0.85417/0.07355/0.0178/38250/450/200', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   Light {'ColorScaleOverLife': '10/10/10/25/25/25', 'BrightnessOverLife': '15/50', 'RadiusScale': '100', 'LightExponent': '16'} {'bAffectsTranslucency': True, 'SpawnFraction': 0.8}
   SizeMultiplyLife {'LifeMultiplier': '1/1/1/1.5/1.5/1.2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '100/0/0', 'StartVelocityRadial': '0'} 
   Location {'StartLocation': '5/0/0'} 
== emitter Sparks {'bUseLegacySpawningBehavior': True}
   Required mat M_Particle_Additive subUV 4 2 dur None loops 1 local None interp PSUVIM_Random screenAlign PSA_Velocity delay None
   Spawn rate 0 bursts [(0, 10, 0.01)]
   SubUV {'SubImageIndex': '0'} 
   VelocityCone {'Angle': '0/0.075', 'Velocity': '500/5000'} {'Direction': (1.0, 0.0, 0.0)}
   Lifetime {'Lifetime': '0.085/0.05'} 
   Size {'StartSize': '0.25/0.25/0.25/0.75/0.75/0.75'} 
   AccelerationConstant {} {'bAlwaysInWorldSpace': True}
   AccelerationDrag {'DragCoefficientRaw': '2.5/15'} {'bAlwaysInWorldSpace': True}
   SizeScaleBySpeed {} {'SpeedScale': (0.0, 0.05), 'MaxScale': (1.0, 100.0)}
   SizeMultiplyLife {'LifeMultiplier': '1/0.5/0/1/0.56667/0/1/0.63333/0/1/0.7/0/1/0.76667 (TimeScale 15)'} 
   ColorOverLife {'ColorOverLife': '25/0.27332/0.1936', 'AlphaOverLife': '1.5/0 (TimeScale 2)'} 
   Velocity {'StartVelocity': '-50/-50/0/50/50/0', 'StartVelocityRadial': '0'} 
   Acceleration {'Acceleration': '-3000/-3000/-3000/3000/3000/3000'} {'bAlwaysInWorldSpace': True}
   VelocityInheritParent {'Scale': '5/5/5'} {'bInWorldSpace': True}
   PivotOffset {} {'PivotOffset': (0.0, 0.15)}
   Orbit {'OffsetAmount': '-0.5/-0.5/-0.5/0.5/0.5/0.5', 'RotationAmount': '0/0/0/1/1/1', 'RotationRateAmount': '-10/-10/-10/10/10/10'} 
== emitter LongSpark {'bUseLegacySpawningBehavior': True}
   Required mat M_Particle_Additive subUV 4 2 dur None loops 1 local True interp PSUVIM_Linear screenAlign PSA_Velocity delay None
   Spawn rate 0 bursts [(-1, 1, 0.01)]
   SubUV {'SubImageIndex': '0/3'} 
   Lifetime {'Lifetime': '0.03'} 
   Rotation {'StartRotation': '0/25'} {'bEnabled': False}
   Size {'StartSize': '25/25/25/50/50/50'} 
   ColorOverLife {'ColorOverLife': '1528/180/8', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.1/0.5/1/0.05/1.25/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '-5/0/0', 'StartVelocityRadial': '0'} 
   OrientationAxisLock {} {'LockAxisFlags': 'EPAL_ROTATE_Y'}
   PivotOffset {} {'PivotOffset': (0.0, 0.75)}
== emitter CoreSpark {'bUseLegacySpawningBehavior': True}
   Required mat M_Particle_Additive subUV 4 2 dur None loops 1 local True interp PSUVIM_Linear screenAlign PSA_Rectangle delay None
   Spawn rate 0 bursts [(-1, 1, 0.01)]
   SubUV {'SubImageIndex': '7'} 
   Lifetime {'Lifetime': '0.03'} 
   Rotation {'StartRotation': '0/1'} 
   Size {'StartSize': '6/6/6/12/12/12'} 
   ColorOverLife {'ColorOverLife': '1528/180/8', 'AlphaOverLife': '1/0 (TimeScale 1)'} 
   ParameterDynamic {} {'UpdateFlags': 1}
   SizeMultiplyLife {'LifeMultiplier': '0.5/1.5/1/0.6/1.8/2 (TimeScale 1)'} 
   Velocity {'StartVelocity': '-5/0/0', 'StartVelocityRadial': '0'} 
   PivotOffset {} {'PivotOffset': (0.0, 0.09)}
   Location {'StartLocation': '5/0/0'} 
```
