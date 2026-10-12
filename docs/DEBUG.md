# Deagle mod: keys, console commands and debug tools

Open the console with the key under Esc (`~`). Settings changed with `deagle_set` last until the game restarts.

## Keys (gun drawn unless noted)
| Key | What it does |
|---|---|
| H | draw / holster (works holstered) |
| Left mouse | fire (hold for full auto) |
| Right mouse | aim down sights, off by default (`deagle_ads 1` turns it on) |
| R | reload (a shot before the new mag clicks cancels it, see "Reloads") |
| Y | inspect |
| B | fire mode (semi / burst / auto, on guns that have them) |
| F9 | slow motion on / off (0.1x, works holstered) |

## Everyday commands
| Command | What it does |
|---|---|
| `deagle_gun` | list the guns |
| `deagle_gun glock_17` | switch to the Glock (`deagle_gun deagle` back) |
| `deagle_give` | give the Desert Eagle item (+ 49 cartridges) |
| `deagle_physics` | what the current gun's cartridge does: bullet speed / energy, recoil push, flip, camera / gun kick, flash size |
| `deagle_status` | hooks, input, ammo, plugin state |
| `deagle_cfg` | print every setting and its value |
| `deagle_hud 0` / `1` | ammo HUD off / on |
| `deagle_arms medieval` / `mw` | sleeve and glove look |
| `deagle_ads 1` / `0` | aim down sights on / off |
| `deagle_motion 0`..`2` | gun bob / sway / sprint pose strength (0 = off, 1 = normal) |

## Settings worth knowing (`deagle_set <name> <value>`)
| Setting | Default | What it changes |
|---|---|---|
| `flashMode` | `entity` | `entity` = flash glued to the muzzle, `world` = left where it was fired |
| `muzzleFxScale` | `1` | multiplier on the flash size the cartridge gives (0.5 = half) |
| `flashDebug` | `0` | `1` = log every flash load into the game log |
| `slowmo` | `0.1` | game speed while F9 slow motion is on |
| `fireRate` | `mod` | `mod` = each GameBanana mod's own speed, `cs2` = CS2's |
| `cancelKeepsRounds` | `0` | `1` = arcade: a reload cancelled after the old mag is out keeps your rounds |
| `recoil` | `1` | multiplier on all recoil |
| `physics.camPerFlip` | `0.217` | camera kick per unit of muzzle flip (all guns in proportion) |
| `physics.returnPct` | `0.65` | share of each kick that settles back by itself |
| `physics.returnRate` | `5` | how fast that share settles (higher = faster) |
| `physics.riseRate` | `70` | how fast a kick lands |
| `physics.sidePct` | `0.25` | sideways kick, +- this share of the climb |
| `physics.gunFlipPerFlip` | `0.29` | gun model flip per unit of muzzle flip |
| `physics.gunBackPerVel` | `0.007` | gun model push-back per m/s of recoil |
| `physics.flashPerUnit` | `0.476` | flash size per unit of powder |

Examples: `deagle_set physics.camPerFlip 0.3` (bigger kick for every gun), `deagle_set flashMode world`,
`deagle_set muzzleFxScale 0.5`.

## Reloads (how they work)
- Ammo goes in at the magazine's click sound; you can fire once the gun is back up (a shot pressed earlier waits).
- A shot while the old magazine is still in the gun cancels the reload; your rounds stay.
- A shot after the old magazine is out fires the round in the chamber; the gun has no magazine until you reload.
- An empty gun keeps reloading when you pull the trigger.
- A round in the chamber stays: reloading with rounds left gives 7+1 (Deagle) / 17+1 (Glock).

## Checking effects
- F9 slow motion, then shoot: flash, smoke, casings, light at 1/10 speed.
- `deagle_fx <effect>` spawns a particle effect 3 m ahead (e.g. `deagle_fx deagle.muzzle`).
- Recording: OBS into `Desktop\Deagle Progress\test records`; Claude reads the clips frame by frame.

## Logs (for Claude)
- Game log: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log` (`[Deagle]` lines).
- Plugin log: `...\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\deagle_native.log`.
- `deagle_log_actions` logs every key action the mod receives; `deagle_native_test` pings the plugin with a shot sound.

## Developer only
`deagle_dev_reload` (re-run the script from the dev folder), `deagle_lua <code>` (run Lua), `deagle_vm_respawn`
(rebuild the viewmodel), `deagle_input` (reload key bindings). Claude uses these through the dev console link
(`tools/live.sh`); they're not needed for play.
