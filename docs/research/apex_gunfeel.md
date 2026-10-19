# Apex Legends (r5apex_dx12.exe) gun-feel study: static, read-only

Scope and gaps: the **weapon-settings schema is verified**. The **runtime formulas are NOT recovered**: no consumer function is identified, so every formula below is inferred. The actual weapon numbers (patterns, per-gun values) live in the VPK weapon settings and Squirrel scripts, not in the exe, so only parser defaults are available.

## 1. Weapon-settings parser (verified)
`FUN_1418f98f0` (about 128 KB of code, too big for the decompiler) registers about 1322 fields. I recovered them by script: `work/apex/fields.tsv` (columns: name, type, id, struct offset, default). Each record has this layout:
- name string
- type byte (5 = float)
- field id (e.g. `0x22c`)
- byte offset inside the weapon-settings object
- a name hash from `FUN_141f344b0`
- a float default (set via `FUN_141f52460`)

Example: `cooldown_viewkick_adsScale` has id 0x22d, offset 0x960, default 1.0.

## 2. Recoil: parameters (verified names, offsets and defaults; semantics inferred)
Offsets are in the weapon-settings object.

- **Pattern kick**
  - `viewkick_pattern` (0x938) selects a per-shot pattern table.
  - `viewkick_pattern_valuePerShot` (0x93c, default 1.0).
- **Base kick per shot**
  - `viewkick_pitch_base/random/random_innerexclude` (0x984–0x98c).
  - `viewkick_yaw_*` (0xa0c–0xa14).
  - `viewkick_roll_base/randomMin/randomMax` (0x9b4, 0x9a4, 0x9a8).
  - The `*_innerexclude` fields look like a dead zone: the random value is re-rolled outside ±exclude, so the sign is never near zero (inferred).
- **Soft vs hard split**
  - `viewkick_{pitch,yaw,roll}_softScale/hardScale` (0x990/0x994, 0xa18/0xa1c, 0x9ac/0x9b0).
  - Inferred: "hard" is a permanent camera offset that must be pulled down by the player; "soft" recovers on its own.
  - `viewkick_perm_{pitch,yaw}_*` (0x998–0x9a0, 0xa20–0xa28) are the permanent part.
- **Weapon-fraction fields**
  - `viewkick_{hipfire,ads}_weaponFraction` (0x964, 0x96c) and `_vmScale` (0x968, 0x970).
  - Inferred: the share of kick that goes to the viewmodel instead of the camera.
- **Shot-count scaling**
  - `viewkick_scale_firstshot_{hipfire,ads}` (0x9b8, 0x9bc).
  - `viewkick_scale_{min,max}_{hipfire,ads}` (0x9c0–0x9cc, default 1.0).
  - `viewkick_scale_valuePerShot` (0x9d0), `..._pitch/yaw_valueLerpStart/End` (0x9d4–0x9e0).
  - `viewkick_scale_valueDecayDelay/Rate` (0x9e4, 0x9e8).
  - Inferred: an accumulator `v` rises by `valuePerShot` per shot and decays after a delay at `DecayRate`. The kick scale is `lerp(min, max, v)`, so recoil ramps up while spraying and resets when you stop.
- **Stance and ADS multipliers**
  - `viewkick_duck_scale`, `_hover_scale`, `_move_scale`, `_air_scale_ads` (0x974–0x980, default 1.0).
  - `cooldown_viewkick_*` (0x940–0x960) is a separate kick set applied when the weapon is in cooldown, e.g. a charge or heat weapon. It has its own `adsScale`.
  - `viewkick_pc_pitch_scale` and `viewkick_pc_yaw_scale` (0x9f0, 0x9ec, default 1.0) are PC-only scales.
  - `alt_fire_alternates_viewkick_yaw` flips the yaw sign per alt-fire shot.
- **Spring**
  - `viewkick_spring` (0x9f4) and `viewkick_spring_hot` (0x9f8) name a spring preset. They are presumably looked up in a separate table that I did not find.
  - `viewkick_spring_heatpershot` (default 1.0), `_cooldown_holdtime` (0), `_cooldown_fadetime` (1.0), `_velocityScalePerShot` (1.0) at 0x9fc–0xa08.
  - Inferred: each shot kicks the spring's velocity, and "heat" accumulates so sustained fire gets a stiffer or different spring.
- **Drift**
  - `viewdrift_{hipfire,ads}_{stand,crouch,air}_scale_{pitch,yaw}`, `_speed_*` (default 1.0), `viewdrift_ads_delay` (0xa2c–0xa6c).
  - Inferred: slow aim wander after a delay, scaled by stance.
- **Player-side**
  - `damage_viewpunch_scale` (0x1104) scales the camera punch you take from damage.
  - Fall landing: `viewkickFallDistMin/Max/MaxScale` (strings at 0x143111900–0x143111938). I found no code references to these; they are likely read through a data table.
  - Wallrun: `wallrun_viewpunch_onStart/onTimeout_{base,random}{Pitch,Yaw,Roll}` (strings at 0x143112558 and following). Same caveat: no code references found.

## 3. Spread / bloom (verified names and defaults; formula inferred)
All offsets are in 0x78c–0x815.
- **Resting spread per state**
  - `spread_stand_hip` (default 5.0), `_stand_hip_run`, `_stand_hip_sprint`, `_stand_ads`, `_crouch_{hip,ads}`, `_air_{hip,ads}` (+`_moving`), `_hover_*`, `_wallrunning`, `_wallhanging`.
  - Defaults of -1 mean "fall back to the previous state".
- **Moving penalty**
  - `spread_moving_increase_rate` and `spread_moving_decay_rate`, both default 75.0, per second.
  - So spread ramps toward the moving value at 75 units/s instead of snapping.
- **Per-shot kick and cap**
  - `spread_kick_on_fire_{stand,crouch,air}_{hip,ads}` (0x7d4–0x7e8) is added per shot.
  - `spread_max_kick_*` (0x7f0–0x804) caps it; `spread_min_kick` (0x7ec) is the floor.
- **Recovery**
  - `spread_decay_rate` (default 5.0), `spread_decay_delay` (default 0.1 s), `spread_time_to_max`.
  - `spread_update_hipfire_in_ads` keeps the hipfire spread simulated while in ADS.
- **Random application**
  - `applyRandSpread` and `dontApplySpread` are script and weapon flags. `primary_attack_ignores_spread` bypasses it.
  - `FUN_141f17bc0` turned out to be an unrelated static-init function.
  - `blast_pattern*` is the shotgun pellet pattern, with `blast_pattern_zero_distance` default 512.

## 4. Viewmodel and ADS (verified names and defaults; formulas inferred)
- **Offsets**
  - `viewmodel_offset_hip` (0x864) and `viewmodel_offset_ads` (0x870) are vectors.
  - Interpolated by `viewmodel_offset_zoom_frac_start` (0) to `_end` (0.5), so the viewmodel reaches its ADS position at 50% of the zoom fraction, well before the FOV finishes.
  - `viewmodel_offset_offhand_scale` is 0.35.
  - `viewmodel_offset_ads_by_centerpoint` aligns the sight's `viewmodel_ads_centerpoint_attachment` to the screen centre.
- **Zoom**
  - `zoom_fov` (0x1094, default 40), `zoom_time_in/out` (0x10a0, 0x109c; default 0.1 s).
  - `ads_fov_zoomfrac_start/end` (0 to 1), `zoom_fov_viewmodel`, `zoom_scope_frac_*`.
  - `ads_move_speed_scale` is 0.5. `player_ADS_buffer_time_seconds` is a convar whose default I did not read.
  - `ads_blend` and `ads_anim_blend_enabled` drive the animation blend. `player_zoomFrac` is exposed to script.
- **Sway and jolt**
  - `sway_rotate_scale_unzoomed` and `sway_rotate_scale_zoomed` (0x420, 0x424; default 1.0) scale turn-inertia rotation of the viewmodel.
  - Titan cockpit sway: `cockpitSway{Gain, Turn/Move OriginFactor, AngleFactor, Min/Max Angle/Origin}`.
  - Per-shot recoil jolt on the model: `viewmodel_spring_jolt` (0x8a0, spring preset), `viewmodel_jolt_scale`, `viewmodel_jolt_backwardPerShot`, `viewmodel_jolt_side`, `viewmodel_jolt_roll` (0x8a4–0x8b8).
- **Not found**
  - Walk and sprint bob, jump and land bounce come from animation (sprint and raise-from-sprint timings only: `raise_from_sprint_time`, `sprintcycle_time`, `weapon_sprint_raise_delay`). I found no procedural bob parameters in the exe.
  - `viewmodelDuckOffset` exists as a string (no code references found).

## 5. Hit feedback (thin)
- Flinch is animation-driven: many `ACT_FLINCH_*` and `ACT_GESTURE_FLINCH_*` activities, chosen by hit zone and direction (selection functions `1422a7b60` and `142425660`).
- `DAMAGEFLAG_NOPAIN` suppresses the flinch.
- Impacts come from `impact_effect_table(_aliases)` (offset 0x14d8) and `fx_impact_{neutral,enemy,ally}`. `weapon_tracers` and `r_drawtracers` handle tracers.
- The hit-marker logic is script-side (Squirrel). I found no native parameters.

## 6. What to copy into the KCD2 gun mod
Copy the structure, not the numbers. These are suggestions based on inferred behaviour; none of the formulas is verified from the binary.
1. **Recoil model** (Lua per-weapon table):
   - Shot kick = pattern lookup by shot index × `(base + random excluding a ±dead zone)` × stance scale (ADS, crouch, move, air).
   - Add a spray accumulator with a decay delay and rate, scaled between min and max.
   - Split the result into a hard (permanent) part and a soft (self-recovering) part.
2. **Viewmodel jolt**: a spring driven by a per-shot impulse (backward, side, roll).
3. **Spread**: a state table with fallbacks, per-shot add with a cap, a delay then decay, and a moving rate of about 75 units/s.
4. **ADS**: viewmodel offset interpolated over the first 50% of the zoom fraction, with FOV and viewmodel FOV handled separately. ADS move-speed multiplier 0.5, ADS in/out about 0.1 s.
5. **Hit feedback**: damage-scaled camera punch (`damage_viewpunch_scale`) plus a directional flinch chosen by hit zone.

Files in `work/apex/`:
- `fields.tsv` (full weapon-settings schema)
- `strings1.txt`, `strings2.txt` (string hits with references)
- `scripts/`
- `dec*.c` (decompiles of non-relevant candidates, found by offset matching; largely noise)
