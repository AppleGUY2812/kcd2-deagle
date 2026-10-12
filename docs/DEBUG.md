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
| `deagle_tracers 0` / `1` | glowing tracer on every round off / on |

## Settings worth knowing (`deagle_set <name> <value>`)
| Setting | Default | What it changes |
|---|---|---|
| `flashMode` | `gun` | `gun` = flash on the gun's muzzle (glued), `world` = left where it was fired |
| `muzzleFxScale` | `1` | multiplier on the flash size the cartridge gives (0.5 = half) |
| `slowmo` | `0.1` | game speed while F9 slow motion is on |
| `fireRate` | `mod` | `mod` = each GameBanana mod's own speed, `cs2` = CS2's |
| `cancelKeepsRounds` | `0` | `1` = arcade: a reload cancelled after the old mag is out keeps your rounds |
| `recoil` | `1` | **master kick**: scales every recoil effect together (camera kick, sideways, gun model flip and push). `1.5` = 50 % harder for every gun |
| `physics.kickExponent` | `0.67` | how much smaller guns are lifted toward the Deagle's kick: `1` = pure physics (Glock a third of the Deagle), `0.67` = Glock 0.49x (3.5 deg), `0` = every gun kicks like the Deagle |
| `physics.loudnessSpan` | `0.25` | how much quieter small guns are than the Deagle: `0` = all as loud, `0.25` = Glock 0.83x, `0.5` = 0.69x, `1` = real difference |
| `physics.camPerFlip` | `0.35` | camera kick per unit of muzzle flip (Deagle 7.1 deg a shot, above Ready or Not's .357 at 5-6) |
| `physics.holdRange` | `4.7` | degrees of kick your grip takes back by itself (more = more of each kick returns) |
| `physics.holdMax` | `0.6` | most of one kick that springs back by itself: `0` = it all stays (Ready or Not), `0.95` = nearly all returns |
| `physics.returnRate` | `5` | how fast the held part settles (higher = faster; heavier guns slower) |
| `physics.adsHold` | `1.3` | aiming tightens the grip by this much |
| `physics.riseRate` | `70` | how fast a kick lands |
| `physics.sideSigma` | `0.15` | sideways kick, random share of the climb (same pattern every time per gun) |
| `physics.barrelAim` | `1` | shots fired while the gun is still kicked go where the barrel points (0 = straight) |
| `physics.gunFlipPerFlip` | `0.467` | gun model flip per unit of muzzle flip (Deagle 9.5 deg) |
| `physics.gunBackPerVel` | `0.0113` | gun model push-back per m/s of recoil (Deagle 5.5 cm) |
| `physics.flashPerUnit` | `0.30` | flash size per unit of blast (Deagle 0.63, Glock 0.30) |
| `tracers` | `true` | a tracer on every round (same as `deagle_tracers`) |
| `range` | `250` | metres a round flies before it's dropped |
| `holeSize` | `0.035` | bullet hole size in m for a 9 mm (bigger rounds scale it up) |
| `holeLife` | `300` | seconds a bullet hole stays |
| `hitReactions` | `true` | NPCs flinch / stagger when hit (KCD2's own reactions, needs the plugin) |
| `hitReactType` | `0` | which of KCD2's reaction sets plays (0 = normal hits; 3 = collision, big staggers only) |
| `hitReactMajor` | `50` | health lost from which a hit is a big stagger instead of a flinch |
| `hitReactFlags` | `0x3F` | reaction event flags (plugin bits 0x100 / 0x200 / 0x400 flip direction / body part handling) |
| `armor.punch` | `104` | how hard a 9 mm hits armour (KCD2 stab defense units). Higher = rounds get through plate more often (Glock through a worn cuirass only within ~5 m now; Deagle through most plate) |
| `armor.glanceAngle` | `70` | degrees off square-on past which plate / mail always turn a round |
| `armor.bruise` | `0.1` | share of the wound a round stopped by armour still does (blunt trauma) |
| `wallbang.budget` | `0.1` | metres of wood a 9 mm gets through (others by energy; the Deagle ~0.19) |
| `ricochetKeep` | `0.35` | energy a ricochet keeps |
| `holeExitSize` | `0.09` | exit hole size in m |
| `accuracy.scale` | `1` | CS2 guns: 1 = CS2's own accuracy (standing / crouched / moving / jumping / shots in a row), 0.5 = twice as tight, 0 = no spread |
| `flashFrames` | `2` | frames the muzzle flash is seen (any frame rate); 0 = the particles' own life |
| `impactVolume` | `1` | bullet impact / ricochet / armour sounds |
| `hearing.on` | `true` | NPCs hear your shots and impacts (KCD2's own gun_fired / bullet_hit sounds) |
| `hearing.shot` | `3.2` | how far a 9 mm shot is heard (multiplier; louder guns more) |
| `physics.sizeExponent` | `0.5` | how much harder small bodies are hit (a hare dies to anything, a horse takes ~2.5x the hits) |

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
