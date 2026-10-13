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
