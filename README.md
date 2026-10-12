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
| `src/` | mod content, packed into `data/deagle.pak` (script `Scripts/Deagle/deagle_main.lua`, particles, viewmodel, clips) |
| `native/` | the C++ plugin (MinHook) and its build/install scripts |
| `tools/` | the conversion pipeline (Blender, rc, audits) |
| `tests/` | mock-engine tests |
| `assets/` | build inputs that aren't committed (MWII sounds, the game's Animations.img) |
| `MODLOG.md` | journal: how the engine really behaves, what failed and why. Read before changing anything |
| `TODO.md` | status and next steps |
| `E:\Tools\deagle-work\blend\` | source Blender files: `mwii_vm.blend` (SourceIO import) -> `_filled` -> `_extra` |
| `E:\Tools\deagle-work\downloads\` | the original downloads (GameBanana mod zip, MW19 muzzle pack, SourceIO) |
| `E:\Tools\deagle-work\{vm,animroot,out}` | export work, rc animation root (SkeletonList), compiled image, renders, audit data |
| `E:\Tools\blender-4.3.2-windows-x64`, `E:\Tools\blender-user` | Blender and its add-ons (SourceIO 5.5.4, KCD2 toolkit) |

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
