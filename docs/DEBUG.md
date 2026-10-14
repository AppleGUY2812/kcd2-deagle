# Deagle mod: keys, console commands and debug tools

Open the console with the key under Esc (`~`). Settings you change (`deagle_set`, `deagle_tracers`, `deagle_hud`,
`deagle_ads`, `deagle_arms`, `deagle_motion`, `deagle_paths 1/0`, `deagle_gun`) are kept: they come back by themselves
after a restart or a save load. `deagle_reset` puts everything back as shipped. (The plugin keeps them in
`Bin\Win64MasterMasterSteamPGO\deagle_settings.txt`; installing the mod doesn't touch it.)

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
| F2 (or mouse button 4) | debug zoom: x4, x10, off (works holstered; you can shoot; the gun is hidden while zoomed). `deagle_zoom 6` for any zoom. (KCD2 uses F10) |

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
| `deagle_mparam <key> <value>` | weapon-movement tunable, kept until restart (`src/Scripts/Deagle/motion.lua`, `native/src/motion.h`). Keys (defaults = RoN G19; the Deagle overrides drag_aim_loc 0.875, drag_strafe_loc 0.05, move_rot_p 35, hang_k 80, land 1.2): `drag_aim_rot` 0.6 / `drag_aim_loc` 0.4 (look sway rotation / shift), `drag_strafe_rot` 0.4 / `drag_strafe_loc` 0.2 (strafe roll / shift), `move_scale_x/y/z` 10/10/5 and `move_rot_p` 60 (move inertia), `mesh_interp` 55 (sway follow speed), `hang_k` 100 / `hang_c` 8 / `hang_ads` 0.3 (the soft hang spring), `sprint_ron` 1 (0 = the old sprint pose), `sprint_x/y/z` m and `sprint_pitch/roll/yaw` deg (pose), `jump` / `land` / `crouch` / `dodge` / `bob` 1 (strengths), `test_roll` 0 (deg, a constant roll for the sign check) |
| `deagle_tracers 0` / `1` | glowing tracer on every round off / on |
| `deagle_reset` | every setting back as shipped, the kept ones forgotten |
| `deagle_shotsound 0` / `1` | gunshot sound off / on (kept). Impacts, ricochets and casings still play: use it to hear impacts on their own |
| `deagle_sounds ron` / `original` | Ready or Not's sound engine (default), or the gun mods' own shot and animation sounds (kept) |
| `deagle_env` | where the mod thinks you are (room type + material) and the ray numbers behind it |
| `deagle_testwall` | a row of real KCD2 objects 4 m ahead, one per surface: stone, plaster, pots, wood door, crate, wicker, metal door, anvil, glass, sack, straw target, hay, vegetables, leather (listed left to right on screen). `deagle_testwall clear` removes them; never saved |
| `deagle_testnpc` | aim at an NPC (within 60 m) and type it: your rounds don't hurt that NPC (no crime), and each hit shows the body part, whether armour stopped it, and the damage it would have done. `deagle_testnpc clear` ends it. (Spawning test NPCs froze the game, so it marks real ones instead) |
| `deagle_paths 1` / `0` / `clear` | draw every round's path, kept until cleared: yellow flight, red inside a wall / body, cyan after a ricochet / wallbang, crosses at hits (white world, red body) |
| `deagle_zoom <n>` | debug zoom (F2 steps x4 / x10 / off) |
| `\` or `deagle_freecam` | free camera: time keeps running. **WASD** fly, **Space** up, **Left Ctrl** down, **Shift** 5x faster; Henry doesn't walk. The **mouse turns the camera** and Henry's aim stays put; **hold right mouse** to aim Henry instead (the camera stays). A red line shows where a shot goes; left click fires. **Scroll** = faster / slower (or `deagle_freecam speed 10`). Turns itself off on a save load. (testing) |

## Settings worth knowing (`deagle_set <name> <value>`)
Type the whole line: `deagle_set hearing.on 0`. A setting name alone (`hearing.on 0`) is an unknown command.
On / off settings take `1` / `0` (or `true` / `false`, `on` / `off`); list settings take numbers apart by commas.
| Setting | Default | What it changes |
|---|---|---|
| `flashMode` | `gun` | `gun` = a flash spawned per shot on the muzzle (shows a frame or two late); `world` = left where it was fired; `live` = experimental, don't use: its hide doesn't work and after a save load it kept burning in the air (the "fireball") |
| `flashMode` `mesh` | (testing) | flash cards on the gun itself, shown on the exact frame you fire, a different shape on frame 2, random roll |
| `flashMesh.mtl` | `deagle_vm_flash` | mesh flash brightness: `deagle_vm_flash_dim` / `deagle_vm_flash` / `deagle_vm_flash_hot` |
| `flashMesh.light.mode` | `particle` | the mesh flash's light: `particle` (a frame late) or `entity` (a light on the gun, testing) |
| `muzzleFx` / `muzzleFxLarge` | `deagle.muzzle` | particle flash look: try `deagle.muzzle_b4`, `_b4s15`, `_b10s2` (and `deagle.muzzle_large_b4`... for the Deagle) |
| `muzzleFxScale` | `1` | multiplier on the flash size the cartridge gives (0.5 = half) |
| `slowmo` | `0.1` | game speed while F9 slow motion is on |
| `fireRate` | `mod` | `mod` = each GameBanana mod's own speed, `cs2` = CS2's |
| `cancelKeepsRounds` | `false` | `1` = arcade: a reload cancelled after the old mag is out keeps your rounds |
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
| `tracer.fx` | `deagle_tracers.trail_040` | tracer streak length: how long each piece glows. `deagle_tracers.trail_025` (shortest, ~1 frame), `_040`, `_060`, `_080`, `deagle_tracers.trail` (0.12 s, long laser) |
| `tracer.mode` | `trail` | `trail`: the round's real path drawn as it flies, muzzle to hit (ricochets and wallbangs too); `flying`: the old self-flying particle |
| `tracer.scaled` | `false` | the last piece of a trail shortened to end exactly on the hit (only once the scale-length check passes in game) |
| `range` | `250` | metres a round flies before it's dropped |
| `holeSize` | `0.035` | bullet hole size in m for a 9 mm (bigger rounds scale it up) |
| `holeLife` | `300` | seconds a bullet hole stays |
| `hitReactions` | `true` | NPCs flinch / stagger when hit (KCD2's own reactions, needs the plugin) |
| `hitReactType` | `0` | which of KCD2's reaction sets plays (0 = normal hits; 3 = collision, big staggers only) |
| `hitReactMajor` | `50` | health lost from which a hit is a big stagger instead of a flinch |
| `hitReactFlags` | `0x3F` | reaction event flags (plugin bits 0x100 / 0x200 / 0x400 flip direction / body part handling) |
| `armor.punch` | `104` | how hard a 9 mm hits armour (KCD2 stab defense units). Higher = rounds get through plate more often (Glock through a worn cuirass only within ~5 m now; Deagle through most plate) |
| `armor.critScale` | `1` | plate / mail critical angles (plate 7, mail 6 deg off their face) x this: higher = rounds skip off armour more often (separate from the world's) |
| `armor.splashAngle` | `20` | a round plate stops flattens and its fragments spray along the plate this many degrees off its face (no bounce back) |
| `armor.bruise` | `0.1` | share of the wound a round stopped by armour still does (blunt trauma) |
| `wallbang.budget` | `0.1` | metres of wood a 9 mm gets through (others by energy; the Deagle ~0.19) |
| `ricochetKeep` | `0.35` | energy a ricochet keeps |
| `ricochet.critScale` | `1` | every world material's critical angle (stone 12, wood 11-16, steel 6, water 7 deg off the surface) x this: higher = more ricochets |
| `ricochet.width` | `1` | how soft the cut-off is: at the critical angle half skip, 3 deg flatter ~95 %, 3 deg steeper ~5 % |
| `ricochet.volume` | `2` | ricochet whine loudness x impactVolume |
| `ricochet.fxScale` | `1.5` | ricochet sparks size |
| `deflect.maxDeg` | `12` | most a round is bent when it cuts through a corner / edge (it tumbles afterwards) |
| `tumble.pen` | `0.5` | how far a tumbling round (after a ricochet or a corner) still gets into walls / armour |
| `holeExitSize` | `0.09` | exit hole size in m |
| `accuracy.scale` | `1` | CS2 guns: 1 = CS2's own accuracy (standing / crouched / moving / jumping / shots in a row), 0.5 = twice as tight, 0 = no spread |
| `impactVolume` | `1` | bullet impact / ricochet / armour sounds |
| `hearing.on` | `true` | NPCs hear your shots and impacts (KCD2's own gun_fired / bullet_hit sounds) |
| `hearing.shot` | `3.2` | how far a 9 mm shot is heard (multiplier; louder guns more) |
| `physics.sizeExponent` | `0.5` | how much harder small bodies are hit (a hare dies to anything, a horse takes ~2.5x the hits) |
| `gore.ragdollOnKill` | `true` | a killing round drops the body at once instead of after KCD2's death animation |
| `gore.killPush` | `1` | how hard a kill shoves the body (x the round's real momentum x punch) |

Examples: `deagle_set physics.camPerFlip 0.45` (bigger kick for every gun), `deagle_set flashMode world`,
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
