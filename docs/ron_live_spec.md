# Ready or Not's sound engine, live in KCD2

The Deagle's gun sounds are Ready or Not's own FMOD Studio 2.03.10 events, mixed live by RoN's own engine (its buses,
limiters and room snapshots). Nothing is pre-rendered. Research: `docs/ron_fmod_events.md`.

## How it runs
- `tools/sound/make_ron_engine.py` copies RoN's `fmod.dll` / `fmodstudio.dll` as `fmor.dll` / `fmorstudio.dll` and
  patches the studio DLL's one import of `fmod.dll` to `fmor.dll`. KCD2 already loads its own 2.02.21 `fmod.dll`, and
  Windows binds an import to an already-loaded module of the same name. It also copies the banks (Master,
  Master.strings, Weapons, Interactions, UI, SWAT, Global Amb, about 100 MB) to `assets/build/ron`. `build.sh` ships
  them as `Mods/deagle/ron`.
- `native/src/ron.cpp` loads them on the first Ready or Not command (bank load about 150 ms), starts
  `event:/Levels/Reverbs/ReverbMaster` for good, and updates the engine and listener every camera step. Its final
  fader is 0.5 x the mod's volume, the level the old renders were made at.
- `native/test/ron_coexist.exe` (outside the game) loads KCD2's 2.02 pair and a 2.02 core system first, then RoN's
  engine, and renders a shot. Measured: both coexist, and ReverbMaster + `ReverbHALL=1` matches the `Reverb_IN_Hall01`
  snapshot.

## Plugin commands
- `ron <vol> <event>|<name=value ...>`: play at the listener.
- `ron3 <vol> <x> <y> <z> <event>|<...>`: play at a world point.
- `rong <global> <value>`: set a global.
- `ronload <event>`: load an event's audio ahead of time.
- Back to Lua: `deagle_ron_state 1|0`. A failure shows a message; there is no fallback sound.

## Room sound
- RoN's ReverbMaster holds 12 snapshots: 8 rooms (BATHROOM SMALL CORRIDOR MID HALL STREET FOREST CAVE) and 4 materials
  (CONCRETE WOOD METAL FABRIC). Each snapshot's strength follows the global of the same name (`ReverbSMALL`,
  `ReverbMaterialWOOD`, ...), and partial values blend.
- The Lua classifier (`Deagle:EnvClassify`, rays every 0.25 s) picks one of these classes:
  - `in_<small|mid|hall|corridor>[_<stone|wood|metal|fabric>]`
  - `in_cave[_stone]`
  - `out_street`, `out_forest`, `out_covered` (MID + CONCRETE)
- `Deagle:EnvMix` turns the class into the globals: its room and material at 1, every other at 0.
- `Deagle:EnvFade` moves the globals there over `cfg.env.fade` s (RoN fades them across doorways).
- The fire event gets `IsOutside` 0/1 from the class.

## Still to do
- Handling events (draw, holster, reloads, ammo check, ADS start/stop, dry fire, mag drop) at RoN's offsets.
- Casings: `9mmShell` with Surface on each bounce.
- Bullet impacts through `Bullet Impact Surfaces` with the KCD2 mat_* -> Surface map.
- A RoN event for the Glock.
