# Ready or Not Grizzly MK V: every first-person interaction, clip and control (2026-10-19)

Read from the RoN paks (tools/ron ronx.py / dump.py) and the exe (Ghidra RON project, `FUN_144b436f0` wall / low
ready, `FUN_144b677a0` input setup) by a helper agent; no web. Scratch dumps (not in the repo): `griz_animdata.json`,
`griz_montages.json`, `grizzly_cdo.json`, `baseplayer_cdo.json`, `dec_144b436f0.c`, `dec_bindings.c` in session
c0dd4493's scratchpad. Goal (user, 2026-10-19): "every interaction full port", controls like RoN, ADS.

## 1. Clips
The Grizzly has 17 arm + 13 weapon clips of its own (`DLC3 .../Grizzly/Animations/FP/`); **all exported**
(`E:\Tools\deagle-work\ron\grizzly\arms_clips.json`, `weapon_clips.json`). Everything else is shared:

| Group | Grizzly's own (exported) | Shared, still to export (`/Game/ReadyOrNot/Animations/...`) |
|---|---|---|
| Draw / holster | add_draw_b 0.83 s, **add_draw_first_b 1.5 s**, add_holster_b 0.83 s (HolsterComplete 0.57 s) | – |
| Fire | fire_single_b, fire_single_ads_b, fire_last_b, fire_last_ads_b (0.667 s, 60 fps) | `Revised/FirstPerson/Weapons/Fiveseven/arms/fp_fiveseven_add_dryfire` (0.5 s) |
| Reload | tac_reload_b / _ads_b 2.5 s (ReloadComplete 2.25), spd_reload_b 1.67 s (1.36), spd_reload_empty_b 1.9 s, spd_reload_empty_b_ads 2.0 s | `FP/G19/arms/fp_g19_add_spd_reload_ads` (1.6 s) |
| Idle / stances | idle_pose_b (base), idle_pose, **idle_high_ready** (wall / low ready up) | `Fiveseven/arms/fp_fiveseven_add_idle` (breathing), `Fiveseven/arms/game_feel/fp_fiveseven_add_lower_pose` (low ready down), `BS_Pistol_Fiveseven_Look_FP` + `fp_fiveseven_add_look_up/down/left/right` |
| Walk / run | – | `M4_Rifle/arms/game_feel/fp_m4a1_add_walk_loop_01`, `_run_loop_01`, `_walk_limp_loop_01`, `_run_limp_loop_01`; ADS `FP/M4A1/arms/fp_m4a1_add_walk_loop`, `_walk_limp_loop` |
| Sprint | – | `FP/SHARED/pistol_sprint/fp_g19_add_loco_sprint_start/fwd/stop`, `fp_g19_add_tac_up_sprint_start/fwd/stop` |
| Misc locomotion | – | `FP/SHARED/fp_m4a1_add_loco_{f,m}_{fwd,bwd,l,r}`, `fp_m4a1_add_ads_enable/_disable`, `fp_m4a1_stand_to_crouch`, `crouch_to_stand`, `fp_gen_add_jump_start/loop/end` |
| Melee | – | `FP/Melee/fp_pistol_bash_hit` (damage at 0.37 s), `fp_pistol_bash_miss` (1.0 s) |
| Gestures | add_feel_Button_Push | `FP/Gamefeel/base_male/fp_feel_Door_Push_pistol`, `fp_feel_Radio_pistol`, `fp_m4a1_add_yell`, `fp_m4a1_add_pickup`, NVG on / off |
| Shield | shield_reload_empty_weapon | `FP/Shield/Fiveseven/base_male/...` (not needed in KCD2) |

RoN has **no** inspect, chamber check, jam or fire-select clip for pistols; lean and ADS are procedural.

## 2. Which clip plays when (`BP_Grizzly_M_AnimData_B`)
- Draw = draw_b; **DrawFirst = draw_first_b** (first draw); Holster = holster_b.
- FireSingle / FireSingleSights = fire_single_b / _ads_b; the last round = fire_last_b / _ads_b; DryFire = fiveseven dryfire.
- **R tap = Tactical_Reload = tac_reload_b** (2.5 s, keeps the magazine). **R double-tap = Reload = spd_reload_b**
  (1.67 s, drops it). Empty = spd_reload_empty_b either way. ADS versions: tac_reload_ads_b, spd_reload_empty_b_ads,
  G19 spd_reload_ads.
- **R hold > 0.2 s = MagazineCheck = mag_check_b** (3.0 s).
- Melee = pistol_bash_hit / _miss.
- Lowered_Up_Pose = idle_high_ready (up); Lowered_Down_Pose = fiveseven lower_pose (down).
- Gun fields: LowReadyPitchThreshold -25, LowReadyRangeOffset 18, PushbackRangeOffset 18, AvailableFireModes FM_Single.

## 3. Wall block / low ready (exe `FUN_144b436f0`, every tick)
1. Sweep from the FP camera + 5 cm, LowReadyTraceDistance (10000 cm), sphere 9 / 14 raised, 11 lowered (hysteresis).
2. Blocked when the hit distance <= weapon length = max(root->tag_muzzle + 5 cm, farthest attachment socket + 10 cm)
   + LowReadyRangeOffset (18) + attachment LowReadyOffset.
3. Tag `DoNotRaiseGun` never blocks, `ForceLowReady` always; a closed door counts at distance x 1.75 (medium confidence).
4. Blocked and camera pitch > -25 deg: point **up** (idle_high_ready); <= -25 deg: point **down** (lower_pose).
5. User low ready (hold / toggle key) always lowers; the user setting bUseHighReadyStyle picks up or down.
6. Pushback: closer than weapon length + 30 cm adds MeshspaceTransform_Back (-40, 3, -2) x 4 (needed - d) / needed
   (already in docs/ron/ron_recoil_code.md 6.2).
7. Not readable: the up / down blend speeds (ANIMBP_Player_FP_Master bytecode; its additives use 0.2 s alpha blends).

## 4. RoN default controls (DefaultInput.ini + the exe's input setup)
| Action | Key | Behaviour |
|---|---|---|
| Fire | LMB | one shot a press |
| ADS | RMB | hold |
| Reload / mag check | R | tap: tactical reload after 0.25 s; double-tap (0.2 s): fast reload, mag dropped; hold > 0.2 s: mag check |
| Melee | B | pistol bash |
| Fire select | X | press cycles, hold = safe (the Grizzly has semi only) |
| Light / laser | Mouse 5 | press toggles, hold cycles |
| Low ready | hold Delete (the user's RoN: hold LeftShift); Space toggles | pressed lowers, released raises |
| Walk | LeftShift | hold = slower |
| Crouch | LeftCtrl | toggle (user setting) |
| Lean | Q / E hold; free lean LeftAlt + WASD | procedural |
| Draw | 2 (secondary) / 1 (primary) | – |
| Interact | F | door push / button push gestures |

## 5. KCD2's defaults on those keys (keybindSuperactions.xml)
LeftShift sprint, Space jump, LeftCtrl horse slowdown, C crouch, LeftAlt chat, Q chat_init, E primary action, B quick
access menu, X companion, F special attack, G distract, R torch (our mod filters it), Mouse 4 / Mouse 5 / H / Y free.

## 6. ADS camera FOV (exe `FUN_144b3bc30`, the PlayerCharacter FOV update, every Tick)
- Iron sights (no scope): target = the player's FOV setting x **0.75** (hardcoded `DAT_1465117f8`), linear in degrees,
  only with the user setting bZoomADS (default True). A scope uses its PlayerCameraFOVMultiplier instead.
- `cam.FOV = FInterpTo(cur, target, dt, speed)` = cur + clamp(dt x speed, 0, 1) x (target - cur). Zoom in: the gun's
  ADSZoomInSpeed (Grizzly 5, ~0.2 s time constant). Zoom out: DefaultFovInterpTime **10**; ADSZoomOutSpeed (6) is not
  read on this path.
- The setting's axis: bUseVerticalFOV True = vertical FOV; False = Hor+ (read as a 16:9 horizontal FOV, then converted
  to the real aspect).
- The user's RoN (GameUserSettings.ini, 2026-10-12): FieldOfView 90, bUseVerticalFOV True, bZoomADS True, 1600x900:
  hip **90 deg vertical**, ADS **67.5 deg vertical** (not "FOV 110" as noted earlier).
- Not camera FOV: ADSZoom (native 0.9; its reader unknown), minhfov 48 / tgtvfov 27 (the tablet-focus override).

## 7. Lean (static: decompile + a Kismet disassembly of ANIMBP_Player_FP_Master, 2026-10-19)
- **The first-person camera doesn't roll.** It slides and dips; the tilt you see is the arms and gun canting.
- Q/E: QuickLeanAmount target +-15 cm (left -15), approached at a constant 400 cm/s; back to 0 with FInterpTo at 30.
- Camera offset target (`FUN_144b3da80`): X = -(FreeLeanX + (free leaning ? 0 : Quick)), Z = FreeLeanZ - 0.5 |Quick|
  (Z >= 0: Z x (1 - 0.9 clamp(0.025 |FreeLeanX|, 0, 1))); VInterpTo speed 8; an additive translation on
  `torso_stabilizer` (parent of `fp_camera`). Clamps X -50..80 standing, -53..65 crouched; Z -90..15; crouched adds
  -0.55 |FreeLeanX| to Z. Full Q/E: 15 cm sideways, 7.5 cm down.
- Arms and gun: roll = clamp(lean, -25, 25) deg, RInterpTo 8 (spine_3 pivots round fp_camera, fp_camera gets it back):
  15 deg on Q/E, up to 25 deg in free lean.
- LeanSpeedMultiplier (0.5) scales walking while Q/E is held; it isn't a lean speed.
- Walls (`FUN_144e6df70`): capsule trace, radius 5 cm, half-height 40, from spine_3 along the body's right, length
  |value| + margin; a hit gives sign x clamp(distance - margin, 0, margin). Margins: Q/E 15 (full lean needs ~30 cm),
  FreeLeanX 40, FreeLeanZ 25.
- ADS halves the free-lean rate (150 -> 75 cm/s); nothing found that changes spread while leaning (not exhaustive).
- Free lean (LeftAlt + WASD): sideways 150 cm/s to +-40, vertical 100 cm/s from -40 to +30, both traced; back to 0 at
  FInterpTo 10.
- Unresolved: what blocks a lean (a character and an item virtual), the trace channel (probably WorldStatic).
