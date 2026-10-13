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
