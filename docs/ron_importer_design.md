# Ready or Not gun importer: design (first gun: the G19)

Goal: bring a Ready or Not (RoN) gun into KCD2 whole, as a new **front end** to our existing gun pipeline. That means
its viewmodel mesh and textures, its first-person arms and animations, its sounds and cue times, and its recoil /
ballistics / movement values 1:1. The gameplay side stays as it is (README "How a gun plugs in"). Private use only;
heavy files live in `E:\Tools\deagle-work\ron\<gun>\`, never in the repo.

## 0. Prototype status (done for the G19, 2026-10-12)
- **Exporter:** `E:\Tools\deagle-work\ron\exporter\RonExport` (source: `tools/ron/RonExport/`) is a .NET 10 console app on CUE4Parse 1.2.2 (FModel's
  library), run through `ronexport.bat <list.txt>`. It mounts the Steam paks read-only (unencrypted; Oodle;
  `Mappings.usmap` build 113804) and writes ActorX `.psk` / `.psa`, PNG textures and material JSON.
- **Output in `E:\Tools\deagle-work\ron\g19\ReadyOrNot\Content\...`:**
  - `SK_Pistol_G19.psk`: 8,758 points, 10,000 faces, 23 cm long, 25 bones. Textures `G19_B` (base colour),
    `G19_N` (normal), `G19_ORM` (occlusion / roughness / metal), plus the material instance JSON.
  - `Skinning_Arms_03.psk`: the FP arms the player really uses (BasePlayer `CharacterMesh1P`), 268 bones, textures
    `FP_Arms_01_B / _N / _ORM`.
  - 31 `.psa` clips: draw, holster, speed reload (+ empty, ADS), tac reload (+ ADS), mag check, idle poses (arms +
    weapon), the 60 fps weapon fire / fire-empty, the M2011 arm fire (+ ADS) the G19 borrows, sprint start / fwd / stop,
    dryfire, NVG, door / button / radio, melee bash.
  - 5 older `Revised/...` sequences fail in CUE4Parse ("Unsupported compressed data type"). They aren't the ones the
    G19 uses: `BP_G19G5_M_AnimData` points at the `FP/G19G5/` set, which exported fine. Our own Python decoder
    (`AnimCompress_PerTrackCompression`, used for `ron_movement_curves.json`) is the fallback.
- **Renders:** `Desktop\Deagle Progress\ron_g19_export_sheet.png` shows the gun from 3 sides, textured, and the arms in
  their bind pose.

## 1. Which gun first: the G19
- We already have every number for it:
  - recoil (`ron_recoil.json`: pattern (2, -0.5) / (1.5, 0.5), FirstShotRecoil 0.25, procedural spring 8 / 5 /
    1.4 / 0.1 s);
  - ballistics (9x19 FMJ / JHP rows, curve `Curve_Damage_Pistol_G19`);
  - movement (`ron_movement.json`: inertia 0.6 / 0.4, MeshSpaceAimInterp 55);
  - sprint curves decoded from its own `fp_g19_add_loco_sprint_*`.
- It's the cleanest RoN rig:
  - a separate weapon skeleton with explicitly named parts (`tag_slide`, `tag_mag_01/02`, `tag_brass`, `tag_muzzle`);
  - a full clip set in `BP_G19G5_M_AnimData`;
  - a plain detachable box magazine, which our reload / mag-swap logic already handles (Glock 17 import).
- It sits next to our existing `glock_17` (same handling class, CS2 `weapon_glock`), so we can compare it side by side.
- **Why not the Python .357 first:** a revolver needs things the pipeline doesn't have yet. Swing-out cylinder, a
  per-chamber speedloader reload, no magazine, a hammer, and RoN's `BarrelIndex` / `FireMontages` blueprint
  variables. Do it second, once the RoN front end works.

## 2. How RoN builds a first-person gun (what we're importing)
- **Two skeletal meshes:**
  - the **arms** (`Skinning_Arms_03`, skeleton `SKL_RoN_Player_Master_Shared`: `root … spine_3 → clavicle /
    upperarm / lowerarm / hand + fingers`, `neck_1 → head → fp_camera`, `weapon_left`, `weapon_right`,
    `ik_hand_weapon_*`);
  - the **weapon** (`SK_Pistol_G19`, skeleton `SKL_Pistol_G19`: `J_Gun` root, `tag_slide`, `tag_slidelock`,
    `tag_trigger`, `tag_mag_release`, `tag_mag_01` / `_bullets` / `_static_bullets`, `tag_mag_02…` (the new mag),
    `tag_brass`, `tag_muzzle`, `flash_socket`, attachment sockets `supressor_ / laser_ / comp_ / light_ / Reflex_ /
    SROPistol_ / RMRPistol_socket`).
  - The weapon is attached to the arms' **`weapon_right`** bone (BaseItem `HandsSocket = RightWeaponSocket`).
- **Every action is a pair of additive montages** (`ReadyOrNotWeaponAnimData` → `FAnimDataEntry {Body_FP, Gun_FP,
  Body_TP, Gun_TP}`): one plays on the arms, one on the gun. Both are **additive** (`AAT_LocalSpaceBase`) on top of an
  idle pose (`fp_g19_idle_pose`, arms and weapon versions), mixed in the FP anim graph (`ANIMBP_Player_FP_Master`:
  idle pose, then loco / sprint additives, then the action additive, then procedural recoil / sway).
- **So no clip is a full pose by itself.** The importer must bake: full pose = idle pose ⊕ additive, per frame and per
  bone, in local space (translation added, rotation pre-multiplied). That's how UE applies AAT_LocalSpaceBase.

## 3. Pipeline route (RoN → our build)
Our GameBanana front end ends in `E:\Tools\deagle-work\guns\<slug>\<slug>_vm.blend` (one armature carrying arms +
gun bones, its meshes, one action per source sequence named `@<sequence>`), plus a profile `guns/<slug>.json`.
`tools/gb/gb_convert.sh` takes it from there (`gb_build.py` fixes, extra clips, export; rc; textures; stage; tests).
The RoN front end produces exactly those two things:

1. **Export** (`RonExport`, done): the arms psk, weapon psk, textures, and the arm + weapon psa per action, from the
   gun's AnimData asset. The list is generated from `BP_<gun>_M_AnimData` (the decoders in `tools/ron/`, docs/ron_extraction.md,
   already resolve montages → sequences). Also the TP body sequences for sound notifies (step 5).
2. **`tools/ron/ron_import.py` (new, Blender, like `gb_import`):**
   - import both psk (own ActorX reader: the prototype `psk_render.py` reads points / wedges / faces / skeleton);
   - parent the weapon armature's `J_Gun` under `weapon_right` and merge it into **one armature** (bone names stay);
   - scale cm → m;
   - turn the view so RoN's forward (+Y in component space, our analysis: right = -X, up = +Z, `fp_camera` aligned)
     becomes CryEngine's +Y. Here that's just X mirrored: rotate 180° about Z and flip, or remap axes on import.
     `gb_build.py` rotates Source's +X view by 90°; add a `view_rotation` profile field instead of hard-coding it.
3. **Bake the additives** (new step, in the importer before `@actions` are written). For each action:
   - **arms:** `full = idle_pose_arms ⊕ Body_FP additive`;
   - **gun:** `full = idle_pose_weapon ⊕ Gun_FP additive`;
   - write one merged action `@<role>` at the clip's fps (30, or 60 for the weapon fire clips; `gb_build` already
     resamples > 30 fps).
   - **Fire:** the G19 uses `fp_m2011_add_fire_single` (arms, 0.96 s) + `fp_g19_add_fire_60fps_weapon` (gun, 1.97 s,
     slide cycle). Bake both on one timeline, and trim at the arms' end plus the settle `gb_build` adds.
   - **Check** whether CUE4Parse's psa already bakes additives against the *skeleton ref pose*. If it does, convert
     back to deltas: `delta = refpose⁻¹ · psa_frame`. Then add the idle pose. Our own decoder gives raw deltas
     directly and is the reference.
4. **Profile** `guns/ron_g19.json` (same schema as `glock_17.json`):
   - `clips` (role → `@` sequence, frames, fps, sounds);
   - `bones` (camera: `fp_camera`, gun: `J_Gun`, slide: `tag_slide`, slide_stop: `tag_slidelock`, mag:
     `tag_mag_01`, mag2: `tag_mag_02`, rounds: `tag_mag_01_bullets`, muzzle: `tag_muzzle` (or `flash_socket`),
     brass: `tag_brass`);
   - `magSize` 15 (RoN `Capacity`);
   - `fireInterval` 0.1 (RoN `FireRate`);
   - `cs2`: `weapon_glock` (CS2 handling stays the baseline);
   - `source: {ron: "Secondary_G19_V2"}`.
5. **Sound cues** (new, `tools/ron/ron_cues.py`):
   - RoN fires reload / draw sounds from **`SoundSourceAnimNotify`** entries on the **TP body sequences**
     (`tp_swt_stand_g19_add_<action>`).
   - Each has an FMOD event path and a time. For the speed reload: `G19_Cloth-ReloadStart` 0.028 s,
     `G19_WetMagOutQuick` 0.258, `G19_Cloth-PouchGrab` 0.392, `G19_WetMagInQuick` 0.853, `G19_Cloth-ReloadEnd`
     1.096.
   - Map each event to its FMOD sample(s) in `Weapons.bank` (`tools/impact_sounds.py` already decodes FSB5; the event
     → sample routing isn't decoded, so match by name: `G19_WetMagInQuick` → `*MagIn*`). Write `cues` in seconds.
   - The FP montage length equals the TP one (1.6 s speed reload), so the times carry over. Verify per action.
   - Gameplay notifies give our clip moments exactly (README: "anim notifies give the moments exactly"):
     - `ReloadComplete` on the arm sequence (speed reload 1.55 s, empty 2.283, tac 2.617) → `times.<clip>.ready`;
     - `DropSpeedReloadMagazine` 0.394 and `ShowMag` 0.591 on the weapon montage → old mag out / new mag shown;
     - `MagOutQuick` 0, `MagChange` 0.549 and `MagInQuick` 0.898 on `fp_g19_add_spd_reload_empty_weapon` → `out` /
       `seat`;
     - `MagMaskingAnimNotify` (TP weapon: hide / show `Mag02` / `Dummy` at 0, 0.349, 0.451, 1.033) → when to show the
       mag attachments.
     - These replace `tools/clip_times.py`'s guesses for RoN guns.
   - The gunshot comes from `SoundData_G19`: `1P_G19_Fire` / `3P_G19_Fire` → shot samples, plus `G19_Dryfire_Old`,
     `Pistol_Mag_Drop`, ADS start / stop.
6. **Handling values** (new, `tools/ron/ron_stats.py`, like `cs2_apply.py`): from `ron_recoil.json` /
   `ron_ballistics.json` / `ron_movement.json`, keyed by the RoN weapon BP. Section 6 has the mapping.
7. **Then the existing route:** `tools/gb/gb_convert.sh ron_g19` (build, rc, textures, stage, `tests/run_gun.lua
   ron_g19`), then the transition audit (`gb_tracks_dump.py` + `vm_transition_audit.py`) and the contact sheets.

## 4. Clip roles (G19, `BP_G19G5_M_AnimData` → our roles)
| our role | RoN arms (Body_FP) | RoN gun (Gun_FP) | length |
|---|---|---|---|
| idle | `fp_g19_idle_pose` | `fp_g19_idle_pose` (weapon) | pose |
| idle_empty | idle + slide locked back | gun idle with `tag_slide` held at the fire-empty end frame | pose (generated, as `gb_build` does) |
| draw / draw_empty | `fp_g19_add_draw` | – (gun rides `weapon_right`) | 0.77 s |
| holster / holster_empty | `fp_g19_add_holster` | – | 0.67 s |
| shoot | `fp_m2011_add_fire_single` (+ `_ads` for ADS) | `fp_g19_add_fire_60fps_weapon` | 0.96 / 1.97 s |
| shoot_empty | `fp_m2011_add_fire_single` | `fp_g19_add_fire_empty_60fps_weapon` (slide stays back) | 0.96 / 1.97 s |
| reload | `fp_g19_add_spd_reload` (RoN default R: drop the mag) | `fp_g19_add_spd_reload_weapon` | 1.6 s |
| reload_empty | `fp_g19_add_spd_reload_empty` | `fp_g19_add_spd_reload_empty_weapon` | 2.33 s |
| reload_tac (new, optional: hold R) | `fp_g19_add_tac_reload` | `fp_g19_add_tac_reload_weapon` | 2.67 s |
| inspect / inspect_empty | `fp_g19_add_mag_check` | `fp_g19_add_mag_check_weapon` | 2.67 s |
| dryfire (new, optional) | `fp_fiveseven_add_dryfire` | – | 0.5 s |
| ADS variants | `*_ads` (spd_reload_ads, tac_reload_ads, fire_single_ads) | same gun clip | – |
| sprint start / loop / stop | `fp_g19_add_loco_sprint_*` | – | **not imported**: our procedural motion layer (motion.cpp) already does them from these curves |
| melee bash (later) | `fp_pistol_bash_hit / _miss` | – | 1.0 s |

`gb_build.py` adds settles and ease-ins and fills unkeyed bones. With RoN's additive bake, every clip starts and ends
on the idle pose by construction, so expect few fixes.

## 5. Arms: keep RoN's
Take gun, animations and arms from the same source (README: animations are bound to one skeleton, and the hands to
the gun's grip):
- keep `Skinning_Arms_03` + `FP_Arms_01_*` textures (black tactical sleeves and gloves);
- its 268-bone skeleton goes into the viewmodel `.chr` as is;
- drop bones nothing animates (`pelvis_hlp_*`, legs, `extra_*`) to keep the bone count sane. `vm_missing_bones.py`
  can list what the clips touch.

Retargeting to our KCD2 / medieval arms (`deagle_arms medieval`) is a **second** step: the same per-bone retarget
`tools/retarget.py` uses for the MW arms, with RoN's finger chains (`thumb / index / middle / ring / pinky _1-5`)
mapped one to one. Until then, the RoN gun ships with RoN sleeves, and the `medieval` look applies only to the
existing guns.

## 6. Handling values 1:1 (the gun's table)
Our gameplay is already RoN-shaped. The importer writes a `ron = { ... }` block into the gun file, read where the CS2
values are read today:
- **Recoil** (`ron_recoil.json`, `Secondary_G19_V2`):
  - RecoilPattern (pitch, yaw) per shot cycled, FirstShotRecoil, RecoilInterpSpeed, RecoilReturnRate / Percentage /
    InterpSpeed, ADSRecoilMultiplier → the camera kick;
  - RecoilFireStrength(First), RecoilFireTime, DampStrength, AngleStrength, Randomness, Fire / Angle ADS modifiers,
    Rotation / Position Buildup, BuildupDamp → the gun-model kick springs (plugin `kick`);
  - the C++ defaults that weren't readable stay at our current values.
- **Ballistics:** `AmmunitionTypes` 9x19FMJ / 9x19JHP → the ammo rows of `ron_ballistics.json` (damage, curve,
  penetration level / distance, ricochet, spall, armour, zone multipliers); `ProjectileMovementSpeed` 374.9 m/s;
  `Damage` 30; `FireRate` 0.1 s. `penetration.lua` / `armor.lua` / `hitbones.lua` already use these tables.
- **Movement:** `Deagle.motionGuns.ron_g19` from `ron_movement.json` (InertiaDragAim / Strafe, MovementSpeedScale,
  MeshSpaceAimInterp, LazySpringStrength, FreeAim*), plus `MeshspaceTransform_Default / Aiming` (hip and ADS offsets:
  7 / 0 / -1.25 cm and 15 / -6.85 / 3.65 cm) → `cfg.vm.offset` and the ADS pose.
- **Spread / accuracy:** FirstShotSpread, SpreadPattern, ADSSpreadMultiplier, VelocitySpreadMultiplier,
  SpreadReturnRate → a RoN accuracy model next to `CsAccuracy`, picked by `gun.ron`.

## 7. Risks
1. **Additive bake correctness:** the biggest one. CUE4Parse's psa may already be baked against the ref pose, or may
   hold raw deltas; mixing them up gives twisted wrists. Check one frame against our own decoder before the full
   import, and render the baked idle + reload start as a contact sheet.
2. **Fire is two clips** of different lengths (arms 0.96 s, gun 1.97 s at 60 fps) on different skeletons. The merged
   clip must keep the slide's cycle timing (the gun clip) and the arm recoil (the arm clip). Trim the gun clip to its
   first ~0.2 s of slide motion + settle; RoN's procedural recoil (which we have) does the rest.
3. **Axis conversion:** UE is left-handed, Z up, component forward +Y (right = -X); CryEngine is right-handed. The
   mesh needs a mirror-free remap: rotate, don't scale -1, or the winding and normals flip. Check the trigger finger
   lands on the trigger, and do the roll sign check (design doc 5.1) on the merged rig.
4. **CUE4Parse gaps:** 5 `Revised/` sequences didn't decode (unsupported codec). Use our decoder for those, or skip:
   none are G19 clips.
5. **Sound routing:** FMOD event → sample isn't decoded (compiled event data), so matching is by name. Some events are
   multi-sample (random picks), so take all variants. Reload timelines inside one event (if any) would need the event
   timeline; the G19's are single-shot cloth / mag events.
6. **Bone count / rc:** 268 arm bones + 25 gun bones. KCD2 viewmodels with 100+ bones are fine (MW arms), but prune
   unused bones before rc to keep compile time and memory sane.
7. **Scale:** RoN is in cm. Check the in-game size against the Glock 17 (both ~0.19-0.23 m long).
8. **Licensing:** RoN assets are private use only. Never commit psk / psa / png or derived DDS / CHR (`assets/build`,
   `src/Objects/**` binaries and `src/Textures/**` are gitignored already).

## 8. Next steps (in order)
1. `tools/ron/ron_import.py`: psk → one merged armature + meshes + materials (Blender). Render against the
   `glock_17` rest pose (contact sheet).
2. The additive bake for `idle`, `shoot` and `reload` first. Check them against the decoder (risk 1).
3. Profile `guns/ron_g19.json` + `ron_cues.py` + `ron_stats.py`.
4. `gb_convert.sh ron_g19`, the transition audit, then in game: `deagle_gun ron_g19`.
