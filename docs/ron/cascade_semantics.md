# Cascade module semantics (Ready or Not exe)

## Cascade module semantics from Ready or Not's exe (motion modules)

Source: static analysis of `ReadyOrNotSteam-Win64-Shipping.exe` (image base 0x140000000, addresses below are full VAs). Ghidra decompiles come from the ProLiant `q dec` helper; constants were read from the PE. Owner: motion-modules agent, 2026-10-18.

### How the classes were mapped (and corrections to the earlier pass)
Each class has a registration record `[Z_Construct, StaticClass, L"UParticleModuleX", RegInfo, 0]`. StaticClass calls GetPrivateStaticClassBody with the class **size** and an InternalConstructor, which jumps to the ctor that writes the vtable. Class sizes match the property layouts (Cone 0xB0, VIP 0x98, Drag 0x70, AccelConst 0x50, Acceleration 0xA0, Pivot 0x40, SizeScaleBySpeed 0x50, Orbit 0x178). Vtable slots: 87 = CompileModule (GPU build info), 88 = Spawn, 89 = Update, 91 = RequiredBytes.

| class | vtable | 87 Compile | 88 Spawn | 89 Update |
|---|---|---|---|---|
| VelocityCone | 146B16698 | stock | 14322F700 -> SpawnEx 143231BF0 | none |
| VelocityInheritParent | 146B16A38 | stock | 14322F760 | none |
| Velocity | 146B162F8 | stock | 14322F6A0 | none |
| AccelerationConstant | 146AF2240 | 1431CD5B0 | 1431E7880 | 1431ECD30 |
| Acceleration | 146AF1EA0 | 1431CD560 | 1431E7370 | 1431EC520 (RequiredBytes 14151E110) |
| AccelerationDrag | 146AF25E0 | 1431CD5D0 | none | 1431ED700 |
| SizeScaleBySpeed | 146B09010 | 143208760 | none | 143238550 |
| PivotOffset | 146B06830 | 1431CDB50 | none | none |
| Orbit | 146B058E0 | 1432084E0 | 14322F240 | 143236A10 (RequiredBytes 0x48) |

**Corrections:** the old uemod.py picked the wrong InternalConstructor for several classes, which shifted the vtables by one class. 6B16A38/322F760 is **VelocityInheritParent**, not VelocityCone. The Cone math (182.0378, 65533.6) comes from 3231BF0, which really is VelocityCone. 31CD5F0/6AF2980 is not Drag. 31E81F0/31F09C0 (vtable 6AF67D0) is not Acceleration: it is an attractor-style module with a vector distribution at +0x30 and a float at +0x90. 322D5D0 is not PivotOffset, which has no Spawn or Update at all.

### Particle layout used by every function (UE5 FBaseParticle)
OldLocation +0x00 (3 doubles), Location +0x18 (3 doubles), **BaseVelocity +0x30**, **Velocity +0x40**, BaseSize +0x50, Size +0x60, Flags +0x6C (0x4000000 = Freeze, skipped), RelativeTime +0x90. Emitter instance: EmitterTime at +0x1BC, Component at +0x18. Component: ComponentToWorld rotation quat +0x240, translation +0x260, scale +0x280, RandomStream +0x7F8, velocity-like vector +0x618. Every module that changes velocity adds the same delta to both Velocity and BaseVelocity, so the change persists. In stock UE, Velocity is reset to BaseVelocity at the start of each tick; this was not re-verified here. In Sparks, `Required.local` is None, so the emitter simulates in world space.

### Random draws (FRawDistribution GetValue, float 142EBC670 / vector 142EBC820)
A baked Uniform uses Op=Random: `v = lerp(min, max, r)`. Without a random stream, r comes from **FMath::SRand**, which is global state (140B578F0: `seed = seed*0xBB38435 + 0x3619636B; r = asfloat((seed & 0x7FFFFF) | 0x3F800000) - 1`). With a stream it comes from the same LCG on the stream. A vector uniform makes **three independent draws** (2EBC3F0 calls SRand three times) unless axes are locked. So a distribution read in Spawn gives a per-particle constant, and one read in Update is **redrawn every frame**.

### VelocityCone (spawn only), 143231BF0
```
3231CC0: lea rcx,[rdi+0x38]; movss xmm1,[rsi+0x1bc]; mov r9,r14; call 2EBC670   ; Angle(EmitterTime, stream)
3231CD8: lea rcx,[rdi+0x68]; ...                                   call 2EBC670   ; Velocity(EmitterTime, stream)
3231CFC: mulss xmm6,[6D700C4]        ; 182.03784 (=65536/360)
3231D28: cvttss2si eax,xmm6 ; cvtdq2pd           ; Pitch = (double)(int)(Angle*182.0378)
3231D44: mulss xmm1,[6D700C8]        ; r*65533.625 (=2*pi*10430)
3231D52: cvttss2si eax,xmm1 ; cvtdq2pd           ; Yaw  = (double)(int)(r*65533.6)
3231D64: call BE1CA0                 ; FRotationMatrix(FRotator(Pitch,Yaw,0)) -- UE5 FRotator is DEGREES
```
- `theta_deg = trunc(Angle*182.0378)`, with Angle ~ U(0, 0.075) from the component RandomStream. That gives integer degrees 0..13; 13 is reached only when Angle is at least 0.07142, about 4.8% of draws. The legacy unreal-units scale is fed into a degree-based UE5 FRotator, so the effect is 182x wider than the author meant.
- `phi_deg = trunc(r*65533.6)` degrees, with r drawn from the same stream. This is effectively a random integer azimuth (about 182 full turns, so close to uniform mod 360).
- `d0 = Rot(pitch=theta, yaw=phi) * (0,0,1)`. It is then rotated so that +Z maps onto `normalize(Direction)` (Direction is (1,0,0)). That makes `theta` the angle from the axis.
- bInWorldSpace is false (+0x30 bit0 = 0) and the emitter is not local, so `d = ComponentToWorld.TransformVector(d0)`, which includes scale: the axis is the component's local +X. If bApplyOwnerScale (+0x30 bit1) is set, the result is multiplied by component scale again.
- `Velocity += d * speed` and `BaseVelocity += d * speed`, with speed ~ U(500, 5000) cm/s (same stream; Angle is drawn before Velocity).

### VelocityInheritParent (spawn only), 14322F760
```
if ((*(byte *)(*(longlong *)(lVar6 + 0x30) + 0x71) & 1) == 0) {   // not bUseLocalSpace
    local_c8 = *(lVar2 + 0x618); local_b8 = *(double *)(lVar2 + 0x628); }   // component velocity, world
else { ... InverseTransformVector ... }
FUN_142ebc820(param_1 + 0x38,&local_b0,*(param_2 + 0x1bc),lVar2,0,0);       // Scale(EmitterTime)
*(float *)(param_5 + 0x40) = (float)(local_b0 * local_c8._0_8_) + *(float *)(param_5 + 0x40);
*(float *)(param_5 + 0x30) = (float)(local_b0 * local_c8._0_8_) + *(float *)(param_5 + 0x30);
```
`Velocity += Scale * Vcomp` and `BaseVelocity += Scale * Vcomp`, once at spawn. Scale = (5,5,5) and Vcomp is the particle-system component's world velocity in cm/s (component +0x618). bInWorldSpace is never read. The 0x618 field is presumably UE's `PartSysVelocity = (Location - OldPosition)/dt`; its writer was not traced.

### AccelerationConstant (spawn + every frame), 1431E7880 / 1431ECD30
Acceleration is a plain FVector at +0x38. Spawn does `v += A*SpawnTime` and `vBase += A*SpawnTime`. Update does `v += A*dt` and `vBase += A*dt` for every particle every frame:
```
31ED045: mulss xmm3,xmm5            ; A*dt
31ED099: addss xmm0,[rdx+0x40] ; movss [rdx+0x40],xmm0   ; Velocity
31ED0C0: addss xmm1,[rdx+0x30] ; movss [rdx+0x30],xmm1   ; BaseVelocity
```
With bAlwaysInWorldSpace and a local-space emitter it would inverse-transform A first. In Sparks A = 0, so this module does nothing.

### Acceleration (random per particle, applied every frame), 1431E7370 / 1431EC520
Spawn:
```
pdVar6 = FUN_142ebc820(param_1 + 0x38,local_d8,*(param_2 + 0x1bc),*(param_2 + 0x18),0,0); // no stream -> SRand x3
*(float *)(uVar8 + param_5) = (float)*pdVar6; ...            // stored in per-particle payload (RequiredBytes)
if (*(byte *)(param_1 + 0x98) & 1) ... *= component scale       // bApplyOwnerScale
*(float *)(param_5 + 0x40) = param_4 * *(float *)(uVar8 + param_5) + *(float *)(param_5 + 0x40); // v += a*SpawnTime
*(float *)(param_5 + 0x30) = param_4 * *(float *)(uVar8 + param_5) + *(float *)(param_5 + 0x30); // vBase += a*SpawnTime
```
Update, for each non-frozen particle every frame:
```
*(float *)(lVar20 + 0x40) = param_4 * *(float *)(param_3 + lVar20) + *(float *)(lVar20 + 0x40);  // v += a*dt
*(float *)(lVar20 + 0x30) = param_4 * *(float *)(param_3 + lVar20) + *(float *)(lVar20 + 0x30);  // vBase += a*dt
```
At spawn `a = (Ux, Uy, Uz)` with three independent U(-3000, 3000) cm/s^2 draws, evaluated at EmitterTime and kept fixed for the particle's life. Each frame adds `a*dt`, in world space (the emitter is not local, so there is no transform).

### AccelerationDrag (every frame, coefficient redrawn every frame), 1431ED700
```
31ED76C: test dword [rbx+0x6c],0x4000000 ; jne skip              ; frozen
31ED77D: lea rcx,[r13+0x40]; movss xmm1,[rbx+0x90]; xor r9d,r9d; call 2EBC670   ; c = DragCoefficientRaw(RelativeTime), no stream -> SRand
31ED79E: xorps xmm3,xmm7             ; -c  (mask 0x80000000)
31ED7A9: mulss xmm2,xmm3 ; mulss xmm2,xmm6     ; Velocity.x * -c * dt
31ED7C4: addss xmm0,xmm1 ; movss [rbx+0x40],xmm0   ; Velocity  += D
31ED7E7: addss xmm2,[rbx+0x30] ; movss [rbx+0x30],xmm2 ; BaseVelocity += D
```
`D = -c * Velocity * dt`, then `Velocity += D` and `BaseVelocity += D`. This is explicit Euler, `v <- v * (1 - c*dt)`; there is no exp and no clamp. Each frame and each particle gets a fresh `c ~ U(2.5, 15)` 1/s (mean 8.75). D is computed from the current Velocity, after earlier update modules in this frame. bAlwaysInWorldSpace is not read here.

### SizeScaleBySpeed (every frame, overwrites Size), 143238550
```
dVar13 = SQRT(v.x*v.x + v.y*v.y + v.z*v.z);              // |Velocity| (particle +0x40), cm/s
dVar14 = dVar13 * dVar2;  dVar13 = dVar13 * dVar1;       // speed*SpeedScale.Y, speed*SpeedScale.X
if (dVar14 <= 1.0) dVar14 = 1.0;  if (dVar13 <= 1.0) dVar13 = 1.0;
if (MaxScale.Y <= dVar14) dVar14 = MaxScale.Y;  if (MaxScale.X <= dVar13) dVar13 = MaxScale.X;
Size.x = |BaseSize.x| * sx;  Size.y = |BaseSize.y| * sy;  Size.z = |BaseSize.z|;
```
`sx = min(max(speed*SpeedScale.X, 1), MaxScale.X)` and likewise sy. With SpeedScale (0, 0.05) and MaxScale (1, 100): sx = 1 and `sy = clamp(0.05*|v|, 1, 100)`. sy hits the 100 cap at 2000 cm/s and is 1 below 20 cm/s. Size is **assigned** from BaseSize, not multiplied, so any size-over-life module that comes **later** in the update list (SizeMultiplyLife does) multiplies on top of it. |v| is the Velocity as updated so far this frame (after AccelConst and Drag, before Acceleration's add, if modules run in list order).

### PivotOffset (no per-particle code)
- CompileModule 1431CDB50 is GPU only: `EmitterInfo.PivotOffset (+0x458) = PivotOffset - 0.5` (constant 0x643E860 = 0.5).
- The CPU path is in UParticleEmitter cache code 14318F440:
  ```
  *(param_1 + 0x178) = _DAT_146460480;            // (-0.5, -0.5) doubles
  ... if IsA(UParticleModulePivotOffset):
  *(double *)(param_1 + 0x178) = (double)plVar12[6] + *(double *)(param_1 + 0x178);
  *(double *)(param_1 + 0x180) = (double)plVar12[7] + *(double *)(param_1 + 0x180);
  ```
- The result is one emitter-wide constant, `EmitterPivot = PivotOffset - (0.5, 0.5)`, which is (-0.5, -0.35) for Sparks. It goes to the sprite vertex shader. No random draw is involved.
- The quad math is in the shader, not the exe. In stock UE the PivotOffset module is the UV position of the pivot, and (0.5, 0.5) means centred. On that reading, (0, 0.15) puts the particle position at the U=0 edge and V=0.15 of the quad. This shader-side reading is unverified.

### Orbit (spawn adds random values; Update does nothing with default options), 14322F240 / 143236A10
Payload (0x48 bytes) layout: BaseOffset +0, Offset +0xC, Rotation +0x18, BaseRotationRate +0x24, RotationRate +0x30, PreviousOffset +0x3C. Each property has an option word: OffsetOptions +0xA0, RotationOptions +0x108, RotationRateOptions +0x170. Bit0 = ProcessDuringSpawn, bit1 = ProcessDuringUpdate, bit2 = UseEmitterTime.

Spawn:
```
if (*(uint *)(param_1 + 0xa0) & 1) { t = (opt&4) ? EmitterTime : Particle.RelativeTime;
  pdVar6 = FUN_142ebc820(param_1 + 0x40,local_30,t,*puVar1,0,0);      // OffsetAmount, SRand
  BaseOffset += v; PreviousOffset = Offset; Offset += v; }
if (*(uint *)(param_1 + 0x108) & 1) { ... FUN_142ebc820(param_1 + 0xa8 ...); Rotation += v; }
if (*(uint *)(param_1 + 0x170) & 1) { ... FUN_142ebc820(param_1 + 0x110 ...); BaseRotationRate += v; RotationRate += v; }
```
Update repeats the same adds per frame only where bit1 is set. The dump shows no options, so the UE defaults apply (spawn only), and Update is a no-op. At spawn: Offset ~ U(-0.5, 0.5) cm on each axis, Rotation ~ U(0, 1) on each axis, RotationRate ~ U(-10, 10) per second on each axis, all from SRand and evaluated at RelativeTime (0 at spawn). The CompileModule switch on +0x38 shows ChainMode 0 = Add, 1 = Scale, 2 = Link.

**Not located:** the emitter-level code that turns this payload into motion. In stock UE4 that is FParticleEmitterInstance::UpdateOrbitData: each frame `Rotation += RotationRate*dt`, then `Offset = FRotator::MakeFromEuler(Rotation*360).RotateVector(Offset)`, and the renderer draws the sprite at `Location + Offset` (Rotation is in turns, so 1 = 360 deg). There is no RTTI in the exe, and the candidate functions found by scanning for the 360.0f constant and the freeze test were not it. Treat this part as stock UE behaviour, not verified in RoN.

### Open / unverified
- Orbit's per-frame rotation and offset application (see above).
- Shader use of EmitterPivot.
- The writer of component +0x618 (assumed PartSysVelocity).
- The emitter tick order: Velocity reset to BaseVelocity, module Update in list order, then `Location += Velocity*dt`. This is stock-UE knowledge and was not decompiled here.
- Whether the same tick also runs Update on particles spawned in it. That depends on bUseLegacySpawningBehavior (true here).

## Cascade module semantics from Ready or Not's exe (rendering, light, events)

Source: static analysis of `ReadyOrNotSteam-Win64-Shipping.exe` (full VAs, base 0x140000000). Decompiles come from the ProLiant `q dec` helper. The per-module raw tables (Op, LockFlag, EntryCount) were re-read from the cooked `WeaponFlash_Pistol_V3` with the repo's `usmap/uasset` readers, because `cascade_dump.py` prints only `Values`. Owner: rendering/light agent, 2026-10-18.

### Vtables (resolved through the ClassConstructor stack argument)
The InternalConstructor is the `lea rax,..; mov [r11-0x30],rax` argument of GetPrivateStaticClassBody. Take the first one only: the scan must stop at `ret`, or it runs into the next class's StaticClass. That over-run was the old uemod.py bug.

| class | vtable | 88 Spawn | 89 Update | other |
|---|---|---|---|---|
| Light | 6AF9BB0 | 31E8480 → SpawnEx 31E8DC0 | 31F19F0 | |
| SubUV | 6B121E8 | 31E8AE0 | 31F2380 | 116 DetermineImageIndex 31D0DA0 |
| ColorOverLife | 6AF67D0 | 31E81F0 | 31F09C0 | (confirmed via SC 2AF1060 → IC 2AF13B0 → ctor 31C6680; this **is** ColorOverLife, not an attractor) |
| SizeMultiplyLife | 6B088D0 | 322F560 | 3237F90 | |
| EventGenerator | 6AF7650 | none | none | 116 HandleParticleSpawned 3220BB0, 117 Killed 3220A40, 118 Collision 3220800, 119 Burst 3220730 |
| EventReceiverSpawn | 6AF8218 | none | none | 117 ProcessParticleEvent 3224CB0 |
| Required | 6B06BD0 | none | none | |
| PivotOffset | 6B06830 | none | none | 87 CompileModule 31CDB50 |

The previous "SubUV 6B14548" and "EventReceiverSpawn slot89 31EE760" are wrong. 31EE760 is an AttractorPoint-style Update and 31ED700 is AccelerationDrag. Particle-life sampler for Op=None tables (142EBC010 float, 142EBC310 vector): `x = max(0,(t - TimeBias)*TimeScale); i = trunc(x), clamped to EntryCount-1; j = min(i+1, EntryCount-1); v = lerp(E[i], E[j], x - i)`. Clamping happens at both ends.

### The asset's tables really are random, not curves (important correction)
| field | raw table | meaning |
|---|---|---|
| Pop-front `SubImageIndex` "0/23" | Op 2 (Random), EntryCount 1 | **Uniform(0,23)**, not 0→23 over life |
| Pistol-core / LongSpark SubImageIndex "0/11", "0/3" | Op 2 | Uniform(0,11), Uniform(0,3) |
| Pistol-core `ColorOverLife` "0.85/0.07/0.02/38250/450/200" | Op 2, LockFlag 0 | per-channel independent Uniform between the two colours, **not** a dim→bright curve |
| Light `ColorScaleOverLife` 10..25 | Op 2, **LockFlag 4 (XYZ)** | one draw for all 3 channels, so the scale is grey 10..25 |
| Light `BrightnessOverLife` | Op 2 | Uniform(70,80) / Uniform(15,50) |
| Light RadiusScale 100, LightExponent 320/16, SpawnCount 1, Lifetimes | Op 1, 1 entry | constants (Sparks Lifetime is Op 2, Uniform(0.05,0.085)) |
| AlphaOverLife, all SizeMultiplyLife | Op 1, EntryCount 2 (16 for Sparks), TimeScale 1 (15) | real curves over RelativeTime |

Op 2 with a null stream draws from global `FMath::SRand` (142EBC0B0 / 142EBC3F0: `if (param_4 == 0) fVar7 = FUN_140b578f0();`). A vector makes 3 draws; LockFlag 1=XY, 2=XZ, 3=YZ, 4=XYZ share them. Every Update below passes a null stream, so **Op 2 values are re-drawn every frame**.

### 1. Light (ParticleModuleLight)
SpawnEx 1431E8DC0 (payload `P` at particle+Offset: ColorScale f3 @0, LightId u64 @0x10, RadiusScale @0x18, Exponent @0x1C, InverseExposureBlend @0x20, bValid @0x24, bAffectsTranslucency @0x25, bHighQuality @0x26):
```
iVar4 = *(int *)(DAT_148d84268 + ..);            // r.ParticleLightQuality (cvar name read from 140A064F0); 0 = no light written
if (0 < iVar4) {
  fVar13 = GetValue(this+0xa0 /*Brightness*/, Particle.RelativeTime, Comp, stream);
  pdVar10 = GetValue(this+0x40 /*ColorScaleOverLife*/, Particle.RelativeTime, .., stream);
  *pfVar1 = (float)*pdVar10 * fVar13; ...                       // ColorScale = CSOL * Brightness
  pfVar1[6] = GetValue(this+0xd0 /*RadiusScale*/, Owner->EmitterTime(+0x1bc) ..);
  pfVar1[7] = bUseInverseSquaredFalloff(+0x30) ? 0 : GetValue(this+0x100 /*LightExponent*/, EmitterTime);
  pfVar1[8] = bOverride(+0x34&1) ? this->InverseExposureBlend(+0x130) : (float)cvar fx.ParticleDefaultLightInverseExposureBlend;
  seed = seed*0xbb38435+0x3619636b;  bValid = (asfloat(seed>>9|0x3f800000) - 1.0) < SpawnFraction(+0x3c);
  P[0x25] = bAffectsTranslucency(+0x31); P[0x26] = bHighQualityLights(+0x13c);
```
- **SpawnFraction** is rolled **once per particle at spawn** from the emitter/component RandomStream. For this effect that means 1 particle, so 80 % of shots get a light. It is never re-rolled.
- Update 1431F19F0, every frame for each live particle:
  `fVar17 = GetValue(this+0xa0, Particle.RelativeTime, Comp, 0); pdVar8 = GetValue(this+0x40, &.., RelativeTime) /*stream 0*/; *pfVar16 = (float)*pdVar8 * fVar17;`
  So ColorScale = CSOL × Brightness is **re-drawn every frame** (grey 10..25 × 70..80 for the front pop; × 15..50 for the core). RadiusScale, Exponent and bValid stay at their spawn values.
- Render: FDynamicSpriteEmitterDataBase::GatherSimpleLights **143217D80**, run per frame over the active particles only, so the light lives exactly as long as its particle:
```
if (*(char *)(pfVar10 + 9) != '\0') {                                    // bValid
  dVar20 = |Particle.Size.X * Source.Scale.X|;                           // +0x60 * src+0x38
  if (ScreenAlignment(src+0xbe) < 2 || == 6) dVar18 = dVar20;  else dVar18 = |Size.Y * Scale.Y|;
  local_f8  = Color.A(+0x7c) * Color.R(+0x70) * P.ColorScale.x;         // likewise G, B
  fStack_ec = (float)((dVar18 + dVar20) * P.RadiusScale * 0.5);          // DAT_14643e860 = 0.5
  if (!P.bHighQuality) { if (Radius <= 1e-4 && max(rgb) <= 1e-4) skip;
     pos = Particle.Location (+orbit offset 143204220); if (bUseLocalSpace) pos = LocalToWorld * pos; emit FSimpleLightEntry }
```
  Formulas: **Radius = RadiusScale × (SizeX + SizeY)/2** using the particle's *current* Size (after SizeMultiplyLife), × emitter scale. For PSA_Square (the pops, whose ScreenAlignment is absent, so the default 1 applies) SizeY = SizeX, so **Radius = 100 × Size.X**: 2500-3500 cm at birth, growing ×1.5 with the size curve. **LightColor (linear RGB) = Particle.Color.rgb × Particle.Color.A × ColorScale**. It carries the particle's HDR colour (38250/450/200), so the AlphaOverLife 1→0 fades the light linearly. Exponent = 320 or 16 (non-zero, so exponent fall-off, not inverse-square). bAffectTranslucency = asset flag (true). VolumetricScattering comes from the emitter data (src+0xa8). There is no unit conversion: it is a unitless simple deferred light, with InverseExposureBlend = cvar (UE default 0).
- Not determined: the shader fall-off formula for Exponent (HLSL, not in the exe), and the cvar values set in RoN's ini files.

### 2. SubUV (Required.InterpolationMethod: 0 None, 1 Linear, 2 Linear_Blend, 3 Random, 4 Random_Blend)
- Spawn 1431E8AE0: only if `Required(+0xc1) != 0 && SubUVDataOffset != 0`: `payload.ImageIndex = this->DetermineImageIndex(...)` (vtable+0x3a0).
- Update 1431F2380: `if (interp != 0 && (interp-3 > 1 /*not Random*/ || Required.RandomImageChanges(+0xd4) != 0))`, then for each particle `if (!Freeze && RelativeTime <= 1.0) ImageIndex = DetermineImageIndex(..)`.
- DetermineImageIndex 1431D0DA0:
```
Total = SubUVAnimation ? H*V : Required.SubImages_H(+0xc8) * SubImages_V(+0xcc);
Linear/Linear_Blend:  xmm1 = Particle.RelativeTime; r9 = 0; call GetValue(this+0x38 /*SubImageIndex*/)  -> idx
                      if (mode == 1) idx = roundps(idx, 3 /*truncate*/);
Random/Random_Blend:  if (RandomImageTime(+0xd0) == 0 || RelativeTime - pay.RandomImageTime > RandomImageTime || pay.RandomImageTime == 0)
                        { idx = (int)(frand_stream * Total); pay.RandomImageTime = RelativeTime; }   // 0..Total-1
```
- **Pop front (4×4, Linear, SubImageIndex Uniform(0,23))**: every frame (spawn and each update) `frame = trunc(SRand()*23)` = 0..22 uniform, so it is a **random flicker, not a sequence**. **Pistol core** gives 0..10 per frame, **LongSpark** 0..2 per frame (4×2), and **CoreSpark** a constant 7. **Side pops / SmallFlame** (2×2, Random, RandomImageChanges 0): one random frame 0..3 at spawn, then Update is skipped. **Sparks** (Random, RandomImageChanges 25, RandomImageTime 0.03808): re-rolled whenever RelativeTime has advanced by more than 0.038, which at 60 fps is every frame.
- Out of range: nothing clamps or wraps on the CPU. The sprite vertex fill (FDynamicSpriteEmitterData::GetVertexAndIndexData **14321F730**) copies the raw float: `if (0 < SubUVDataOffset(+0xd8)) local_260 = *(float*)(particle + SubUVDataOffset); ... param_2[0xb] = local_260;`. Indices 16..22 on a 4×4 sheet are resolved by the GPU vertex factory. Stock `ParticleSpriteVertexFactory.ush` does `col = fmod(i, H); row = floor(i / H)`, so row 4-5 gives V in [1, 1.5), which wraps to rows 0-1 (frames 0..6) with a Wrap sampler or smears the edge with Clamp. That is shader and material behaviour, **not verifiable in the exe**.
- Blending: in non-blend modes the index is truncated, so the fractional part is 0 and nothing blends. Only modes 2 and 4 keep the fraction.

### 3. EventGenerator → EventReceiverSpawn ("FlashCore")
Asset: Events = [{Type EPET_Spawn, Frequency 0, bUseOrbitOffset false, CustomName FlashCore}]. Receiver: SpawnCount const 1, EventName FlashCore, bInheritVelocity and bUsePSysLocation false (defaults).
- HandleParticleSpawned 143220BB0, called per spawned generator particle:
```
EventPayload->SpawnTrackingCount++;
if (Type == 1 /*Spawn*/ && (Frequency == 0 || count % Frequency == 0)) {
  loc = bUseOrbitOffset(flags&8) ? LocWithOrbit(..) : Particle.Location(+0x18);   vel = Particle.Velocity(+0x40);
  FUN_1431ae800(Component, CustomName, Owner->EmitterTime, &loc, &vel, &EventsToSendToGame);   // ReportEventSpawn
```
- ProcessParticleEvent 143224CB0 (receiver):
```
if (Event.Name != this->EventName(+0x34) || (EventGeneratorType(+0x30) && != Event.Type)) return 0;
count = RoundToInt(SpawnCount(+0x40).GetValue(Event.EmitterTime, 0, 0));       // (int)ROUND(x*2+0.5)>>1
loc = (flags(+0x70)&2 /*bUsePSysLocation*/) ? Owner->Location(+0x50) : Event.Location;
vel = (flags&4 /*bInheritVelocity*/) ? InheritVelocityScale(+0x78)(t) * Event.Velocity : ZeroVector;
Owner->vtbl[0xc0](Owner, DeltaTime, 0, count, &loc, &vel);                     // ForceSpawn(dt, 0, burst=count, loc, vel)
```
  Result: **1 side-pop particle per front-pop particle, at the front-pop particle's Location (its simulation space, local here), with zero inherited velocity**. Its own Velocity/Size/etc. modules then apply (5 cm/s forward). Both emitters are local-space, so they share the frame.
- Not verified in the exe (stock UE behaviour): HandleParticleSpawned runs inside SpawnParticles right after the particle's spawn modules and PostSpawn, so its Location already includes the 25 cm offset and the sub-frame `SpawnTime*Velocity` advance. The component processes queued spawn events after all emitters have ticked in the same frame. ForceSpawn → PreSpawn sets `Location = loc` before the receiver's spawn modules add offsets.

### 4. ColorOverLife / AlphaOverLife and SizeMultiplyLife
- ColorOverLife Spawn 1431E81F0 **sets** `Color.rgb = BaseColor.rgb = COL(RelativeTime)` and `Color.a = BaseColor.a = AOL(RelativeTime)`. Update 1431F09C0 **sets** Color every frame: `if (FUN_142eb8860(COL) && FUN_142eb8860(AOL))` (both Op 1) uses the fast lerp `FUN_142ebc310(tbl, RelativeTime, &Color); FUN_142ebc010(tbl, RelativeTime, &Color.a)`. Otherwise it uses `GetValue(.., RelativeTime, Comp, 0, 0)`, the null stream, which for the pistol core means **a new random colour per channel every frame**.
- SizeMultiplyLife Spawn 14322F560 / Update 143237F90 **multiply**: `Size.X(+0x60) *= LM.x(RelativeTime)` and likewise Y (+0x64) and Z (+0x68), each gated by MultiplyX/Y/Z bits (+0x90; UE defaults are all on, and the dump shows the defaults). The multiply is applied to the current Size each frame. In stock UE, Size is reset to BaseSize at the start of each tick (ResetParticleParameters), so the effective `Size = BaseSize × LM(RelativeTime)`. That reset was not located in this exe.
- RelativeTime runs 0..1 over the life. Curves are linear between the baked entries, clamped at both ends (sampler above).

### 5. Screen alignment and PivotOffset (only partly in the exe)
- EParticleScreenAlignment: 0 FacingCameraPosition, 1 **Square (default; the pops and pistol core)**, 2 Rectangle, 3 Velocity, 4 AwayFromCenter, 5 TypeSpecific, 6 FacingCameraDistanceBlend. CPU vertex fill 14321F730 and light gather 143217D80: `if (align < 2 || align == 6) SizeY = SizeX` (square, camera-facing, uses only Size.X). Rectangle and Velocity keep X and Y separate. The vertex carries Position, OldPosition, RelativeTime, ParticleId, Size (sign flipped for UV flip when BaseSize < 0), Rotation (+0x3c), SubImageIndex and Color.
- PivotOffset CompileModule 1431CDB50: `subsd xmm0,[0.5]; subsd xmm1,[0.5]; movups [rdx+0x458], xmm0`, so EmitterPivot = PivotOffset − (0.5, 0.5).
- Not in the exe (HLSL in the shader cache), stock UE `ParticleSpriteVertexFactory.ush`: `WorldPos = P + Size.x*(uv.x + Pivot.x)*Right + Size.y*(uv.y + Pivot.y)*Up`. PSA_Velocity uses `Up = -normalize(Velocity)` and `Right = normalize(cross(CameraDir, Velocity))`, with rotation ignored. PSA_Square/Rectangle use the camera Right/Up rotated by Rotation. So pivot.y −0.35 → (−0.5, −0.85): the quad spans from 0.15·SizeY behind the particle to 0.85·SizeY ahead along the velocity. Treat this as unverified for RoN's build.
