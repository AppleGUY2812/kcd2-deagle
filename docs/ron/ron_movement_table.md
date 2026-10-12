# Ready or Not first-person weapon movement read-out

Decoded from the Steam install (read-only). Raw: `ron_movement.json`. `-` = not overridden → C++ default (**not read**). Units: cm, degrees, seconds.

## Headline: what is procedural vs baked

- **Baked additive animations** drive sprint (start / loop / stop), jump (start / loop / end), stand↔crouch and walk/jog loco bob. The FP anim graph `ANIMBP_Player_FP_Master` plays them: `fp_g19_add_loco_sprint_fwd`, `fp_g19_add_loco_sprint_start`, `fp_g19_add_loco_sprint_stop`, `fp_g19_add_tac_up_sprint_fwd`, `fp_g19_add_tac_up_sprint_start`, `fp_g19_add_tac_up_sprint_stop`, `fp_g19_add_tac_up_sprint_stop_heavy`, `fp_gen_add_jump_end`, `fp_gen_add_jump_loop`, `fp_gen_add_jump_start`, `fp_m4a1_add_loco_f_bwd`, `fp_m4a1_add_loco_f_fwd`, `fp_m4a1_add_loco_f_l`, `fp_m4a1_add_loco_f_r`, `fp_m4a1_add_loco_jog_fwd_stop`, `fp_m4a1_add_loco_m_bwd`, `fp_m4a1_add_loco_m_fwd`, `fp_m4a1_add_loco_m_l`, `fp_m4a1_add_loco_m_r`, `fp_m4a1_add_loco_sprint_fwd_heavy`, `fp_m4a1_add_loco_sprint_fwd_start_3_heavy`, `fp_m4a1_add_loco_sprint_stop_heavy`, `fp_m4a1_add_sprint_fwd`, `fp_m4a1_add_sprint_start`, `fp_m4a1_add_sprint_stop`, `fp_m4a1_add_tac_up_sprint_fwd`, `fp_m4a1_add_tac_up_sprint_fwd_no_add`, `fp_m4a1_add_tac_up_sprint_start`, `fp_m4a1_add_tac_up_sprint_stop`, `fp_m4a1_add_tac_up_sprint_stop_heavy`, `fp_m4a1_crouch_to_stand`, `fp_m4a1_stand_to_crouch`.
- **Procedural, numeric**: per-gun inertia drag (look/strafe sway), mesh-space movement scale, free-aim (deadzone) interp, lean multipliers, mesh-space poses (default / ADS / back / draw / holster) with interp speeds, wall pushback / low-ready ranges, spread. There is also one **LazySpringBone** node on `weapon_right`.
- **Bob parameters** (`CameraBob*` / `WeaponBob*`, 21 fields on BaseItem) are **never overridden** by any of the 68 weapons, so they are C++ defaults (names only, listed at the end).

## 1. Sway / bob / inertia

- LazySpringBone (`weapon_right`, ref `root`): DrivingForceStrength 3.5, TargetDistance 0/15/0, Stiffness 0.35, Mass 0.9, Damping 0.925, Gravity 0.75, SpringStiffness 100, SpringDamping 8, all 6 axes enabled. Weapons scale it with `LazySpringStrength` / `LazySpringStrengthADS` (G19 ADS = -1, i.e. use default).
- Player: CameraRotationRateMeshSpaceMultiplier -2.5/-2.5/0 (look-rate → mesh offset), MeshspaceInterp 1, MeshspaceRecoilMovementMultiplier 2/2/2, MinMax 2/2/2, idle camera shake DefaultIdleShake 0.05.
- Per gun: see the table in section 7 (InertiaDrag*, MovementSpeedScale = mesh offset per unit velocity xyz, MovementSpeedRotationScalePitchYawRoll, MeshSpaceAimInterp, FreeAim*).

## 2. Sprint

- SpeedModifier_Sprint 1.05, SpeedModifier_SprintTime 0.1 s, RunSpeed 320 cm/s, SprintFovFactor 1.15 (×FOV), SprintFovInterpTime 0.9 s, SprintShakeAmount 0.1, SprintShakeDecreaseRate 0.1.
- Sprint pose = baked `fp_<gun>_add_loco_sprint_start/fwd/stop` (pistols use the G19 set, rifles the M4A1 set, heavy armour gets `_heavy` variants). No numeric sprint offset, blend time, or **sprint-to-fire delay** field exists in the mappings. Firing after a sprint is most likely gated by the sprint-stop anim (inference).

## 3. Jump / land

- CharacterMovement: JumpZVelocity 350 cm/s, GravityScale 3, MaxAcceleration 512, BrakingDecelerationWalking 256, BrakingFriction 3, MaxStepHeight 22.5.
- Weapon kick = baked `fp_gen_add_jump_start/loop/end` additives. `JumpRecoveryStrength/Time/AnimTime`, `bHasPrelanded` exist only as runtime anim variables (names only). There is no fall-height scaling field; only the landing sound threshold `NegativeVelocityRequiredForLandingSound` (not overridden).

## 4. Crouch / lean / stance

- CrouchHeight 20, CrouchedEyeHeight 40, CrouchedHalfHeight 96, MaxWalkSpeedCrouched 100 cm/s. Transition = baked `fp_m4a1_stand_to_crouch` / `fp_m4a1_crouch_to_stand`.
- Lean: LeanSpeedMultiplier 0.5 (move speed while leaning). Per gun QuickLeanMultiplier / FreeLeanMultiplier (G19 200/200, most guns default). QuickLeanAmount / Intensity / InterpSpeed are names only.
- Speeds: SpeedModifier_Aim 0.85, SpeedModifier_AimFocus 0.5, BackwardStrafeSpeedMultiplier 0.75, MinWalkSpeedPercent 0.25, HitSpeedMultiplier 0.8.
- Freelook limits: P:50 Y:-65 Y:65. Ready or Not has no prone stance.

## 5. Movement inaccuracy

- Per gun: FirstShotSpread, SpreadPattern, ADSSpreadMultiplier, VelocitySpreadMultiplier (spread added per unit of movement speed), SpreadReturnRate, BulletsFiredUntilFullyAccurate (table below). There are **no crouch / jump / in-air spread fields** in the mappings, and attachments scale spread with `SpreadMultiplier` (GM9 suppressor 0.9).

## 6. Wall pushback / low ready

- Per gun: PushbackRangeOffset, LowReadyRangeOffset (cm added to the trace for a long gun), LowReadyPitchThreshold. Attachments add length, e.g. Pistol_Suppressor_GM9: LowReadyOffset 33, PushbackOffset 33 (also VerticalRecoilMultiplier 1.2, SpreadMultiplier 0.9, DeadzoneMultiplier 1.8, MuzzleVelocityMultiplier 1.3). Base trace distances (LowReadyTraceDistance, LowReadyDistance) are names only.

## 7. Per-gun values

| weapon | name | InertiaAim R/L | InertiaStrafe R/L | MoveSpeedScale | MoveRotScale PYR | MeshSpaceAimInterp | LazySpring/ADS | FreeAim speed/ADS/hip | FreeAimLimit/ADS | Default pos | ADS pos | QuickLean/FreeLean | Pushback/LowReady | FirstShotSpread | ADSSpread | VelSpread | SpreadReturn | ADSZoomIn/Out |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Secondary_G19_V2 | G19 | 0.6/0.4 | 0.4/0.2 | 10/10/5 | 60/0/0 | 55 | -/-1 | 20/0.5/1 | 0/0 | 7/0/-1.25 | 15/-6.85/3.65 | 200/200 | 15/15 | 0 | 0.1 | 0 | 0.5 | 5/- |
| Secondary_M92FS_V2 | M9A1 | 0.6/0.875 | -/0.05 | 10/10/7 | 60/0/0 | 15 | -/- | 20/1/1 | 1/1.5 | 9/0/-1.5 | 15/-6.2/2.2 | -/- | -/- | 0 | 0 | 0 | 0 | 5/- |
| Secondary_Python_V2 | .357 Magnum | 0.6/0.875 | -/0.05 | 10/10/5 | 35/1/-2 | 55 | -/- | 20/0.5/1 | 1/1.25 | 5/-1/-1 | 12/-6.85/1.8 | -/- | 15/15 | 0 | 0 | 0 | 1 | 3/3 |
| Secondary_M1911_V2 | M1911 | 0.6/0.875 | -/0.05 | - | - | 15 | -/- | 10/0.9/1 | 3/1.5 | 7/-3/-2 | 8/-6.9/3.15 | -/- | -/- | 0.2 | 0 | - | 6 | 5/6 |
| Secondary_USP | USP45 | 0.6/0.4 | 0.4/0.2 | 5/10/15 | 60/0/0 | 55 | 1.8/1.5 | 20/1/1 | 0/0 | 12/0/0 | 15/-5.4/2 | 200/200 | 15/15 | 0 | 0 | 0 | 0 | -/- |
| Secondary_P229 | M11 Compact | 0.6/0.4 | 0.4/0.2 | 10/10/5 | 60/0/0 | 55 | -/-1 | 20/0.5/1 | 0/0 | 5/-1/-1.25 | 15/-6.85/3.65 | 200/200 | 15/15 | 0 | 0.1 | 0 | 0.5 | 5/- |
| Secondary_PFC9 | PC19 | 0.6/0.4 | 0.4/0.2 | 10/10/5 | 60/0/0 | 15 | -/-1 | 20/0.5/1 | 0/0 | 7/0/-1.25 | 15/-6.85/3.65 | 200/200 | -/- | 0 | 0.1 | 0 | 0.5 | 5/- |
| Secondary_P250 | P250 | -/- | 1/1 | 5/10/15 | 60/0/0 | 15 | 1.8/1.5 | 20/1/1 | 1/0.5 | 10/-0.5/-1 | 15/-5.35/2 | 200/200 | -/- | 0 | 0 | 0 | 0 | -/- |
| Secondary_M92A3 | B92X | 0.6/0.4 | 0.4/0.2 | 10/10/7 | 80/0/0 | 55 | -/- | 20/1/1 | 1/1.5 | 9/0/-1.5 | 15/-6.4/2.2 | -/- | 15/15 | 0 | 0 | 0 | 0 | 5/- |
| Secondary_FiveSeven_V2 | FiveSeven | 0.6/0.4 | 0.4/0.2 | 10/10/5 | 60/1/0 | 55 | 0.8/0.4 | 20/0.5/1 | 0/0 | 9/0/0 | 15/-6.6/4 | -/- | 15/15 | 0 | 0 | 0 | 0 | 11/11 |
| Secondary_P99 | G19 | 0.6/0.875 | -/0.05 | 10/10/8 | 60/0/0 | 15 | 0.8/0.4 | 20/0.5/1 | 5/2 | 9/0/0 | 15/-6.85/3.65 | 200/200 | -/- | 0.05 | 0 | 0 | 0 | 5/- |
| Secondary_Kimber1911 | TLE 1911 | 0.6/0.4 | 0.4/0.2 | 10/10/6 | 60/0/0 | 55 | 0.8/0.4 | -/-/- | 0/0 | 5/-0.5/-0.35 | 10/-6.85/3.9 | -/- | 17/17 | 0 | 0 | 0.02 | 0 | 5/- |
| Secondary_M45A1 | M45A1 | 0.6/0.4 | 0.4/0.2 | 10/10/6 | 60/0/0 | 55 | 0.8/0.4 | -/-/- | 0/0 | 5/-0.5/-0.35 | 10/-6.85/4.1 | -/- | 17/17 | 0 | 0 | 0.02 | 0 | 5/- |
| Secondary_M2011 | M2011 | 0.6/0.4 | 0.4/0.2 | 10/10/6 | 60/0/0 | - | 0.8/0.4 | -/-/- | 0/0 | 8/-0.5/-0.35 | 15/-6.85/3.25 | -/- | -/- | 0 | 0 | 0.02 | 0 | 5/- |
| Secondary_TCR | TPL | 0.8/0.15 | 0.4/0.2 | 10/10/5 | 75/0/0 | 55 | -/- | 3/0.5/1 | 1/1 | 7/0/-0.6 | 0/-6.8/2.15 | -/- | 6/6 | 0 | 0 | 0 | 0 | 3/3 |
| Primary_MP5A3 | MP5A3 | 0.8/0.15 | 0.4/0.2 | 10/5/5 | 75/0/0 | 55 | 0.85/-0.25 | 20/0.5/1 | 1.5/1 | 9/0/-0.25 | 8/-6.83/2.1 | -/- | -5/3 | 0 | 0 | 0 | 0 | 11/11 |
| Primary_M4A1 | M4A1 | 1.6/0.3 | 0.8/0.4 | 10/10/5 | 75/0/0 | 11 | -/- | 3/0.5/1 | 5/3 | 6/-1/0 | 5/-6.7/2.35 | -/- | -/- | 1 | 0 | 0.02 | 0 | 5/8 |
| Primary_HK416 | GA416 | 0.8/0.5 | 0.4/0.2 | 10/10/5 | 75/0/0 | 11 | -/- | 3/0.5/1 | 1/1 | 8/0/-0.5 | -1/-6.8/0.7 | -/- | 7/7 | 0 | 0 | 0 | 0 | 3/3 |
| Primary_870mcs | 870mcs | 0.8/0.15 | 0.4/0.125 | 5/0/3 | 15/0/0 | - | -/0.46 | -/0.5/1 | 0/0 | 8/-1.25/1.5 | 5/-6.64/2.75 | -/- | -/- | 1 | 0.7 | 0 | 0 | -/- |
| Primary_B1301 | B1301 | 0.8/0.15 | 0.4/0.125 | 10/10/5 | 80/0/0 | 55 | -/- | 20/0.5/1 | 0/0 | 12/-0.75/1 | 4/-6.64/3.2 | -/- | -/- | 1 | 0.7 | 0 | 0 | -/- |

All 68 weapons are in `ron_movement.json → weapons`. Each gun links its montages through `AnimationData` (e.g. G19 → `AnimData/Mainline/BP_G19G5_M_AnimData`). Those are montage references only, with no numeric movement values. Correction to the earlier recoil notes: the G19 **does** play an arm fire montage (`fp_m2011_add_fire_single_Montage` / `_ads`), borrowed from the M2011.

## 8. Damage-curve X units

cm. A G19 curve read in cm falls off from 1.5 m to 50 m. The 870 buck curve runs 25 m to 151 m. Read in metres, the pistol would fall off out to 5 km. There is no UI range text to cross-check (weapons only have velocity/RPM text).

## Names only (no value in data)

- **BaseItem (weapon bob, never overridden on any of 68 weapons -> C++ defaults)**: CameraBobScaleH, CameraBobScaleV, CameraBobSpeedScaleH, CameraBobSpeedScaleV, CameraBobAmplitudeBaseSpeed, CameraBobIntensitySprintScale, CameraBobAmplitudeWalkScale, CameraBobAmplitudeSprintScale, WeaponBobScaleH, WeaponBobScaleV, WeaponBobScaleInjured, WeaponBobSpeedScaleH, WeaponBobSpeedScaleV, WeaponBobSpeedScaleInjured, WeaponBobCrouchModifier, WeaponBobADSModifier, WeaponBobRotPitchScale, WeaponBobRotRollScale, WeaponBobRotPitchSpeed, WeaponBobRotRollSpeed, WeaponBobRotCrouchModifier, WeaponBobRotADSModifier, CameraBobAdditionFactor (WeaponAttachment)
- **PlayerCharacter**: CameraBobTrans, WeaponBobTrans, WeaponBobRot, CameraBobRot, bIsPelvisFPMovementBobActive, MeshSpaceMovementMultiplier1P, LowReadyTraceDistance, SpeedModifier_Crouch, SpeedModifier_SprintMax, SprintSpeedRampUpMultiplier, MaxCrouchRunSpeedPercent, NegativeVelocityRequiredForLandingSound
- **ReadyOrNotCharacter**: QuickLeanAmount, QuickLeanIntensity, QuickLeanInterpSpeed, FreeLeanX, FreeLeanZ, LowReadyDistance
- **ReadyOrNotAnimInstance**: bHasPrelanded, bJumpRecoveryActive, JumpRecoveryAnimTime, JumpRecoveryStrength, JumpRecoveryTime, SprintAlpha, SprintFPAlpha, PelvisMovementBobAlpha, MovementJogThreshold
- **RoNAnimInstance_PlayerFP**: LeanAngleY, LeanAngleZ, MeshPostureLeanOffset, MeshWeaponLeanOffset, MeshWeaponLeanRotation, LazySpringStrength, ADS_Movement_Weight
- **sprint_to_fire_delay**: no property found in mappings (only SpeedModifier_SprintTime, SprintFovInterpTime); likely gated by the sprint-stop additive animation
