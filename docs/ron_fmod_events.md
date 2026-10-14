# Ready or Not's gun sound, read from its own FMOD engine (2026-10-12)

Goal: replicate Ready or Not's gunshot sound exactly, with the Deagle (Ready or Not's MK V .50 AE) as the first gun.

## How
`tools/ron/FmodRender/` (C#, .NET 10, P/Invoke) loads `Master.bank`, `Master.strings.bank` and `Weapons.bank` with
Ready or Not's own `fmod.dll` / `fmodstudio.dll` (FMOD **2.03.10**, copied next to the exe, not in git) and either lists
events / parameters / the mixer, or renders an event to WAV with FMOD's offline wav writer (48 kHz stereo).
Built in `E:\Tools\deagle-work\ron\fmodrender` (`dotnet build -c Release -o bin`). Read-only on the game.

```
FmodRender.exe list   <bankdir> grizzly
FmodRender.exe render <bankdir> out.wav "event:/Weapons/Grizzly/1P Grizzly Fire" 4 IsOutside=1 snapshot:/Reverb_OUT_Forest01 [+ 4 IsOutside=1 ...]
```
Env `DUMP_ALL=1` (from WSL: `WSLENV=DUMP_ALL`) prints, for every shot, which recordings play and at what volume/pitch.
In a `+` shot's own arguments, `ev:<event path>` plays another event and `vol:<x>` sets its instance volume, so one
file can hold a gunshot and the impact its travel time later, mixed through the same buses and limiters
(`tools/sound/impact_level_ab.py`).

**Check against the real game:** 8 shots rendered at the user's video spacing vs `Deagle Progress\test records\
2026-10-12 11-53-12.mp4` (gas station, outdoors): 3 bands x 5 windows (0-800 ms), **0.7 dB average miss, 2.7 dB worst**
(bass, 0.55-0.8 s). Aim made no difference; the street snapshot moved things ~0.1 dB.

## What Ready or Not does (MK V)
- The MK V's internal name is **Grizzly** (`Plugins/GameFeatures/ReadyOrNotDLC3/Content/Weapons/Grizzly`, `50AE_Casing`).
  Waveform match of the user's video to `Grizzly-1`: 0.98 (other pistols <= 0.54).
- `event:/Weapons/Grizzly/1P Grizzly Fire`: 2D, 3.87 s. Parameters: `IsOutside` 0..1, `Aim` 0..1, `FireMode`, `IsSupressed`.
  `3P Grizzly Fire` (other shooters): 3D, 1..150 m, adds `Distance`, `Direction`, `Elevation`, `Occlusion`, `PropagateDistance`.
- One shot = a random take of `Grizzly-1..4` **plus all three echoes at once** for inside or outside
  (`IsOutside=1`: `EXT_Small` + `EXT_Base` + `EXT_Large`; `=0`: `INT_Small` + `INT_Base` + `INT_Large`), each a random
  take of _01/_02, all at 0 dB. Small/Base/Large are **not** chosen by room size. (Tests 3-5 played one echo file: wrong.)
  In 17 renders `Grizzly-1` never came up; the game's random pick may weight it out (not checked in the bank).
- Mixer: group bus with **Limiter** (ceiling -2 dB, release 220 ms, linked), **Distortion** level 0.12, a send to a
  return bus with **FMOD Reverb** (default 1.5 s decay, HF cut 5 kHz); master bus **Limiter** (ceiling -1 dB, 160 ms).
- Room character comes from **snapshots** a map's room zones switch on: `Reverb_IN_Small01`, `IN_Bathroom01`,
  `IN_Corridor01`, `IN_Mid01`, `IN_Hall01`, `OUT_Street01`, `OUT_Forest01`, `OUT_Cave01`, plus material snapshots
  (`Reverb_METAL/FABRIC/CONCRETE/WOOD`) and global parameters `ReverbSwitch` 0..7, `ReverbHALL`, `ReverbSMALL`, ...
  Level 1 s after the shot: bathroom -44 dB, small -40, mid -33, corridor -32, hall -25, street/forest -26, cave -22.

## In the mod (done, waiting for install)
`assets/build/sounds/decho/decho-1..8.wav` = 8 renders of `1P Grizzly Fire`, `IsOutside=1` + `Reverb_OUT_Forest01`
(44.1 kHz). CoD originals backed up in `E:\Tools\deagle-work\sound\cod_decho_backup`. `shot_gain.lua` regenerated.
KCD2's FMOD is 2.02 and can't load Ready or Not's 2.03 banks, so the mod plays pre-rendered output.

## Next (commander: deagle_main.lua / native)
1. Pick the room type where the player stands (rays: roof overhead = inside; open distance and hits -> small / mid /
   hall / corridor; outside -> forest / street / cave) and play that room's pre-rendered set (8 takes each, rendered
   like the forest set).
2. Rapid fire: the limiters act across overlapping shots in game; pre-renders are limited one shot at a time. Render a
   few rapid-fire takes if overlapping shots sound louder than in Ready or Not.
3. Other shooters: `3P Grizzly Fire` with `Distance` / `Occlusion` (render a distance ladder).

## How a map picks the reverb (read from RoN_Gas_Audio_2.umap and the blueprints, 2026-10-12)
- `Sound_ParameterTransition_V2_BP` actors sit in doorways (a box + a spline that says which side is in / out).
  Crossing one crossfades **global** FMOD parameters (`ReverbSMALL` ... `ReverbCAVE`, `ReverbMaterial*`) along the
  spline, using line traces (refresh 0.13 s; inside range 0.3-1.0, outside 0-0.7). Each has `Reverb IN` / `Reverb OUT`
  (+ optional material) as `ParameterTransition_Reverb_Enum` **values** (display order is not value order):
  0 None, 1 BATHROOM, 2 SMALL, 3 CORRIDOR, 4 MID, 5 HALL, 6 STREET, 7 FOREST, 8 CAVE.
  Materials: 0 None, 1 CONCRETE, 2 WOOD, 3 METAL, 4 FABRIC.
- Gas station: bathrooms 1, freezer / closet 2, halls 3, pump canopy 4 + CONCRETE (out side 6), diner 5,
  start of map `ReverbCommand_Street` (`Sound_StarterAmbience_BP`). 45 `RoomVolume`s (rooms, `RoomGroupID`),
  41 `PortalVolume`s (doors / windows; `bIsOutside` on the ones that open to the street), 16 `ReadyOrNotAudioVolume`s
  (ambience). `IsOutside` on the gunshot: set in C++ (not decompiled); the clips fit "inside" in rooms and "outside"
  on the forecourt, consistent with "inside a RoomVolume = inside".
- `event:/Levels/Reverbs/Commands/ReverbCommand_*` (in `Global Amb.bank`) read those globals; in the offline render
  starting them did not switch a snapshot on (the game's own C++/blueprint path does). Rendering with the snapshot
  directly gives the room sound.
- What a room snapshot does (dumped from the mixer while playing): opens the weapon bus's reverb **send** (none: 0 =
  no reverb at all; indoor + cave 0.562; street + forest 0.178) and sets the shared **FMOD Reverb**:

| snapshot | decay ms | early / late ms | HF ref / ratio | diffusion | low shelf Hz / dB | high cut | E/L mix |
|---|---|---|---|---|---|---|---|
| IN_Small01 | 1400 | 8.5 / 19.5 | 4600 / 74.5 | 60 | 290 / +1.5 | 4000 | 36 |
| IN_Bathroom01 | 1200 | 11.5 / 20 | 3400 / 63 | 52 | 400 / +1.5 | 8500 | 30 |
| IN_Corridor01 | 2000 | 24 / 40 | 4600 / 63.5 | 36 | 92 / 0 | 3600 | 38 |
| IN_Mid01 | 1700 | 17.5 / 57 | 3400 / 64 | 71.5 | 110 / -2 | 8500 | 45 |
| IN_Hall01 | 2600 | 42 / 54 | 3400 / 64 | 69.5 | 160 / -4 | 4000 | 47 |
| OUT_Street01 | 3200 | 52 / 69 | 3400 / 67.5 | 53.5 | 160 / -20 | 3600 | 51 |
| OUT_Forest01 | 1500 | 110 / 88 | 5000 / 53.5 | 78.5 | 250 / -4 | 1500 | 75 |
| OUT_Cave01 | 2100 | 27 / 50 | 6500 / 50 | 100 | 120 / 0 | 2500 | 42 |
| (materials, layered) CONCRETE / WOOD / METAL / FABRIC | 1500 / 1500 / 3500 / 1500 | | HF ref 5000 / 2600 / 1550 / 1800 | | | 4000 / 5000 / 3200 / 1150 | |

- User's uncompressed clips (`Deagle Progress\mkv sound clips`, 2026-10-12): freezer fits inside + SMALL best of 20
  (2.5 dB, ambience-limited), open forecourt fits outside (+ STREET 0.9 dB), the diner clip has music in it.

## The real room blend (verified offline in Ready or Not's engine, 2026-10-12)
**Recipe:** load `Global Amb.bank`, start `event:/Levels/Reverbs/ReverbMaster` once and keep it running, then drive
the GLOBAL parameters. That is the whole mechanism; partial values blend.
- `ReverbMaster` (Global Amb.bank, 0 ms, never stops) holds 12 event instruments (the bank's `EVIS/EVIT` chunks
  reference the 12 snapshot GUIDs: 8 rooms + 4 materials), each snapshot's intensity follows one global:
  `ReverbBATHROOM / SMALL / CORRIDOR / MID / HALL / STREET / FOREST / CAVE` and `ReverbMaterialCONCRETE / WOOD / METAL /
  FABRIC` (all 0..1). `ReverbSwitch` (0..7) alone changes nothing.
- `ReverbCommand_<Room>` / `ReverbMaterialCommand_<Mat>` (0 ms one-shots, `CMDS/CMDI` command instruments) just SET those
  globals: the room's to 1, the other rooms' to 0 (tested: ReverbSMALL=1 then ReverbCommand_Hall -> pure hall).
  `Sound_StarterAmbience_BP` fires one at map start (gas station: ReverbCommand_Street); the doorway blueprint
  (`Sound_ParameterTransition_V2_BP`) sets the globals directly with partial values while you cross it.
- Why the first offline test failed: only the command / snapshot was started, never `ReverbMaster`.
- Checks (mixer dump of the weapon-bus send + the shared FMOD Reverb while a shot plays, IsOutside=0):

| globals | send | decay ms | early / late ms | HF ref | low shelf | high cut | E/L |
|---|---|---|---|---|---|---|---|
| none | 0 | 1500 | 20 / 40 | 5000 | 250 / 0 | 5000 | 50 |
| HALL=1 | 0.562 | 2600 | 42 / 54 | 3400 | 160 / -4 | 4000 | 47 |
| SMALL=1 | 0.562 | 1400 | 8.5 / 19.5 | 4600 | 290 / +1.5 | 4000 | 36 |
| HALL=0.5, SMALL=0.5 | 0.541 | 2357 | 30.2 / 45.3 | 3662 | 185 / -2.9 | 4037 | 45.3 |
| HALL=0.25 | 0.296 | 2079 | 28.0 / 47.4 | 4158 | 203 / -2.1 | 4474 | 48.4 |
| STREET=1 | 0.178 | 3200 | 52 / 69 | 3400 | 160 / -20 | 3600 | 51 |
| SMALL=1, MaterialWOOD=1 | 0.562 | 1400 | 8.5 / 19.5 | 2600 | 400 / +2 | 4000 | 46 |

- Audio check: `ReverbMaster` + ReverbHALL=1 rendered with the same takes as the hall snapshot render is identical
  (difference -200 dB, i.e. bit for bit).
- A room "holds": inside a small concrete room ReverbSMALL=1 and ReverbMaterialCONCRETE=1, all other room and material
  globals 0. In a doorway the blueprint fades the two sides' globals along its spline (inside range 0.3-1.0,
  outside 0-0.7, traces every 0.13 s), e.g. ReverbSMALL 0.6 + ReverbCORRIDOR 0.4 halfway through.
- Snapshot instance volume does NOT set intensity (tested); use the globals.

## Surface / Material numbers (part 2, 2026-10-12)
The `Surface` (1..53: Bullet Impact Surfaces, 9mmShell / 556Shell / 762Shell / Shotgunshell) and `Material` (1..62:
weapon drops, Bodyfalls) parameters are Ready or Not's UE physical-surface index, `SurfaceTypeN` in
`ReadyOrNot/Config/DefaultEngine.ini` (53 defined; 54-62 unused):
1 Aluminium, 2 Asphalt, 3 Brick, 4 CarbonFibre, 5 Cardboard, 6 Ceramic, 7 ConcreteSoft, 8 ConcreteStrong, 9 Dirt,
10 Drywall, 11 Electrical, 12 EnergyShield, 13 Fabric_Carpet, 14 Fabric_Stuffing, 15 Fabric_Thin, 16 Flesh,
17 Galvanized, 18 Glass_Plate, 19 Glass_Windshield, 20 Grass, 21 Gravel, 22 Ice, 23 Lava, 24 Lead, 25 Leaves,
26 Limestone, 27 Mahogany, 28 Marble_Coated, 29 Marble_Thick, 30 Mud, 31 Oil, 32 Paper, 33 Pine, 34 Plaster,
35 Plastic, 36 Plywood, 37 Polystyrene, 38 Powder, 39 Rock, 40 Rubber, 41 Sand, 42 Snow, 43 Soil, 44 Steel, 45 Tin,
46 Treewood, 47 Wallpaper, 48 Water, 49 Shield, 50 Cloth, 51 Vehicle, 52 Bulletproof_Glass, 53 Leather.
Checked by sweeping each value in Ready or Not's engine and reading which sample played, e.g. impacts 2 imp_asphalt,
3 imp_bricks, 6 imp_tile, 7/8 imp_concrete, 9 imp_soil, 10 imp_dry_wall, 16 weap_impact_gore, 17 imp_galv,
18 imp_glass_sweet, 19/52 imp_windshield, 27/35/36/46/47 imp_wood, 33 imp_pine, 34 imp_dry_wall, 44/45 imp_steel,
48 imp_water, 49 shield hit, 51 imp_carbody; 9mm shells: metal / concrete / dirt / carpet / gravel / wood / water drops.
**Silent for Bullet Impact Surfaces** (no sound at all, checked by rendering): 4, 11, 12, 22, 23, 25 Leaves, 30 Mud,
31, 37, 42 Snow, 43 Soil. Weapon drops play pistol drop variants for every value (wood variants at 22/27/33/46).

Proposed KCD2 `mat_*` -> RoN index (groups from docs/kcd2_impact_fx.md section 1; avoids the silent ones):

| KCD2 | RoN | why |
|---|---|---|
| mat_rock, mat_rock_unwalk, mat_rock_horse_ignore | 39 Rock | |
| mat_stairs_stone | 26 Limestone | cut stone (brick-like impact) |
| mat_plaster | 34 Plaster | |
| mat_ceramics | 6 Ceramic | |
| mat_wood, mat_wood_unwalk, mat_door_wood, mat_stairs_wood, mat_railing_wood_seethrough | 33 Pine | planks |
| mat_wood_soft | 46 Treewood | trunks / soft wood |
| mat_shield_wood | 36 Plywood | |
| mat_wicker | 5 Cardboard | light, hollow |
| mat_metal, mat_door_metal, mat_metal_seethrough, mat_plate, mat_chainmail (+ combos with plate/chain) | 44 Steel | |
| mat_bells | 44 Steel (or the `Bullet Impact Tower Bell` event) | |
| mat_soil, mat_soil_unwalk, mat_forest, mat_field, mat_road | 9 Dirt | 43 Soil is silent |
| mat_mud, mat_mudwater | 9 Dirt | 30 Mud is silent |
| mat_gravel | 21 Gravel | |
| mat_grass, mat_grassdry, mat_grass_tall | 20 Grass | |
| mat_bushes, mat_vegetation, mat_vegetation_seethrough, mat_canopy, mat_thatch | 20 Grass | 25 Leaves is silent |
| mat_water, mat_slowwater, mat_rockwater, mat_puddle | 48 Water | |
| mat_glass, mat_window | 18 Glass_Plate | |
| mat_fabric, mat_fabric_items, mat_sack | 15 Fabric_Thin | |
| mat_organic_stuff | 14 Fabric_Stuffing | |
| mat_leather (+ leather_chainmail) | 53 Leather | |
| mat_flesh | 16 Flesh | |

## MK V handling, casing and other gun events (part 3, 2026-10-12)
- **Handling sounds are NOT notifies on the first-person animations.** The FP arm / weapon sequences carry only gameplay
  notifies (`ReloadComplete`: speed 1.364 s, speed empty 1.717 s, speed empty ADS 1.95 s, tac 2.252 s, tac ADS 2.278 s;
  `HolsterComplete` 0.570 s). The sounds are `SoundSourceAnimNotify` on the **third-person body** sequences
  (`Animations/TP/actions/body/tp_swt_stand_grizzly_*`), each starting ONE full-length FMOD event at the start of the
  action, attached to a hand bone and following it (3D):

| action (FP / TP length) | event | at | bone |
|---|---|---|---|
| draw (0.833 s) | `Weapons/Shared/Draw/Draw Pistol V2` (540 ms) | 0.188 s | hand_LE |
| first draw (1.5 s) | `Weapons/Grizzly/Grizzly First Draw` (1541 ms) | 0.005 s | hand_LE |
| holster (0.833 / 1.0 s) | `Weapons/Shared/Pistols Shared/Pistol Holster` (871 ms) | 0.180 s | hand_LE |
| mag check (3.0 s) | `Weapons/Grizzly/Grizzly Ammo Check` (3038 ms) | 0.028 s | hand_LE |
| speed reload (1.667 s) | `Weapons/Grizzly/Grizzly Reload Speed` (1705 ms) | 0.007 s | hand_RI |
| speed reload empty (1.9 s) | `Weapons/Grizzly/Grizzly Reload Empty` (1936 ms) | 0.010 s | hand_RI |
| tac reload (2.5 s) | `Weapons/Grizzly/Grizzly Reload Tac` (2535 ms) | 0.010 s | hand_RI |

  The FP and TP actions have the same lengths, so in first person the same events line up with our clips if started
  at those offsets from the action start. The part events (`Grizzly MagIn/MagOut`, `Reload Empty MagOut/MagIn/Slide
  Release`, `Reload Quick *`) are not referenced by these notifies (probably layers inside the full-sequence events).
- From `SoundData_Grizzly` (code-triggered): ADS in/out `Weapons/Shared/ADS/ADS Pistol Start` / `ADS Pistol Stop`,
  dry fire `Weapons/FN57/FN57 Dry Fire`, mag drop `Weapons/Shared/Mag Drops/Pistol Mag Drop`, weapon drop
  `Surfaces/Impacts/Weap Drop Pistol` (Material = surface index), bullet whiz heard by others `WhizzBy Rifle Close`.
- **Casing:** `BP_Secondary_LARGrizzly` ejects `BulletShell` (mesh `50AE_Casing`) with its shell sound set to
  `Weapons/Shared/9mmShell` (the class default is `762Shell`); `BulletShell` plays it on bounces
  (`ShellBounceFMODAudio`, impact speed normalised between 0.5 and 26.9) with `Surface` = the surface index it hit
  (part 2 list; 9mm shells switch metal / concrete / dirt / carpet / gravel / wood / water drops).

## Per-play parameters, impacts / casings, Glock (2026-10-12)
Parameter types (FMOD_STUDIO_PARAMETER_TYPE): t0 = game-controlled (the game sets it per play), t1 = automatic
distance, t4 = automatic direction, t5 = automatic elevation (FMOD computes t1/t4/t5 itself from the instance's and
listener's 3D attributes; flags 0x3 = read-only + automatic). flags 0x8 = discrete, 0x5 = global read-only.

| event | 3D min..max | game sets | FMOD sets |
|---|---|---|---|
| `Weapons/Shared/9mmShell` (MK V + Glock casing) | 0..10 m | `Surface` 1..53 (default 2) | - |
| `Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces` (2.0 s) | 1..20 m | `Surface` 1..53, `Occlusion` 0..1, `PropagateDistance` 0..250 | `Distance` |
| `Surfaces/Impacts/Bullet Impacts/Ricochets` | 3..45 m | `Occlusion`, `PropagateDistance` | `Distance`, `Distance Ricochets` (0..45) |
| `Surfaces/Impacts/Bullet Impacts/Spalling` | 2..15 m | `Occlusion`, `PropagateDistance` | `Distance` |
| `Weapons/G19 New/1P G19 Fire` (2D, 3.42 s) | - | `IsOutside`, `Aim`, `FireMode`, `IsSupressed` | - |
| `Weapons/G19 New/3P G19 Fire` | 1..150 m | `IsOutside`, `IsSupressed`, `Occlusion`, `PropagateDistance` | `Distance`, `Direction`, `Elevation` |

- No impact-speed parameter on the shell events. `BulletShell` (C++, not decompiled) normalises bounce speed between
  0.5 and 26.9 (`ShellNormalizeMin/Max`), probably for volume or to skip soft bounces.
- What the game-set propagation params do (Bullet Impact Surfaces, Surface 3, rendered; source at the listener, so
  levels clipped and approximate): `Occlusion` 1 muffles (spectral centroid 1706 -> 1066 Hz, level about -1 dB);
  `PropagateDistance` is an extra fade on top of FMOD's distance: 10 -> -34 dB, 50+ -> silent. For a sound with a clear
  line of sight use Occlusion 0, PropagateDistance 0 and let FMOD's 3D distance (min/max above) do the falloff.
  In Ready or Not they come from its C++ SoundPropagator through `PortalVolume`s; DefaultGame.ini
  `[ReadyOrNotSoundSettings]`: DoorOcclusionAdditive 0.7, WindowOcclusionAdditive 0.7, ObstructionOcclusionAdditive
  0.2, AngleOcclusionMultiplier 1.0, OutsidePathOcclusionDistance 750 (cm), OutsideOcclusionMultiplier 0.05 (the
  same values again for reflections).
- **Glock 17 -> Ready or Not's G19** (`SoundData_G19`): 1P `1P G19 Fire`, 3P `3P G19 Fire`, dry fire
  `Weapons/G19/G19 Dryfire Old`, mag drop `Pistol Mag Drop`, ADS `ADS Pistol Start/Stop`, whiz `WhizzBy Rifle Close`,
  weapon drop `Weap Drop Pistol`. `1P G19 Fire` = 5 layers at 0 dB: 1 of `Glock-1..4` + 3 echoes (inside
  `Glock18_Reflection_Int_Small/Base/Large`, outside `GLOCK-ExteriorReflectionLayer01 / 02Building / 03`) + a shared
  body layer (`556_Int_Med1p` / `556_Ext_Med1p`). Same rules as the MK V (IsOutside 0/1, ReverbMaster globals).
- Glock handling = many part events timed by `SoundSourceAnimNotify` on the TP body sequences (`overrides/G19G5/...`):

| action (length) | events @ time (bone) |
|---|---|
| tac reload (2.667 s) | Cloth-ReloadStart 0.059, Cloth-PouchGrab 0.250, Dry Mag Out 0.657, Wet Mag In 1.253, Cloth-ReloadMagStow 1.866, Cloth-ReloadEnd 2.387 (all hand_LE) |
| speed reload (1.6 s) | Cloth-ReloadStart 0.028, WetMagOutQuick 0.258, Cloth-PouchGrab 0.392, WetMagInQuick 0.853, Cloth-ReloadEnd 1.096 |
| speed reload empty (2.333 s) | Cloth-ReloadStart 0.028, Dry Mag Out 0.258, Cloth-PouchGrab 0.392, WetMagInQuick 0.853, Slide 1.450, Cloth-ReloadEnd 1.829 |
| mag check (2.667 s) | Mag Check Out 0.197 (hand_LE), Mag Check In 1.809 (hand_RI), Cloth-CheckDone 2.365 (hand_LE) |

  (all `Weapons/G19 New/G19 ...`, 3D 2..8 m, params Occlusion / PropagateDistance). Our Glock clips have their own
  timings, so scale the offsets to our clip lengths or retime to our clip's impacts.

## Handling events: transient timings (2026-10-12, tools/sound/ron_part_transients.py)
Each event rendered alone 6 times by Ready or Not's engine (listener at the source, Occlusion 0, PropagateDistance 0),
256-sample mix blocks. Engine output delay (exactly 10 blocks, measured with a core oscillator) is removed.
- **All click times are measured from the event's first audible sound** (engine-independent, +-5 ms run to run).
  "first sound" = ms from the start call to the first audible sample; it includes the engine's own start scheduling,
  1-3 mix blocks (5-17 ms here at 256 samples; ~20-60 ms at FMOD's default 1024). Larger values (Ammo Check 82,
  First Draw 145, ReloadStart 116, Reload Empty Start 62, Reload Quick Start 55, ADS Stop 67) contain a designed
  silent lead-in of roughly that minus ~10 ms. Calibrate once live: the MK V fire's bang = first sound.
- main click = the loudest sharp rise (>= 12 dB in 8 ms); 2nd = next loudest (level vs main in dB);
  "all clicks" lists every sharp rise within 15 dB of the main one, play 1.
- takes: "random" = several files picked per play (main-click time then varies by take: range shown);
  "fixed" = the same file(s) every play (times identical across the 6 plays).
- "sounds like" is from the measurement (sharp = 1-3 clicks, busy = many; bright / mid / dull by spectral centroid)
  plus the file names: not listened to. Grizzly MagIn / MagOut reuse Kimber recordings (KIMBER_Reload_Normal_*).

| event | takes | first sound (ms) | main click (ms) | 2nd click (ms, dB) | all clicks within 15 dB (ms) | sounds like |
|---|---|---|---|---|---|---|
| Grizzly/1P Grizzly Fire | random (9 files, 4 at once) | 11 | 0 | - | 0 | sharp dull hit; GRIZZLY_Reflection_INT_Base_01, GRIZZLY_Reflection_INT_Base_02, GRIZZLY_Reflection_INT_Large_01 ... |
| Grizzly/Grizzly Ammo Check | fixed, 1 file | 82 | 778 | 2374 (-0) | 309, 353, 466, 668, 738, 778, 2168, 2237, 2303, 2374, 2420 | busy bright mag; Grizzly_Mag_Check |
| Grizzly/Grizzly First Draw | fixed, 1 file | 145 | 569 | 528 (-3) | 2, 412, 465, 528, 569, 679, 779 | busy bright draw; Grizzly_Draw_First |
| Grizzly/Grizzly MagIn | fixed, 1 file | 15 | 251 | 136 (-7) | 136, 183, 251 | busy bright mag; KIMBER_Reload_Normal_MagIn |
| Grizzly/Grizzly MagOut | fixed, 1 file | 17 | 239 | 164 (-6) | 10, 99, 164, 239 | busy bright mag; KIMBER_Reload_Normal_MagOut |
| Grizzly/Grizzly Reload Empty MagIn | fixed, 1 file | 14 | 201 | 150 (-3) | 4, 150, 201, 356 | busy bright mag; GRIZZLY_Reload_Empty_MagIn |
| Grizzly/Grizzly Reload Empty MagOut | fixed, 1 file | 6 | 138 | 3 (-2) | 3, 138 | sharp bright mag; GRIZZLY_Reload_Empty_MagOut |
| Grizzly/Grizzly Reload Empty Slide Release | fixed, 1 file | 12 | 55 | 2 (-7) | 2, 55, 196 | sharp bright slide; GRIZZLY_Reload_Empty_SlideRelease |
| Grizzly/Grizzly Reload Empty Start | fixed, 1 file | 62 | 243 | 199 (-10) | 130, 199, 243 | busy bright hit; GRIZZLY_Reload_Empty_Start |
| Grizzly/Grizzly Reload Quick MagIn | fixed, 1 file | 9 | 106 | 10 (-2) | 10, 45, 106, 260 | busy bright mag; GRIZZLY_Reload_Speed_MagIN |
| Grizzly/Grizzly Reload Quick MagOut | fixed, 1 file | 9 | 245 | 56 (-1) | 1, 56, 99, 245, 288 | busy bright mag; GRIZZLY_Reload_Speed_MagOut |
| Grizzly/Grizzly Reload Quick Start | fixed, 1 file | 55 | 38 | 97 (-1) | 38, 97 | sharp dull hit; GRIZZLY_Reload_Speed_Start |
| Grizzly/Grizzly ReloadStart | fixed, 1 file | 116 | 352 | 491 (-4) | 352, 387, 426, 491 | busy mid hit; GRIZZLY_Reload_Normal_Start |
| FN57/FN57 Dry Fire | random (2 files, 1 at once) | 9 | 2 | - | 2 | sharp bright click; weap_glock_fire_dry_01, weap_glock_fire_dry_02 |
| Shared/ADS/ADS Pistol Start | random (6 files, 1 at once) | 30 | 155-192 | 222-259 (-11) | 32, 171, 230, 276 | busy bright hit; char_foley_arms_quick_01, char_foley_arms_quick_02, char_foley_arms_quick_03 ... |
| Shared/ADS/ADS Pistol Stop | random (12 files, 2 at once) | 67 | 73-178 | 94-178 (-6) | 129, 175 | busy bright hit; char_foley_aim_clicks_01, char_foley_aim_clicks_02, char_foley_aim_clicks_03 ... |
| Shared/Draw/Draw Pistol V2 | random (18 files, 4 at once) | 24 | 53-86 | 1-168 (-20) | 78 | sharp bright holster; char_foley_gear_shake_short_01, char_foley_gear_shake_short_03, char_foley_gear_shake_short_04 ... |
| Shared/Mag Drops/Pistol Mag Drop | random (6 files, 1 at once) | 5 | 1-141 | 1-145 (-3) | 1, 115, 238 | sharp bright mag; weap_pistol_mag_drop_01, weap_pistol_mag_drop_02, weap_pistol_mag_drop_03 ... |
| Shared/Pistols Shared/Pistol Holster | random (4 files, 1 at once) | 42 | 387-431 | 269-457 (-8) | 269, 387 | sharp mid holster; Universal_Pistol_Holster_01, Universal_Pistol_Holster_02, Universal_Pistol_Holster_03 ... |
| G19 New/1P G19 Fire | random (11 files, 5 at once) | 17 | 1-4 | - | 1 | sharp dull hit; 556_Int_Med1p-003, 556_Int_Med1p-006, Glock-1 ... |
| G19 New/G19 Cloth-CheckDone | fixed, 1 file | 5 | 4 | 164 (-7) | 4, 164 | sharp mid cloth; Cloth-CheckDone |
| G19 New/G19 Cloth-PouchGrab | fixed, 1 file | 41 | 253 | 73 (-17) | 253 | sharp mid cloth; Cloth-PouchGrab |
| G19 New/G19 Cloth-ReloadEnd | fixed, 1 file | 9 | 9 | 401 (-22) | 9 | sharp mid cloth; Cloth-ReloadEnd |
| G19 New/G19 Cloth-ReloadMagStow | fixed, 1 file | 5 | 302 | 227 (-1) | 227, 302, 369 | sharp mid cloth; Cloth-ReloadMagStow |
| G19 New/G19 Cloth-ReloadStart | fixed, 1 file | 5 | 302 | - | 302 | sharp mid cloth; Cloth-ReloadStart |
| G19 New/G19 Dry Mag In | fixed, 1 file | 15 | 230 | 116 (-5) | 4, 116, 167, 230, 285, 547 | busy bright click; DryMagin |
| G19 New/G19 Dry Mag Out | fixed, 1 file | 12 | 2 | 161 (-14) | 2, 161 | sharp bright click; DryMagOut |
| G19 New/G19 Mag Check In | fixed, 2 layers | 25 | 203 | 3-270 (-4) | 3, 149, 203, 270 | busy bright cloth; CheckMagIn, Cloth-CheckB4MagIn |
| G19 New/G19 Mag Check Out | fixed, 2 layers | 20 | 4 | 326 (-10) | 4, 167, 326, 426, 756 | busy bright cloth; CheckMagOut, Cloth-CheckMagOut |
| G19 New/G19 Slide | fixed, 2 layers | 26 | 176 | 32 (-6) | 32, 176 | sharp bright cloth; Cloth-SlideGrab, DrySlideGrab |
| G19 New/G19 Wet Mag In | fixed, 1 file | 9 | 347 | 2 (-11) | 2, 47, 347 | busy bright mag; WetMagInNormal |
| G19 New/G19 Wet Mag Out | fixed, 1 file | 12 | 10 | 305 (-4) | 10, 61, 128, 305 | busy bright mag; WetMagOutNormal |
| G19 New/G19 WetMagInQuick | fixed, 1 file | 25 | 224 | 172 (-18) | 224 | busy bright mag; WetMagInQuick |
| G19 New/G19 WetMagOutQuick | fixed, 1 file | 6 | 3 | 148 (-12) | 3, 148 | sharp bright mag; WetMagOutQuick |
| G19/G19 Dryfire Old | random (6 files, 1 at once) | 5 | 0 | - | 1 | sharp bright click; weap_g19_dryfire_01, weap_g19_dryfire_02, weap_g19_dryfire_03 ... |
