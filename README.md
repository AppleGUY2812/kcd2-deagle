# Desert Eagle for Kingdom Come: Deliverance II

A scripted semi-auto .50 AE pistol for KCD2 (Steam 1.5.x, CryEngine) with a first-person viewmodel:
arms, gun and animations converted from GameBanana mod 532396, a Counter-Strike: Source mod
("Insurgency: Sandstorm's DEAGLE on MWII Animations": Insurgency Sandstorm model, Call of Duty MWII animations,
arms and sounds). An optional native plugin places the viewmodel without lag and plays the sounds.

**Private use only.** The model, arms, animations, sounds and muzzle textures are ripped from other games. Don't publish
or share the built mod.

## In game
| Key / command | Does |
|---|---|
| H | draw / holster (`deagle_toggle`) |
| Left click | fire |
| R | reload |
| Y | inspect |
| Right click | aim down sights, off by default (`deagle_ads 1`) |
| B | fire mode (guns whose CS2 counterpart has burst / full auto) |
| `deagle_gun <id>` | which converted gun the pistol is (no id: list) |
| `deagle_give` | adds the pistol |
| `deagle_arms medieval` / `mw` | sleeve and glove look |
| `deagle_motion 0..2` | bob / sway strength (plugin) |
| `deagle_hud 0/1` | ammo HUD |
| `t_Scale 0.1` | slow motion for checking effects (`t_Scale 1` to undo) |

## Build
```
tools/build_vm.sh --audit            # clips -> compile -> stage -> tests -> transition audit -> pak (about 1.5 min)
tools/build_vm.sh --extra --install  # also regenerate the extra clips (ADS, empty variants...) and install
./build.sh --install                 # pak only (script / particles / text changes), then install
native/build.ps1; native/install.ps1 # the plugin (dinput8.dll ASI loader + deagle_native.asi)
```
Install only while the game is closed (the scripts refuse otherwise).

## Test
```
luajit tests/run_flows.lua        # every gameplay flow in a mock KCD2 (0 errors expected)
luajit tests/run_transitions.lua  # clip + blend chosen for every risky animation combination
luajit tests/run_fallback.lua     # without the native plugin
tools/live.sh reload              # running game: re-run the script live (dev console over rcon)
tools/live.sh <snippet.lua>       # running game: run a Lua snippet
```
Video checks: drag an OBS clip onto `Desktop\Make Scrubbable.bat`, then step with Ctrl+Left/Right in MPC-HC.

## Where things are
| Path | What |
|---|---|
| `src/` | mod content, packed into `data/deagle.pak`: particles, viewmodel, clips, and the scripts in `Scripts/Deagle/`, one file per system (see below) |
| `native/` | the C++ plugin (MinHook) and its build/install scripts |
| `inventory/` | the Inventory Tools mod (its own pak + `inventory.asi`, split out on 2026-10-26): weight map, G fast transfer, inventory open speed; see `inventory/README.md` (plugin source `native/src/inventory_main.cpp`) |
| `tools/` | the conversion pipeline (Blender, rc, audits) |
| `tests/` | mock-engine tests |
| `assets/` | build inputs that aren't committed (MWII sounds, the game's Animations.img) |
| `MODLOG.md` | journal: how the engine really behaves, what failed and why. Read before changing anything |
| `TODO.md` | status and next steps |
| `docs/ARMSWAP.md` | Henry's arms + worn clothing on any gun: the reusable process (new game's arms = one bone table) |
| `docs/code_navigation.md` | how Claude finds code here (grep / ast-grep, the ownership map in CLAUDE.md); why no code index |
| `E:\Tools\deagle-work\blend\` | source Blender files: `mwii_vm.blend` (SourceIO import) -> `_filled` -> `_extra` |
| `E:\Tools\deagle-work\downloads\` | the original downloads (GameBanana mod zip, MW19 muzzle pack, SourceIO) |
| `E:\Tools\deagle-work\{vm,animroot,out}` | export work, rc animation root (SkeletonList), compiled image, renders, audit data |
| `E:\Tools\blender-4.3.2-windows-x64`, `E:\Tools\blender-user` | Blender and its add-ons (SourceIO 5.5.4, KCD2 toolkit) |

Scripts (`src/Scripts/Deagle/`): `deagle_main.lua` loads the others in this order and holds the settings (`cfg`), the
shared helpers (`Deagle.U`), input, start-up, the settings commands and the console command table.

| File | System |
|---|---|
| `surfaces.lua` | what each KCD2 surface does when hit: effect, hole and sound columns (`SURFACES`) |
| `physics.lua` | cartridge physics: recoil impulse, muzzle gas, wound energy |
| `effects.lua` | impact effects: `SpawnFx`, per-hit variety, `deagle_fx`, `deagle_fxreload` |
| `sound.lua` | gunshot gain, Ready or Not hit sounds, pain voices, sound cues, the room check |
| `flash.lua` | muzzle flash: particle flash, flash cards, flash light |
| `debug.lua` | zoom, slow motion, test NPC / test wall, round paths, stress test |
| `recoil.lua` | recoil: kick, spray pattern, camera recoil |
| `hits.lua` | hits: body parts from bones, blood, hit reactions, impact marks, `ApplyHit` |
| `npc.lua` | NPC reactions: XP, near misses, AI hit / hearing events |
| `tracers.lua` | tracer trail pieces |
| `shells.lua` | ejected cases |
| `weapon.lua` | firing, ammo, reloads, aiming, inspect, fire modes, CS2 accuracy |
| `viewmodel.lua` | the first-person arms + gun, its animation states, gear hiding, HUD |
| `freecam.lua` | free camera |
| `ballistics.lua` | rounds in flight, ricochets, corners, wallbangs, armour (loads `killcam.lua`) |
| `motion.lua` | weapon movement inputs |
| `guns/` | gun data (`cs2.lua`, `calibers.lua`, `penetration.lua`, `armor.lua`, one file per gun, ...) |

## Pipeline
1. `blender` + SourceIO imports the Source MDL with its sequences -> `mwii_vm.blend`
2. `tools/vm_export.py` fills gun bones a clip leaves unkeyed, restores zeroed Source sections -> `mwii_vm_filled.blend`
3. `tools/vm_extra_anims.py` makes the extra clips: empty idle / draw / holster / inspect, ADS, the empty-shot
   settle, the empty reload ease-in -> `mwii_vm_extra.blend`
4. `tools/vm_export.py` (again, on the extra blend) turns the view +Y, splits the magazine rounds into their own skin,
   exports the character and one DAE per clip
5. `tools/vm_anims.ps1` compiles the clips with the game's rc; `tools/merge_img.py` merges their headers into
   `Animations.img`
6. `tools/vm_tracks_dump.py` + `tools/vm_transition_audit.py` check every transition for jumps

## Importing a GameBanana CS weapon mod
```
tools/gb/gb_import.sh 447086          # mod id (or a local .zip/.rar/.7z), about 30 s
```
Downloads and extracts to `E:\Tools\deagle-work\guns\<slug>`, reads the viewmodel (sequences, fps, sound events,
bones, attachments), the soundscripts and the weapon script, and writes a guessed **gun profile** to
`guns/<slug>.json` (which clip is draw / shoot / reload..., which bones are slide / mag / rounds / muzzle / brass,
magazine size, sound cues, gunshot sounds). Correct anything wrong in that file. A contact sheet of every clip lands
on the Desktop (`Deagle Progress\<slug> clips.png`). Checks: `python3 tests/test_profile_guess.py`.

```
tools/gb/gb_convert.sh glock_17 [--install]   # profile -> in-game gun, about 1.5 min
```
Builds the clips from the profile (Source section gaps repaired, unkeyed bones filled, a settle / ease-in wherever a
clip doesn't start or end on its rest pose, empty idle / draw / holster / inspect generated, clips faster than 30 fps
resampled for rc, magazine-swap timings measured), compiles them, converts the textures, stages everything under
`src/` (`Scripts/Deagle/guns/<slug>.lua` holds the gun's data) and runs the tests (`tests/run_gun.lua <slug>`).
Audit it like the Deagle: `tools/gb/gb_tracks_dump.py` on `<slug>_vm_roles.blend`, then
`tools/vm_transition_audit.py`; `tools/gb/gb_roles_preview.py` + `gb_sheet.py` render the converted clips.
In game: `deagle_gun glock_17` (no argument lists the guns).

Handling comes from **CS2**: each profile names its closest CS2 weapon (`"cs2": "weapon_glock"`, guessed from the
title). `tools/gb/cs2_fetch.sh` downloads CS2's current `weapons.vdata` (SteamDB GameTracking-CS2) and
`python3 tools/gb/cs2_apply.py .` writes `Scripts/Deagle/guns/cs2.lua`; the script takes damage, headshot multiplier,
range falloff, fire rate, recoil and fire modes (semi / burst / auto) from it.

## How a gun plugs in (the contract)
The gameplay script doesn't care where a gun came from, but it needs per-gun knowledge that lives in the animations and
the model, so every importer has to produce it (tools/gb does for GameBanana CS:S guns):
- clips by role (idle, shoot, shoot_empty, reload, reload_empty, inspect(_empty), draw(_empty), holster, burst) with
  their frame rates;
- moments inside them (tools/clip_times.py): old mag out, new mag clicked in (the mag-in sound), gun back up, settled;
- named parts: muzzle and ejection bones, slide (+ parts riding it), both magazines and every modelled round as their
  own attachments;
- sound cues (file + time per clip).
Lua runs the gameplay (input, ammo, reloads, damage, which clip plays); the native plugin does what has to happen on
the final camera every frame (placing the gun, keeping the flash on it), sounds and procedural motion. They talk through
"[DN] ..." log lines and the deagle_native_ok handshake; without the plugin Lua falls back (tests/run_fallback.lua).
A new source (Ready or Not: FModel -> Blender, anim notifies give the moments exactly) needs its own importer front end
filling this contract, not a different gameplay system. Take gun, animations and arms from the same source: animations
are bound to one skeleton and the hands to the gun's grip (mixing sources means re-rigging by hand).
