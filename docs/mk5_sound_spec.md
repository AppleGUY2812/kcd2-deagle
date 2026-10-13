# Spec: Ready or Not's MK V gunshot for the Deagle, picked by where the player stands

Goal (the user's): the Deagle sounds exactly like Ready or Not's MK V (.50 AE) does in Ready or Not, in every kind
of place. The Deagle is the first gun; the same system should take other guns later.
Research and evidence: `docs/ron_fmod_events.md`. Owner: fix-glock-animation-index (deagle_main.lua / native).

## 1. What Ready or Not does (verified)
One first-person shot is exactly this, nothing else (read from its FMOD banks with its own FMOD 2.03.10 engine,
checked against the user's in-game recordings: 0.7 dB average match outdoors):

| layer | what | volume |
|---|---|---|
| shot | 1 of `Grizzly-1..4` at random (0.37 s) | 100 % |
| echo | inside: `GRIZZLY_Reflection_INT_Small`, `_INT_Base`, `_INT_Large` **all three together**; outside: the three `_EXT_` files. 2 takes each (_01/_02), random | 100 % each |
| ringing | FMOD Reverb on a send, settings from the room-type snapshot the map's doorway boxes switched on (table in ron_fmod_events.md). Indoor send 0.56, outdoor 0.18 | per snapshot |
| squeeze | Limiter (ceiling -2 dB, release 220 ms) + Distortion 0.12 on the weapon bus, Limiter -1 dB on master | |

The map decides inside / outside (standing in a hand-placed `RoomVolume` = inside, consistent with the clips) and the
room type (hand-placed doorway triggers set one of: bathroom, small, corridor, mid, hall, street, forest, cave, plus
an optional wall material: concrete, wood, metal, fabric). KCD2 has no such boxes, so the mod must classify live.

## 2. Assets (done, gitignored, packed by build.sh as loose sounds)
`assets/build/sounds/mk5/<class>/shot-1..8.wav`: 8 takes per class, each already the complete Ready or Not output
(shot + 3 echoes + ringing + limiters), 44.1 kHz 16-bit stereo, 3.95 s, rendered with the engine's final fader at
0.5 (at 1.0 the indoor sets overflow 16 bit; indoor stays louder than outdoor, as in Ready or Not; `shot_gain.lua`
sets the playback level). Made by `tools/sound/render_mk5_sets.sh`
(Ready or Not's own engine via `tools/ron/FmodRender`; re-run it to add classes). Nothing is synthesized.

| class | KCD2 places | made from |
|---|---|---|
| `out_forest` | forest, woods, field edge (default outdoors) | outside + Forest |
| `out_street` | village street, open field, town square | outside + Street |
| `out_covered` | under a roof but open sides: porch, market stall, barn door, bridge underside | outside + Mid + Concrete |
| `in_small` / `in_small_wood` | small room, cottage room, stall | inside + Small (+ Wood) |
| `in_mid` / `in_mid_wood` | normal room, workshop | inside + Mid (+ Wood) |
| `in_hall` / `in_hall_wood` | tavern hall, church, castle hall, big barn | inside + Hall (+ Wood) |
| `in_corridor` / `in_corridor_wood` | passage, narrow alley-like interior, staircase | inside + Corridor (+ Wood) |
| `in_cave` / `in_cave_stone` | cellar, mine, cave, crypt | inside + Cave (+ Concrete) |

`decho/decho-1..8.wav` currently = `out_forest` (interim, so the Deagle already sounds right outdoors).
`tools/shot_loudness.py` must also measure the mk5 files (add them to its file list) so `shot_gain.lua` has entries.

## 3. Runtime behaviour
1. **Classify** where the player's head is, every 0.25 s (not per shot, so firing costs nothing), with
   `Physics.RayWorldIntersection` from the eye, static + terrain only, skipping the player:
   - `up`: 5 rays in a 15 deg cone straight up, 30 m. Roof = at least 3 hit within 12 m.
   - `ring`: 16 horizontal rays (every 22.5 deg) at eye height, 40 m. Note distance and surface
     (`System.GetSurfaceTypeNameById(hit.surface)`, groups in docs/kcd2_impact_fx.md section 1).
   - **inside** = roof AND at least 12 of 16 ring rays hit within 20 m.
   - inside, size from the median ring distance d: d < 3.5 m `small`; 3.5-7 m `mid`; > 7 m `hall`.
     `corridor` if two opposite rays are both < 2.5 m and the perpendicular pair both > 6 m.
     `cave` if > 50 % of ring hits are stone (`mat_rock*`) and the roof hit is stone or terrain (cellars, mines).
   - wood variant if > 50 % of inside ring hits are wood (`mat_wood*`, `mat_door_wood`, `mat_stairs_wood`).
   - roof but NOT inside (open sides) = `out_covered`.
   - no roof: `out_forest` if > 25 % of ring hits are vegetation (`mat_vegetation*`, `mat_bushes`, `mat_canopy`,
     trees) or within 40 m there are trunks on 6+ rays; else `out_street`.
   - Switch class only after 2 classifications in a row agree (about 0.5 s), like Ready or Not's doorway fade.
   These thresholds are starting values: tune by ear in game.
2. **Fire:** play a random take of the current class (never the same take twice in a row) through the existing
   `Deagle:Snd` / `Deagle:ShotGain` path, exactly like the `decho` files today. One voice per shot, no extra layers,
   no live reverb or echo DSP.
3. **Rapid fire:** in Ready or Not the limiters act on overlapping shots together; here each take was limited on its own,
   so overlapping shots sum a bit louder. v1: accept. v2 if it sounds too loud: an FMOD Limiter DSP (ceiling -2 dB,
   release 220 ms) on the mod's gunshot channel group (KCD2's FMOD 2.02 core has it).
4. **Debug:** console command `deagle_env` prints the current class and the ray numbers (roof hits, ring hits, median
   distance, surface shares); optional on-screen line while `deagle_env_debug 1`.

## 4. Done when
- Lua tests: the classifier on mock ray results (open field, forest, porch, cottage room, tavern, corridor, cellar)
  returns the expected class.
- In game (user, game closed for install, then launched by the user): standing in each of those places, `deagle_env`
  prints the expected class and the shot sounds like the matching file in
  `Deagle Progress\14 Ready or Not rooms (real engine)`.
- Shots never cut out, no audible jump when the class changes between shots.

## 5. Not in this spec
Other people's shots (Ready or Not's `3P Grizzly Fire`: distance, direction, walls in between), MK V reload / draw
sounds (timings tied to our animations), suppressed version.
