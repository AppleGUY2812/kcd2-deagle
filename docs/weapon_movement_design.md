# Weapon movement layer: design (roadmap item 4)

Goal: hands and gun move with the player the way they do in Ready or Not (RoN). That covers walk / run bob, look
sway (inertia drag), a sprint pose, jump and land kicks, a crouch transition and a dodge kick, as one generic layer
for every gun.

Sources:
- `docs/ron/ron_movement.json` / `ron_movement_table.md`: RoN's numeric movement properties.
- `docs/ron/ron_movement_curves.json` (new): RoN's **baked** sprint / jump / crouch / loco animations, decoded and
  converted to our camera axes.

Units here: m, deg, s, and our plugin camera space (x right, y forward, z up). Rotations follow `LocalRot`: pitch
about x, roll about y, yaw about z.

## 1. What we have today
### Native (`native/src/main.cpp`, `UpdateMotion`, the `mo` struct, `ResetMotion`)
Everything is computed from the **camera's own movement**: position and forward vector each processed frame, and dt
from QPC. Lua sends nothing per frame except `aim`, `kick`, `motion <scale>` and `off`.

- **Springs:** `Spring{x,v}.step(target,k,c,dt)` is an implicit (backward-Euler) damped spring, stable at any dt. A
  frame with dt > 0.25 s or a jump > 15 m/s resets the reference (teleports, cuts).
- **Speed:** horizontal speed `hs` from the camera position delta, smoothed with tau 0.15 s into `mo.speed`. Vertical
  speed is `vz`.
- **Aim fade:** `aimK` spring (k 200, c 28) toward 0/1. `hip = 1 - 0.85*aimK`, so 85 % of bob and sway fade while
  aiming.
- **Turn sway:** `yaw`, `pitch` springs (k 120, c 18 → ω 11 rad/s, ζ 0.82) toward `-yawRate*0.012` deg
  (clamp ±2.5) and `-pitchRate*0.010` deg (clamp ±2). Rates are in deg/s. Output: rot.z = yaw, rot.x = pitch,
  rot.y = 0.5*yaw (roll).
- **Bob:**
  - phase += hs*dt/0.75*π (one step per 0.75 m); amplitude a = min(1, speed/5).
  - x = 6 mm·sin(phase)·a, z = -5 mm·|cos(phase)|·a, roll += 0.6°·sin(phase)·a.
  - All faded by `hip`.
- **Sprint pose:**
  - `sprint` blends with tau 0.2 s to 1 when speed > 4.6 m/s and not aiming.
  - Pose: off (+1.5 cm x, -1 cm y, -3 cm z), rot (pitch -14°, roll +12°, yaw +6°).
  - It's a pure first-order blend: no start / stop overshoot and no sprint-loop motion.
- **Landing:**
  - After > 0.25 s with vz < -2.5 m/s, `land.v -= min(0.25, -vz*0.03)`.
  - `land` spring (k 160, c 16) drives off.z by `0.15*land.x`, giving a small dip.
  - No jump-start kick, and no fall-height curve beyond that linear clamp.
- **Shot kick:** `kickBack / kickPitch / kickYaw / kickRoll` (k 784, c 50 → ω 28 rad/s, ζ 0.89), impulses from Lua
  (`kick b p y r`).
- **Output:** `mo.off` (m) / `mo.rot` (deg) × `mo.scale` (`motion` cmd, `cfg.vm.motion`, `deagle_motion 0..2`). The
  shot kick is not scaled. `PlaceViewmodel` composes `cam · LocalRot(rot)` and `cam.pos + C·(g_off + off)`.
- **Missing:**
  - no crouch transition, no dodge, no strafe-dependent sway or roll, no inertia drag from strafing;
  - no jump-start pose, no sprint start / stop shape;
  - no per-gun parameters (one global set, scaled).

### Lua (`deagle_main.lua`)
- `cfg.vm.motion = 1.0` → `NativeSyncVm()` sends `motion %.2f`.
- `deagle_motion <0..2>` changes it.
- `SetAim` sends `aim`; the shot sends `kick`.
- `VmFollow` / `VmPlace` place the viewmodel (Lua fallback when the plugin isn't there) using `cfg.vm.offset`.
- There's no Lua-side motion of its own.

## 2. RoN's numbers mapped onto ours
RoN's runtime code is native and was not read. The meanings below come from names and ranges (**inference**).

| RoN property (G19 / Deagle-like Python .357) | what it does in RoN (inferred) | ours today | proposal |
|---|---|---|---|
| `InertiaDragAimRotation` 0.6 / 0.6 | gun rotation lags camera rotation by this fraction (drag of the mesh-space aim interp) | turn sway targets -0.012 °/(°/s) | keep the turn-rate model, but scale the target gain by `dragAimRot`: gain = 0.02 × dragAimRot °/(°/s) → G19 0.012 (today's value) |
| `InertiaDragAimLocation` 0.4 / 0.875 | gun translates against the turn (x for yaw, z for pitch) | none | new `swayPos`: off.x = -yawRate × 0.00004 × dragAimLoc m/(°/s), off.z = -pitchRate × 0.00004 × dragAimLoc. At 180 °/s the G19 shifts 2.9 mm, the Python 6.3 mm |
| `InertiaDragStrafeRotation` 0.4 / – | gun rolls / yaws against strafe velocity | none | new: roll target = -vRight × 1.5 × dragStrafeRot °/(m/s) (G19: 0.6° per m/s, 2.4° at a 4 m/s strafe); yaw = -vRight × 0.5 × dragStrafeRot |
| `InertiaDragStrafeLocation` 0.2 / 0.05 | gun shifts against strafe / forward velocity | none | new: off.x = -vRight × 0.004 × dragStrafeLoc m/(m/s) (G19 0.8 mm per m/s), off.y = -vFwd × 0.004 × dragStrafeLoc |
| `MovementSpeedScale` (10, 10, 5) | mesh-space offset per normalised velocity (cm at full walk speed) | none | new `moveScale` (cm at 1.0 = 3.2 m/s, RoN RunSpeed 320): off.xyz -= (vRight, vFwd, vUp)/3.2 × moveScale/100 × 0.3. The 0.3 is a **guess** so the gun doesn't swim (RoN combines it with MeshSpaceAimInterp) |
| `MovementSpeedRotationScalePitchYawRoll` (60, 0, 0) / Python (35, 1, -2) | pitch per normalised forward speed | none | pitch -= vFwd/3.2 × 60 × 0.05 = 3° at full walk (G19), with the same 0.05 guess |
| `MeshSpaceAimInterp` 55 (G19), 15 (M9/PFC9) | interp speed of the mesh-space pose (UE FInterpTo speed, 1/s) | springs k 120 | the follow spring for the movement offsets: ω = MeshSpaceAimInterp/5 rad/s (55 → 11, our current ω), ζ 0.8. 15 → ω 3: lazier |
| `CameraRotationRateMeshSpaceMultiplier` (-2.5, -2.5, 0) (player) | mesh offset per camera rotation rate | turn sway | is the swayPos sign / scale above (x from yaw, z from pitch) |
| `MeshspaceRecoilMovementMultiplier` (2,2,2), `MinMax` (2,2,2) | recoil moves the mesh, capped | shot kick | already covered by our kick springs |
| LazySpringBone on `weapon_right`: SpringStiffness 100, SpringDamping 8 (Stiffness 0.35, Mass 0.9, Damping 0.925, Gravity 0.75, DrivingForceStrength 3.5) | the gun hangs on a soft spring off the body (UE AnimNode_SpringBone: k = 100, c = 8 → ω 10 rad/s, ζ 0.4) | yaw/pitch springs ζ 0.82 | new **hang spring** for translation lag (bob, strafe, land, dodge): k 100, c 8 (ζ 0.4, 1.6 Hz, overshoot ~25 %). This is what makes RoN's gun feel heavy and alive; ours is too damped. Gravity 0.75: a constant sag, folded into the idle offset |
| `LazySpringStrength` / `LazySpringStrengthADS` (USP 1.8 / 1.5, G19 -/-1) | per-gun scale of that spring's drive | – | `hang` scale per gun (default 1, ADS 0.3) |
| `FreeAimInterpSpeed` 20, `ADSModifier` 0.5, `HipModifier` 1, `FreeAimLimit` 0/1/3, `FreeAimLimitADS` | free-aim deadzone: the gun points off-centre and catches up | none | optional later: deadzone (deg) that the gun lags inside before the camera drags it (FreeAimLimit 1-3°) |
| `QuickLeanMultiplier` / `FreeLeanMultiplier` 200 (G19), `LeanSpeedMultiplier` 0.5 | lean drive | none (KCD2 has no lean) | skip |
| `SpeedModifier_Aim` 0.85, `SpeedModifier_Sprint` 1.05, `SprintFovFactor` 1.15 / 0.9 s | player speeds, FOV | – | sprint FOV is out of scope (camera), noted |
| `FP_ADS_Motion_Weight` 1, aim fade | how much motion stays in ADS | hip = 1 - 0.85·aim | keep: bob 15 %, sway 15 %, hang spring 30 %, sprint 0 (sprint ends ADS) |
| `CameraBob*` / `WeaponBob*` (21 fields) | procedural bob | our bob | **values unread** (C++ defaults); bob is calibrated from the baked loco curves instead (section 3.4) |

## 3. The baked parts as procedural targets
I decoded RoN's FP animations (`AnimCompress_PerTrackCompression`, additive vs frame 0, 30 fps). I took the
`weapon_right` bone, which carries the gun, and the `fp_camera` bone, and converted them into our axes
(`ron_movement_curves.json`, columns t, x, y, z, pitch, roll, yaw). Roll and yaw **sign** come from the axis mapping
and need one in-game check (section 5, check 1). Below, the numbers are read off the curves.

### 3.1 Sprint (pistol set `fp_g19_add_loco_sprint_*`, rifle set `fp_m4a1_add_sprint_*`)
| | pistol (G19 set) | rifle (M4A1 set) |
|---|---|---|
| loop mean pose (off m / rot deg) | x -0.008, y -0.031, **z -0.118**; pitch **-8.7**, roll **-35.5**, yaw +1.2 | x +0.038, y -0.010, z -0.066; pitch -0.5, roll -23.3, yaw **+31.8** |
| loop wobble (amplitude / period) | x ±4.2 cm @0.64 s, z ±1.2 cm @0.31 s, pitch ±9° @0.37 s, roll ±5° @0.36 s, yaw ±6° @0.65 s | x ±4.2 cm @0.64 s, pitch ±5° @0.63 s, roll ±5.5° @0.31 s, yaw ±3.7° @0.64 s |
| start (0.93 s clip) | z reaches 63 % at 0.30 s, roll at 0.43 s; pitch dips to -11.8° at 0.60 s (overshoot), yaw +12° at 0.23 s | roll / yaw reach 63 % at 0.63-0.67 s |
| stop (1.0 s clip) | starts at the loop pose; pitch -8.5° at 0.37 s, back to 0 by ~0.7 s (z ends at 0.0 at 1.0 s) | z dips to -8.6 cm at 0.30 s, then returns |
| camera (`fp_camera`) | pitch ±0.4°, roll ±0.35°: the camera barely moves | same |

Procedural version (per gun, `sprintPose` table; pistols use the pistol row):
1. **Target pose** = loop mean (above). This replaces today's (1.5, -1, -3 cm; -14, +12, +6°): the roll is about 3x
   bigger and on the other side, and the gun drops 12 cm.
2. **Blend in:** spring toward the pose with ω 7 rad/s, ζ 0.7. That reaches 63 % at ~0.3 s and overshoots ~5 % in
   pitch, matching the start clip. **Blend out:** ω 6, ζ 0.75 (~0.6 s, like the stop clip).
3. **Loop wobble while sprinting**, driven by the step phase (period 0.64 s = one stride, half period 0.32 s):
   - x = 0.042·sin(φ)
   - z = 0.012·sin(2φ)
   - pitch = 9·sin(2φ + 0.3)
   - roll = 5·sin(2φ)
   - yaw = 6·sin(φ + 0.5)

   Amplitudes are the pistol row, times the sprint blend. φ advances with distance (stride 0.64 s × sprint speed),
   so it locks to the footsteps.
4. **Sprint-to-fire:** RoN has no numeric delay; the stop clip takes ~0.6 s. Proposal: firing within 0.25 s of
   leaving sprint forces the blend-out spring to 2x stiffness (snap up), and the shot waits until the pose is
   < 30 % (about 0.15 s).

### 3.2 Jump (`fp_gen_add_jump_start / loop / end`)
- **Start (0.73 s):** gun dips **-3.5 cm z at 0.17 s**, then rises to +2.4 cm. Roll swings -5.8° (0.33 s) → +5.2°,
  yaw -2.2° → +1.6°. The camera pitches up +2.9° at 0.53 s.
- **Loop (in the air, 2.5 s):** held pose z +1.9 cm, roll +4.9°, yaw +1.6°, pitch -0.6°, with a slow drift ±2.7°
  roll. The camera pitch is +2-3.5°.
- **End / land (0.87 s):** **pitch -6.1° at 0.20 s**, z -2.7 cm at 0.20 s; roll goes from +5.2° to 0, and
  everything settles by ~0.6 s.

Procedural version:
1. **Takeoff** (in-air goes true, or vz jumps up past +2 m/s): impulse on the hang spring so z peaks at -3.5 cm after
   ~0.17 s. With k 100, c 8, an impulse v0 = -0.035·ω·e ≈ -0.95 m/s peaks near 0.15 s. Then target the air pose.
2. **Air pose** (spring ω 5, ζ 0.8): z +0.019, roll +4.9°, yaw +1.6°, pitch -0.6°, plus roll 2.5°·sin(1.3·t).
3. **Landing** (in air for > 0.15 s, then grounded):
   - pitch impulse to peak at -6° at 0.2 s;
   - z impulse to -2.7 cm;
   - air pose released, settling by 0.6 s.
   - **Fall-height scale:** RoN doesn't scale its landing anim. Proposal: k = clamp(-vz_land / 4.5, 0.5, 2) on both
     impulses (4.5 m/s ≈ a 1 m drop; KCD2 jump ≈ 3-4 m/s). This replaces today's `min(0.25, -vz*0.03)`.

### 3.3 Crouch (`fp_m4a1_stand_to_crouch`, `crouch_to_stand`, 1.0 s each)
- **Stand → crouch:**
  - pitch -2.2° at 0.47 s, roll -2.0° at 0.43 s, yaw +1.0° at 0.27 s;
  - x -1.3 cm, y +1.5 cm at 0.33 s;
  - back to 0 at the end (it's a transition, not a held pose).
- **Crouch → stand:** pitch -2.6° at 0.40 s, roll -3.2° at 0.30 s, yaw +1.6° at 0.17 s, z -2.3 cm at 0.27 s.

Procedural version: on an eye-height change (section 4), fire impulses that peak at those values and times. Use the
hang spring for the offsets, plus a rotation spring ω 8, ζ 0.6 (peak ~0.25-0.3 s, done by ~0.8 s). Crouched, RoN
has no extra pose; ours stays the same pose, only lower with the camera.

### 3.4 Walk / jog bob (`fp_m4a1_add_loco_m_*` walk 3.07 s, `fp_m4a1_add_loco_f_*` jog 0.87 s)
The baked arm loco is **tiny**: walk ±1-2 mm, ±0.2-0.6°; jog ±3 mm, pitch ±0.58° @0.42 s, roll ±0.44° @0.44 s,
yaw ±0.9°. RoN's visible bob comes from the procedural `WeaponBob*` / `CameraBob*` (unreadable defaults) and
the hang spring. Proposal: keep our distance-phased bob but make it RoN-sized and spring-filtered:
- x = 3 mm·sin(φ), z = -3 mm·|cos(φ)|, pitch = 0.6°·sin(2φ), roll = 0.45°·sin(2φ), yaw = 0.9°·sin(φ).
- φ advances one step per 0.75 m as now. Amplitude ∝ min(1, speed/4).
- Feed it through the hang spring rather than adding it directly, so it lags and overshoots softly.
- This is about half of today's 6 / 5 mm, plus the rotations RoN has.

### 3.5 Dodge (KCD2 only; RoN has none)
Kick like a strong strafe:
- hang-spring impulse off.x = -0.04 m × side (peak ~0.12 s);
- roll impulse 8° × side; yaw 3° × side.
- It recovers on the springs (~0.5 s).
- Sized by the measured sideways speed: k = clamp(vSide / 5, 0.5, 1.5).

## 4. Inputs (KCD2 Lua, live-checked by the Deagle session) and how to feed them
| input | source | use |
|---|---|---|
| velocity (m/s) | `player:GetVelocity()`; walk ~1.5-2, run ~4-6 | in camera space: vRight = v·camRight, vFwd = v·camFwd (horizontal), vUp = v.z. Today native derives speed from the camera; send `vel x y z` each Lua tick, or keep the camera-derived value (it includes head bob) |
| in air | `player.actor:IsFlying()` | takeoff / air / landing (section 3.2). Edge-detect in Lua and send `air 0|1` |
| crouch | eye height = camera z - feet z: 1.54 standing, 1.02 crouched | crouch if < 1.28; edges send `stance <0|1>` → section 3.3 impulses. Native could also watch the camera-z step at ~zero horizontal speed |
| mouse look | camera forward vector each frame (native already has it) | turn rates for sway (unchanged) |
| sprint | speed > 4.6 m/s (today), or KCD2's sprint input if exposed | use speed with hysteresis: in at > 4.6, out at < 3.8 |
| **dodge** | no direct flag known | detect in Lua: horizontal velocity in the camera-right (or back) direction rising > 3.5 m/s within 0.2 s while not sprinting and not airborne. That's a sideways speed spike: KCD2's dodge is a quick lateral hop (~0.4 s). Confirm by logging `GetVelocity()` during dodges (test 6). If KCD2 exposes an action / animation event for dodge (e.g. a movement-state query on `player.actor`), prefer it |

Proposed native commands, extending today's: `vel <x y z>` (camera-relative m/s), `air <0|1>`, `stance <0|1>`,
`dodge <side -1..1> <strength>`, and `mparams <key> <value>` for the per-gun numbers below.

### Per-gun parameter table (proposal: `cfg.vm.motion` → per gun `motion = { ... }`, defaults = G19)
| key | unit | G19 default | Deagle (Python .357 values) |
|---|---|---|---|
| dragAimRot / dragAimLoc | - | 0.6 / 0.4 | 0.6 / 0.875 |
| dragStrafeRot / dragStrafeLoc | - | 0.4 / 0.2 | 0.4 / 0.05 |
| moveScale | cm at full walk | 10, 10, 5 | 10, 10, 5 |
| moveRotPYR | deg at full walk (×0.05) | 60, 0, 0 | 35, 1, -2 |
| meshInterp | 1/s → ω = /5 | 55 | 55 |
| hang k / c / ADS scale | 1/s², 1/s, - | 100 / 8 / 0.3 | 100 / 8 / 0.3 (heavier gun: k 80) |
| sprintPose off / rot | m / deg | (-0.008, -0.031, -0.118) / (-8.7, -35.5, 1.2) | same (pistol set) |
| sprintWobble | m, deg | see 3.1 | same |
| jump / air / land | m, deg, s | see 3.2 | same, land × 1.2 (heavier) |
| crouch | deg, s | see 3.3 | same |
| bob | mm / deg | 3 / 3 mm; 0.6 / 0.45 / 0.9° | same |

## 5. Test plan (record in game, compare side by side with RoN)
Record 60 fps video with a fixed FOV. Run each test once in KCD2 with the Deagle and once in RoN with the G19 (and an
M4 for the rifle set). Then extract frames with ffmpeg into contact sheets with timestamps
(`Desktop\Deagle Progress`).
1. **Axis / sign check (first):** stand still and toggle a pure +10° roll test value (`mparams test_roll 10`). Confirm
   it tilts the top of the gun the same way as RoN's sprint pose (top toward the left). If not, flip the roll and yaw
   signs of the curve data.
2. **Sprint:** stand → sprint 3 s → stop on flat ground. Compare at t = 0.1, 0.3, 0.6, 1.0 s after the sprint key,
   one sprint stride, and 0.2 / 0.4 / 0.7 s after release. Measure gun drop (px) and roll angle against RoN.
3. **Look sway:** flick the mouse 90° in 0.25 s left and right while standing still, then hipfire and ADS. Compare
   the gun's lag (px) and settle time (ζ 0.4 overshoot vs RoN).
4. **Strafe:** A / D at walk, then run, 2 s each. Compare roll and offset against strafe direction.
5. **Jump:** a standing jump and a running jump. Frames at takeoff + 0.17 s, peak, landing + 0.2 s and + 0.6 s.
   Also drop from ~1 m and ~2.5 m to check the fall-height scale.
6. **Crouch:** toggle crouch 3 times. Frames at 0.17 / 0.3 / 0.45 / 0.8 s. In KCD2 also log
   `GetVelocity()` and eye height at 30 Hz during 5 dodges each side, to tune the dodge detector (threshold, window).
7. **Walk bob:** walk forward 5 s and jog 5 s. Compare the gun's vertical travel and roll per step against RoN.
8. **ADS:** repeat 3 and 4 while aiming. RoN keeps ~15-30 % of the motion.
9. **Regression:** the shot kick and recoil (unchanged springs) still settle, and there are no NaN resets in the
   plugin log during jumps or dodges.
