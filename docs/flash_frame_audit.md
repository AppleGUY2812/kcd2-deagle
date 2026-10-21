# Muzzle flash frame audit: Ready or Not vs the flash-rolls worktree (2026-10-20)

Auditor: Sonnet agent (read-only on the implementation). Checked: Claude 4's worktree `flash-rolls` (uncommitted: `flash.lua`,
`deagle_main.lua`, `tools/ron/flash_bake.py`, `flash_ref.py`, `flash_preview.py`, tests) against RoN's decoded
`WeaponFlash_Pistol_V3` (docs/ron/ron_flash_pistol_v3.md, cascade_semantics.md) and the user's clips. The skins are not built yet, so "ours" below
is the bake run into a scratch dir (`FLASH_RON_OUT=...`, the worktree's own `flash_bake.py`) and drawn flat with the project's own camera model
(`flash_ref`: eye 41 cm behind the muzzle, 55 deg). It shows the cards' shapes, sizes, colours and timing. It does NOT show KCD2's tonemapper, the entity
lights, the sparks particle effect or the smoke.

## Verdict

**Not 1:1 yet, but close on the fireball itself.** The fireball cards (shape, size, flipbook flicker, per-frame change, 0.04 s life,
variants with and without the front pop) match RoN's data and visibly resemble the clip. What does not match, worst first:
1. **The user's own settings file forces `flashChance 1`** (every shot flashes, no smoke-only shots; RoN: 0.65). One command fixes it.
2. **Scene light**: RoN lights the whole scene orange-red for the flash's life (in the slow-mo clip the frame's mean brightness goes up 16-45 % over the
   flash frames; at normal speed +21 % on the flash frame, +11 % and +5 % on the next two). Ours is an entity light with a different fall-off,
   uncalibrated (intensity 0.5 "not matched"). I could not see it offline: unverified, the biggest risk to "looks like RoN".
3. **Sparks**: RoN shows dozens of bright glints from the first drawn frame; ours are a KCD2 particle effect one+ frames late, started 10 ms in.
4. **Too white and flat**: at the project's fitted exposure (0.135) the decoded fireball is a flat white blob with a red rim; the clip is
   cream-orange with visible structure and no clipped pixels. (Exposure fit problem in `flash_ref.EXPOSURE` / `flash_bake.RON_EXPOSURE`, upstream of the cards.)
5. In slow motion the 3 baked rolls per stage repeat (the same 3 fireball images cycle inside every 8-frame stage); thin parts (side flame, long streak,
   star) step in size/pop in late. Invisible at normal speed.
6. Heat haze: not built. Smoke: not judged here.

## What to look at (PNGs, "Deagle Progress\flash audit\")
Row meaning in 1-3: top = the RoN clip (2026-10-20 21-11-46.mkv, slow-mo ~0.05, crop 900x600 around the muzzle, every 4th video frame, label = game ms);
middle = RoN data run by `flash_ref` (continuous time, per-frame draws, exposure 0.05, includes its sparks); bottom = OURS (the baked cards, the card the Lua
would show on that frame: stage by game time, a new roll each frame, material-mean brightness, no sparks/light).
- `1_RoN_vs_ours_big_fireball_shot.png`: the shot at clip frames #735-779 (front pop on), 12 columns in 2 blocks.
- `2_frame_by_frame_flicker_12_consecutive_frames.png`: the same shot, 12 CONSECUTIVE video frames (stage change at the 8th), RoN vs ours.
- `3_RoN_vs_ours_small_core_shot_no_pop.png`: clip frames #621-665, a shot without the front pop (small core + star + red haze).
- `4_normal_speed_RoN_vs_ours.png`: RoN at normal speed (20-48-58.mkv, flash on clip #82 only) vs the no-pop variant at 60 fps.
- `5_RoN_clip_dim_vs_bright_frames_with_sparks.png`: 12 clip frames of shot B showing the dim/bright alternation and the sparks.
Scratch copies: /tmp/claude-1000/-home-johnpork/c0dd4493-5265-416b-a759-3d5c165b4183/scratchpad/flashaudit/ (script: tools/strips.py).

## What looks different, worst first (with the fix)

**1. Every shot flashes (settings, WRONG, not code).** `deagle_settings.txt` next to KCD2's exe
(`E:\SteamLibrary\...\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\deagle_settings.txt`, line `flashChance 1`) is replayed at every load
and overrides the shipped 0.65 (deagle_main.lua:115). The user sees no smoke-only shots; RoN has 35 % of them (clip: 2 of 8 shots, #272, #521).
Fix: in game `deagle_set flashChance 0.65` (or `deagle_reset`). The same file also has `slowmo 0.01`, `flashMesh.look ron`, `flashMode mesh`.
Other per-shot odds in the code are right: pop on 4 of 8 baked variants (RoN burst CountLow 0 / Count 1 = 50 %), each light 80 %
(`flashMesh.light.chance 0.8`; main light only with the pop, so ~26 % of all shots at 0.65 x 0.5 x 0.8), core light 52 % of shots.
(docs/ron/ron_flash_pistol_v3.md says the pop spawns "1 always"; flash_ref and the clip say 0-1. The clip has both kinds: big fireballs at #735, #853, #974, small-core-only
shots at #376, #621. The doc line should be corrected.)

**2. The scene light (likely WRONG in amount and shape, unverified in game).** Clip: the whole yard, ground, canopy and the smoke around the gun go
red-orange from the first flash frame (shot A #974: mean frame brightness 84 -> 117-128; shot B 84 -> 89-105 incl. the fireball; normal speed 51 -> 62, 57, 53.5 over
three video frames = the linear fade of the 0.04 s light). Code: light = particle colour x alpha x grey 10-25 x brightness (70-80 / 15-50), radius
100 x sprite size (25-52 m), exponent 320. Ours (flash.lua `ronLightColour`, `RON_LIGHTS`): right fade and per-frame flicker, right odds, but a fixed hue
`1 : 0.27 : 0.15`, constant radius 40 m with KCD2's inverse-square falloff, `intensity 0.5` never matched to the clip, and the core light's colour
randomness is reduced to its red channel. RoN's exponent-320 profile is Gaussian-like (visible out to roughly 7-12 m once the huge brightness is applied: my reading of the
formula, the exponent shader is not in the exe); inverse-square has a long tail and a hot near field instead. Fix: in game, shoot at a wall at 2, 5, 10 m
and match the on-screen brightness ratio to clip frames #735-745 / #974-985; add a fall-off that is steeper than 1/d^2 (e.g. 2 lights, or a smaller radius with a higher intensity).

**3. Sparks (WRONG timing, APPROX look).** In RoN the 10-ish embers are already spread over ~20-40 cm in the first drawn frame (#735) and make dim frames look "full of glitter"
(image 5). The data says they are born at 0.01 s, which at 0.05 slow-mo is 12 video frames later, so the data and the clip disagree here (cause unknown: possibly a pre-aged
first tick; see "open"). Ours: `muzzle_sparks_ron` has SpawnDelay 0.01 and is drawn at least 1 frame after the shot, one fixed sheet frame per particle (RoN re-rolls it every
~3.8 % of life: every frame at 60 fps, about every 3-4 frames at 0.05), no 5x gun-velocity inheritance. Count `10,Random=1` = 0-10, fine.
Fix: try SpawnDelay 0 and compare with clip #735-742; accept the fixed sheet frame.

**4. Colour and brightness of the fireball (APPROX, calibration).** Clip fireball body: median RGB 235-250 / 197-207 / 164-175, never pure white (0 % of crop pixels
above 250 in all channels). The decoded sim at the documented exposure 0.135 has 6-8 % pure white pixels and a red rim; at 0.02-0.05 it
matches the structure much better (0.1-1 % white). The cards inherit this because `flash_bake.RON_EXPOSURE` is the same 0.135, and KCD2's own tonemapper is then solved on top
(not checked in game). Fix: re-fit `RON_EXPOSURE` against a whole-fireball histogram, not only the body (flash_ref.py:~line 24 and flash_bake.py RON_EXPOSURE), then judge in game.
Per card the bake is fine: the material-mean brightness is within +-20 % of each card's true brightness for 92 % of quads (worst 0.76x / 1.41x).

**5. Slow-mo repetition and steps (APPROX; visible only at slow motion).** RoN draws a new random tile (0-22 of the 4x4 sheet), core colour and
light level every rendered frame; we draw one of 3 baked rolls per frame, never the same twice in a row (verified: `newRoll`), so a stage's 8 slow-mo frames
cycle through 3 images and a 48-frame flash shows 18 distinct pop images per variant against ~35 in RoN. Size and brightness are sampled at each stage's middle:
pop width +-4 %, alpha +-0.08 of peak per stage (core +-0.10); side pop (grows x0.12 -> 1) +-61 % in stage 1, 27 % in stage 2, then 18-9 %; small flame +-19 % then 14-8 %. The
streak and star (born 10 ms) first show in stage 3 = 13.3 ms, 4 video frames late at 0.05 and already at their 16.7 ms size. At 60 fps (2 frames: ages ~8 and ~25 ms) none of this is visible.
Fix if wanted: rolls 3 -> 6 and stages 6 -> 8 (cards 8x8x6 = 384 per gun) and bake stage 1 at 0.2 stage to start thin parts small; or leave it for slow-mo.

**6. Sheet frames 16-22 (checked, OK).** The pop's frame index is 0-22 on a 16-tile sheet; the cards use `index mod 16`. Test: the sheet has dark tiles 5, 12, 13 (and dim 8, 9); mod 16 predicts dark pop frames in 4/23 = 17 % of frames,
"16-22 draw nothing" would predict 10/23 = 43 %. Clip, shot B, frames 736-778: 7 of 43 = 16 % dim frames (#743, 745, 750, 751, 764, 766, 770). So mod 16 is right.

**7. Heat haze: not built (WRONG, missing layer).** RoN's `HeatDistortion` emitter and the lit barrel smoke are unknown/unverified in the data (gun_smoke_shells.md 1.3); the user saw it in the clip.
Smoke itself (`deagle.muzzle_smoke_ron`) was not compared visually here. In the clip the smoke is lit red-orange by the flash light for the 0.04 s (image `3`, whole frame pink): that depends on fix 2.

**8. Normal speed (OK / open).** Ours shows 2 flash frames at 60 fps (ages 8 and 25 ms, stage 2 and 4); the one RoN normal-speed clip (a no-pop shot, 85 fps game) shows the flash on one
video frame (#82) and the light on three. The decoded 0.035-0.04 s lifetimes predict 2 video frames. One sample, cannot say if RoN draws 1 or the capture dropped a frame.
Look at image 4: the no-pop variant is the same size and colour family as the clip (orange-yellow soft cloud).

## Cascade: what re-rolls every frame, what is fixed at spawn (docs/ron/cascade_semantics.md; confirmed against the clip where noted)
| property | when it is drawn | ours |
|---|---|---|
| pop sheet frame (0-22), core sheet frame (0-10), long streak frame (0-2) | every rendered frame (clip: dim/bright flicker, yes) | per-frame roll from 3 baked |
| core colour (R, G, B each U(low, 38250...)), light colour scale and brightness | every rendered frame | core: 3 rolls; light: re-drawn each frame in Lua, red channel only |
| spark drag coefficient | every frame | baked into one mean value |
| spark sheet frame | when relative life advanced >0.038 (about each frame at 60 fps, 3-4 frames at 0.05) | one fixed frame |
| size, rotation, pivot, start position, side pop / small flame tile, light on/off (80 %), pop 0-1, spark count 0-10, spark random acceleration | once at spawn | 8 baked shots (pop on 4); light 80 % rolled in Lua |
| size curve, alpha 1->0, light alpha fade | continuous over life | 6 stages of 6.7 ms (errors above); light continuous |

## Timelines (short)
RoN, `WeaponFlash_Pistol_V3`, game ms after the shot. Slow-mo 0.05: 1 video frame = 0.833 ms (clip: flash visible 45-47 frames: 0.040 s / 46 f x 60 = 0.052, so the factor is 0.05-0.052, verified).
| layer | born | dies | slow-mo 0.05 video frames | 60 fps frames |
|---|---|---|---|---|
| front pop + its light (pop 50 %, light 80 % of pops) | 0 | 40 | 0-47 | 1-3 (3rd is the last 7 ms) |
| side pop (event from pop) | 0 | 40 | 0-47 | 1-3 |
| pistol core (always, light 80 %) | 0 | 35 | 0-41 | 1-3 |
| small flame | 0 | 35 | 0-41 | 1-3 |
| sparks 0-10 | 10 | 60-95 | 12-114 | 1-6 |
| long streak, star (1 each) | 10 | 40 | 12-47 | 1-3 |
| muzzle smoke (every shot), heat haze | 0 | seconds | | |
Whole flash is rolled once per shot at 65 %.

Ours (worktree), stage s = game ms [6.67 (s-1), 6.67 s): quads on the card: s1 4 (pop, side, core, flame; 2 without pop), s2 4, s3-s5 6, s6 4 (no core/flame). Slow-mo 0.05: stage s = video frames
8(s-1) .. 8s-1, roll changes every frame. 60 fps: frame 1 = stage 2 (age 8.3), frame 2 = stage 4 (age 25), then nothing. Light: loaded on the shot's frame, colour reset each frame, freed at 0.04 / 0.035 s.

## Open / not checked
- The first drawn frame of the clip looks pre-aged (sparks already spread, ring-like pop); the data's 0.01 s spark birth does not explain it. Unresolved.
- On-screen fireball size vs the clip is confounded by camera geometry (the eye offset is KCD2's, RoN's gun pose varies per shot): in shot B the clip's fireball is ~1.2-1.4x wider than the sim, in shot C ours is bigger; world sizes follow the data.
- Entity light intensity/fall-off, KCD2 tonemapper and sparks effect: need an in-game clip. The Lua tests in the worktree were not run (read only).
- Not read: RoN's shader for light exponent and sheet addressing; flipbook frames 16-22 only by the dim-frame statistics above.
