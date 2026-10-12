# Muzzle flash: how games time and render it, and what we should do

The user: "the flash is still not as snappy as i want, i can still see it expand. flash should be near instant. how do
games handle flash timings, also post processing to make it look amazing".

## TL;DR
- **Good shooters show the flash for 1-2 frames, at full size from the first frame, with no visible growth.** It's
  a random sprite / variant and a random rotation per shot. The dynamic light lasts about as long as the flash
  (~0.035-0.05 s), and HDR bloom does the "glow". Ready or Not's pistol flash lives **0.035-0.04 s**, grows only
  1 → 1.5x over that life, and is HDR-bright (colour ×38,000).
- Ours lives 0.08-0.15 s and grows 0.6 → 1.1x, and the tongues move outward at 1 m/s. That's the expansion the user
  sees.
- The KCD2 constraint (an emitter is first drawn 1-2 frames after it spawns; a particle is pre-aged one frame, so
  < ~0.08 s parts never show) means **a spawned-per-shot flash can't be 1-2 frames**. The fix is a **pre-warmed,
  always-alive flash emitter whose visibility the plugin gates per frame** (section 4.2). Plus a cheap first step:
  constant size, alpha held at 1, a hard cut at the end (section 4.1).
- Post FX: RoN uses **no per-shot post-process**, just HDR bloom, the light and a camera shake. For a little extra
  punch, KCD2 exposes `System.SetPostProcessFxParam` (chroma shift, radial blur, brightness), which we can pulse for
  1-2 frames (section 5).

## 1. Ready or Not, from its data (decoded read-only from the install)
The G19's `MuzzleFlash` component (`Secondary_G19_V2`, ParticleSystemComponent attached to the weapon,
`RelativeLocation` 13.5 cm forward) uses Template **`WeaponFlash_Pistol_V2`** (Cascade). All emitters are local
space (glued to the gun) and each spawns **one particle in a burst at t = 0**:

| emitter | lifetime | size (cm, random) | size over life | sprite | colour (HDR) | other |
|---|---|---|---|---|---|---|
| SmallFlame | **0.035 s** | 15-20 | 0.25 → 1 (x), 0.5 → 1.5 (y) | `MI_MuzzleFlash_Side` 2x2, **random tile** | 382 / 4.5 / 2 | velocity-aligned, pivot at the base |
| Pop (front) | **0.04 s** | 17-32 | 1 → 1.5 | `MI_MuzzleFlash_Rifle` 4x4, tile 0-7 linear | **38,250** / 450 / 200 | **light**: brightness 70-80, radius scale 100 (= 17-32 m), exponent 320, on 80 % of shots (`SpawnFraction` 0.8); random rotation |
| Pop (side, event) | 0.04 s | 20-30 | 0.12 → 0.75 / 0.5 → 2 | Side 2x2 random | 38,250 / 450 / 200 | spawned by the front pop |
| Pop (pistol core) | **0.035 s** | 8-12 | 1 → 1.5 | `MI_MuzzleFlash_Pistol` 4x4, tile 0-11 | 0.85 → 38,250 | **light** brightness 15-50, radius scale 100, exponent 16 |
| Sparks | 0.05-0.085 s | 0.25-0.75 | shrink | additive 4x2 | 25 / 0.27 / 0.19 | 10 at t = 0.01 s, 5-50 m/s, drag 2.5-15 |
| LongSpark | 0.03 s | 25-50 | 0.1 → 2 (y) | additive 4x2 | 1528 / 180 / 8 | axis-locked streak |
| CoreSpark | 0.03 s | 6-12 | 0.5 → 1.8 | additive tile 7 | 1528 / 180 / 8 | random rotation |

So:
- **The flash proper lives 2 frames at 60 fps** (0.035-0.04 s) and is **1 frame at 30 fps**.
- It's **full size on frame 1**. The ×1.5 "growth" happens across those two frames, so it reads as a pop, not an
  expansion.
- **Variation** comes from random tiles from flipbook sheets and random rotation, not animation.
- **The light** lives exactly as long as the pop, is huge in radius and very bright, and is skipped on 20 % of shots.
- `BaseMagazineWeapon.MuzzleFlashChance` exists, so a flash may not show every shot. Its C++ default wasn't
  readable; the light's `SpawnFraction` 0.8 is.
- Barrel smoke (`P_WeaponSmoke_G19_Muzzle`) and heat haze (`P_BarrelSmokeTrail`, `CustomTimeDilation` 5) are
  separate, longer effects.
- **Post-process:** `PlayerPostProcessing` lists 21 effects (Radio Flash, NVG, Gas Mask, Blood, Bleed, Healing,
  Death, Injury, Taser, Heartbeat, **Suppression**, Flashbang, Stinger, Compliance, WeaponHighlight…). **None is tied
  to firing.** The firing punch is the HDR sprite + global bloom, the 0.04 s light, `FireCameraShake` (e.g.
  `C_Generic_Fire_Shaky_G19`) and the recoil. Suppression post-FX plays when you're **shot at**.

## 2. Other shooters (web sources, see the list at the end)
- **Source / CS:** the classic `C_BaseAnimating::ProcessMuzzleFlashEvent` light lives **0.05 s**, radius 32-64
  (entity light). The "light up the world" variant uses radius ~245-256, warm yellow-orange, die time 0.05 ± 0.01 s.
  Muzzle particles are separate, short and synced to the shot sound.
- **General practice** (VFX guides, sprite-pack authors):
  - the flash is visible ~60-100 ms in total, **with the sprite shown for a single frame** and then particles / smoke;
  - pick one flash from a set at random per shot;
  - rotate it at random;
  - choose the variant **once at spawn** (re-rolling every frame makes it flicker).
- **CoD (MW2019 / MWII):** no public frame-by-frame numbers. They're known for very bright, HDR-heavy flashes with lots
  of smoke (players complained they obscure the target), and Infinity Ward cut muzzle smoke opacity after feedback.
  So bright and short beats big and long.
- **Tarkov / Insurgency: Sandstorm / Battlefield:** no public numbers found. Sandstorm and RoN are UE titles with the
  same pattern (short Cascade / Niagara bursts plus a light). Treat RoN's decoded data as the representative modern
  tactical-shooter reference.

## 3. Why ours expands (today, `src/Libs/Particles/deagle.xml`)
| part | life | size over life | motion |
|---|---|---|---|
| bloom | 0.10 / 0.13 s (large) | 0.65 → 1 → 1.1 | – |
| front | 0.11 / 0.14 s | 0.6 → 1 → 1.15 | – |
| tongues | 0.10 / 0.13 s | 0.7 → 1 | **Speed 1** (outward) |
| sparks | 0.15 / 0.19 s | – | Speed 2 |
| core | 0.08 / 0.10 s | – | – |

At 60 fps that's 6-9 frames of a growing, outward-moving flash, which is what the user sees. RoN's is 2 frames.

## 4. Recommendation
### 4.1 Now, data only (the xml, no code)
Make the flash **pop, not grow**:
1. Every flash part (bloom, front, core, tongues) gets **constant size** (no `ParticleAge` size curve, or a 1.0 / 1.0
   curve) and **no speed** (tongues `Speed 0`; length comes from `Aspect` / `Stretch`).
2. **Alpha held at 1, then a hard cut:** `Alpha="1,ParticleAge=(v=1;t=0.8,v=1;t=1)"`. It's bright, then gone, with
   no visible fade.
3. **Lifetime 0.08 s** (the shortest KCD2 draws: pre-age 1 frame + first draw 1-2 frames later), with no Random on
   it. Visible time then becomes ~2-3 frames at 60 fps, versus RoN's 2.
4. **Variation:** `TextureTiling` `VariantCount` (random tile per particle) + `RandomAngles` on the front / bloom
   (RoN: random rotation; we already randomise `y=180`).
5. **Brighter, smaller:**
   - core `EmissiveLighting` 12 → 25-40, front 3 → 8;
   - sizes ~20 % smaller, since a sharper and brighter flash reads as faster;
   - let bloom (`r_HDRBloom`) make the glow, instead of a big soft sprite.
6. **Light:** match the flash. Keep the LightSource but drop the ramp (`Intensity` constant over age, like RoN's
   brightness 70-80 over its 0.04 s). Leave it off on ~20 % of shots (RoN `SpawnFraction` 0.8), done from Lua by
   choosing between two effects (`muzzle` / `muzzle_nolight`).

### 4.2 Near-instant: a pre-warmed flash emitter the plugin gates (code, proposal)
The KCD2 delay comes from **spawning**: the emitter isn't drawn for 1-2 frames, and its first particles are pre-aged.
So don't spawn per shot:
1. On draw, spawn **one persistent flash emitter** (plugin-owned, as today: `FindEffect` → `Spawn` → kept with
   `AddRef`, `SetLocation` each camera step, which already works).
   - Its effect is `Continuous` with a high rate, so particles of 0.03-0.05 s always exist. It has a valid bbox, and
     the renderer always draws it.
2. All its visible params are driven by **emitter strength**: `Alpha="1,EmitterStrength=(v=0;t=0.5,v=0;t=0.51,v=1;t=1,v=1)"`,
   and the same for the LightSource intensity.
3. The plugin sets strength **1** for exactly N **drawn** frames after a shot (N = 1 at <= 40 fps, 2 above), then
   **0**. It counts with the frame counter it already has (`g_viewFrames`) rather than the bbox, since the emitter is
   already being drawn.
   - Call: `IParticleEmitter::SetSpawnParams(SpawnParams{ fStrength })`. The vtable offset is to find, the same way
     as `SetLocation` (+0x2B0).
   - **Unverified:** whether legacy pfx1 re-evaluates `EmitterStrength` curves on live particles each frame (it
     does for spawn-time params). If it only applies at particle spawn, a high spawn rate (particles 0.03 s) still
     makes the change visible next frame.
4. Random look per shot comes for free: the particles alive at that moment have random tiles and rotations.
5. **Test:** 240 fps capture, count the frames between the shot sound / `kick` and the first and last flash frame.
   Target 1-2 frames, zero growth. Also check `deagle_slowmo` (F9).

Fallback if strength gating doesn't work: two pre-spawned emitters alternating, each toggled with
`IParticleEmitter::Activate(bool)` (another vtable), or (as the user prefers particles) keep 4.1.

### 4.3 What the flash should be (shape and light)
- **Frames:** 1 at <= 40 fps, 2 at 60+.
- **Shape:** a 3-4 petal front star (random tile from a 2x2 / 4x4 sheet, random roll) + a small, very hot core + 2-3
  side tongues, velocity-aligned, set by aspect, not motion. Deagle size ~1.5x the 9 mm, per our gas scale.
- **Light:** one dynamic light, ~0.04 s, radius 7 m (9 mm) / 21 m (Deagle) as today, constant intensity, ~20 % of
  shots skipped.
- **After:** sparks 0.05-0.1 s (fine at 0.08+), smoke and barrel wisp (already good).

## 5. KCD2 post-processing we can drive per shot (found in WHGame.dll)
KCD2 has CryEngine's Lua binds **`System.SetPostProcessFxParam(name, value)`** / `GetPostProcessFxParam`, and these
parameters among others. Pulse them for 1-2 frames from Lua on the shot, then restore the value read beforehand:

| param | use | suggested pulse |
|---|---|---|
| `FilterChromaShift_User_Amount` | tiny chromatic split on the shot (Deagle only) | 0.15-0.3 for 2 frames (subtle; CoD-like) |
| `FilterRadialBlurring_Amount` (+ `_ScreenPosX/Y` 0.5, `_Radius`) | a hint of punch | 0.05 for 1 frame (Deagle); mostly skip |
| `Global_User_Brightness` | exposure blip from the flash, dark scenes only | +0.03-0.06 for 1 frame (scale by darkness; skip in daylight) |
| `FilterArtifacts_ChromaShift`, `FlashBang_*`, `Dof_User_*` | exist; flashbang is for grenades, DOF isn't for shots | don't |

Engine cvars (not per shot): `r_HDRBloom`, `r_HDRBloomQuality` (keep on: the HDR sprite blooms),
`r_ChromaticAberration`, `r_Flares` / `r_FlaresChromaShift` (lens flares, for **light entities** with a flare from
the LensOptics library, not particle lights; a per-shot flare would need a light entity, so skip),
`r_MotionBlur*` (don't change).

Particle-side "glow" in legacy pfx1 is **`EmissiveLighting`** together with **HDR Dynamic** (CryEngine docs: "Power
to apply to engine HDR multiplier, for emissive lighting in HDR") and the LightSource **HDR Dynamic**. There's no
separate glow / flare param on particles. Also from the docs:
- `Draw Near`: the first-person weapon render pass, worth trying on the flash so it never clips;
- `Bind Emitter To Camera`;
- `Min Pixels`: keeps a tiny core visible at distance.

Camera: a sharp ~0.05 s micro-shake on the shot (RoN `FireCameraShake`). Our recoil camera kick already does this.

## Sources
- Ready or Not data, decoded read-only from the user's install:
  - `Assets/Particles/WeaponFlash/WeaponFlash_Pistol_V2` (Cascade);
  - `Blueprints/Items/WeaponsRevised/Secondary_G19_V2` (MuzzleFlash component);
  - `Blueprints/Characters/BasePlayer` (PlayerPostProcessing effects);
  - see also `docs/ron/ron_impact_fx.md`.
- KCD2 `WHGame.dll` strings: `CScriptBind_System::SetPostProcessFxParam`, `FilterChromaShift_User_Amount`,
  `FilterRadialBlurring_*`, `Global_User_Brightness`, `FlashBang_*`, `r_HDRBloom`, `r_Flares`,
  `r_ChromaticAberration`.
- [CRYENGINE 3 Particle Reference](https://cryengine.com/docs/static/engines/cryengine-3/categories/1114113/pages/1048585): Spawn Delay, Pulse Period, LightSource HDR Dynamic, Draw Near, Bind Emitter To Camera, Variant Count, Min Pixels.
- [CRYENGINE 5 particle docs](https://cryengine.com/docs/static/engines/cryengine-5/categories/23756816/pages/36868327) (spawn Delay / Restart).
- [Valve Developer Community: Adding Muzzle Flashes that lights up the world](https://developer.valvesoftware.com/wiki/Adding_Muzzle_Flashes_that_lights_up_the_world) and [Muzzle Flash Lighting](https://developer.Valvesoftware.com/wiki/Muzzle_Flash_Lighting): Source's 0.05 s muzzle light, radius 32-64 / ~256.
- [Source SDK c_baseanimating.cpp (mirror)](https://swarm.workshop.perforce.com/files/guest/knut_wikstrom/ValveSDKCodecl_dll/c_baseanimating.cpp).
- [Source Filmmaker community thread on muzzle flash particles](https://steamcommunity.com/app/1840/discussions/0/135512931349995117) (each flash is a separately timed particle, synced to the shot).
- [OpenGameArt muzzle flash sets](https://opengameart.org/node/179598) / [gun muzzle flash effects](https://opengameart.org/content/gun-muzzle-flash-effects-fire-and-ion-and-melee) (show one sprite for a frame, random pick per shot).
- [Unity discussion: muzzle flash for rapid-fire weapons](https://discussions.unity.com/t/muzzle-flash-not-reliable-for-rapid-fire-weapons/89353), [Construct forum: pick the random anim once](https://www.construct.net/forum/construct-2/how-do-i-18/choose-random-anim-67242).
- [PC Gamer: MW4 blinding muzzle flash bug](https://www.pcgamer.com/games/call-of-duty/the-blinding-muzzle-flash-in-modern-warfare-4s-gameplay-premiere-was-actually-a-bug/), [Dexerto: devs respond](https://www.dexerto.com/call-of-duty/modern-warfare-4-devs-respond-after-players-cant-see-what-theyre-shooting-3388482/) (bright / smoky flashes obscuring targets in MW2019 / MWII, smoke opacity reduced).

## 6. RE for 4.2: emitter vtable, SpawnParams, strength gating (WHGame 1.5.6, static)
**`CParticleEmitter` vtable** (RVA 0x3A55F48, `.?AVCParticleEmitter@@`, offset 0). Anchored on SetLocation +0x2B0:

| offset | method | evidence |
|---|---|---|
| +0x250 | `IsAlive()` | age +0x64 vs stop time +0x1D0; PulsePeriod (+0x128) > 0 keeps it alive |
| +0x258 | `Activate(bool)` | branches on `dl` |
| +0x260 | `Kill()` | sets stop times +0x68 / +0x1D0 = -1e9, re-registers (0x422608) |
| +0x268 | `Restart()` | resets age +0x64 and the stop times |
| +0x270 / +0x278 | `SetEffect(eff)` / `GetEffect()` | effect at +0xF0 |
| +0x280 | `SetEmitGeom(const GeomRef&)` | compares +0xB0..+0xC0 |
| **+0x288** | **`SetSpawnParams(const SpawnParams&)`** | compares against +0x110, copies with 0x7390CC; only a size / speed / attach change calls the reset 0x422F28; then re-registers |
| **+0x290** | **`GetSpawnParams(SpawnParams& out)`** | `out <- this+0x110` (0x7390CC) |
| +0x298 | `SetEntity(entity, slot)` | +0x1E0 entity, +0x1D8 slot |
| +0x2A8 | `GetLocation(QuatTS& out)` | copies this+0x70 |
| +0x2B0 | `SetLocation(const QuatTS&)` | (known) |

**SpawnParams** (0x48 bytes; the emitter's copy is at **emitter+0x110**). Names from the ParticleEffect entity
reader 0x4598F0 (it fills an embedded copy at entity+0x70); defaults from the ctor 0x422DA4:

| off | type | name | default |
|---|---|---|---|
| 0x00..0x06 | bytes | flags (0x02 = bRegisterByBBox; others unnamed) | 0,0,0,0,0,1,1 |
| 0x08 | float | fCountScale | 1 |
| 0x0C | float | fSizeScale ("Scale") | 1 |
| 0x10 | float | fSpeedScale | 1 |
| 0x14 | float | fTimeScale | 1 |
| 0x18 | float | fPulsePeriod | 0 |
| **0x1C** | **float** | **fStrength** | **-1** (= "use emitter age") |
| 0x20 | int | nSeed | -1 |
| 0x28 | int | eAttachType | 0 |
| 0x2C | int | eAttachForm | 2 |
| 0x30 / 0x31 | bool | bCountPerUnit / bEnableAudio | 0 / 1 |
| 0x34 | int | eSoundObstructionType | 1 |
| 0x38 | string | audio RTPC (CryString: copy it, never memcpy) | "" |
| 0x40 | bool | bLODUpdateEnabled | 1 |

**Setting strength:** `GetSpawnParams(tmp)` → `tmp.fStrength = s` → `SetSpawnParams(tmp)`. Copy with the
engine's own Get (the 0x38 CryString is ref-counted), only change +0x1C, and call it only on change. A strength-only
change doesn't reset the emitter. Read-back for debugging: `*(float*)(em + 0x12C)`.

**Do live particles follow strength?** Not proven statically. In CryEngine's legacy pfx1, `EmitterStrength` curves
are evaluated when a particle is **emitted** (it stores the result as a per-particle mod). Particle-age curves run
every frame, emitter-strength ones don't (**guess**, from engine design). So use a **continuous, high-rate emitter
with very short particle life** (0.02-0.04 s, `Count` high), with Alpha / Size / LightSource intensity keyed on
EmitterStrength. A strength change then shows on the next rendered frame through fresh particles, and the old ones die
within 1-2 frames.

**A "drawn this frame" signal:** none found on the emitter. None of its vtable methods stores a pass / frame id. Since
the flash emitter is persistent and always in view, use our own **`g_viewFrames`** (incremented once per
distinct drawn camera frame) as the frame clock. That's exactly "N drawn frames".

**C++ sketch** (plugin; main thread / camera step; reuses SpawnFlash / SafeSetLocation / SafeRelease):
```cpp
using GetSPFn = void(__fastcall*)(uintptr_t em, void* out);       // +0x290
using SetSPFn = void(__fastcall*)(uintptr_t em, const void* sp);  // +0x288
static uintptr_t g_flashEm = 0; static uint64_t g_flashUntil = 0; static float g_flashStr = -2;
static bool SafeSetStrength(uintptr_t em, float s) {
    __try {
        alignas(16) uint8_t sp[0x48];
        auto get = (GetSPFn)VFn(em, 0x290); auto set = (SetSPFn)VFn(em, 0x288);
        if (!get || !set) return false;
        get(em, sp);                       // engine copy (string inside stays valid)
        *(float*)(sp + 0x1C) = s;
        set(em, sp);
        return true;   // NB: sp's CryString at +0x38 now holds a ref: let the engine's SpawnParams dtor run if one
                       // is found; else copy only on the stack once and keep it alive (leak 1 ref) - see note below
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
// "flasharm <effect>": on draw, spawn once (strength 0) and keep it (AddRef as today)
// "flashshot <frames>": g_flashUntil = g_viewFrames + frames
// every camera step (OnCamera, after the viewmodel is placed):
//   SafeSetLocation(g_flashEm, muzzleLoc);
//   float want = g_viewFrames < g_flashUntil ? 1.f : 0.f;
//   if (want != g_flashStr && SafeSetStrength(g_flashEm, want)) g_flashStr = want;
// "hello" (level load) / holster: SafeRelease(g_flashEm); g_flashEm = 0; g_flashStr = -2;  (re-arm on next draw)
```
Note on the CryString at +0x38: the struct copy from GetSpawnParams bumps its ref-count. Two clean options: find
SpawnParams' dtor (it would call the string release 0x4FC624 on +0x38), or call Get once at arm time into a static
buffer and reuse that buffer for every Set (one ref, held for the emitter's life). The static-buffer way is the
simplest.

Fallbacks: `Activate(false/true)` (+0x258) per shot on a pre-spawned emitter (cheaper than spawning, but may still
lose a frame to the first emission); `Restart()` (+0x268) to re-burst an instant emitter in place.
