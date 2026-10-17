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
| F9 | slow motion on / off (0.1x, works holstered). Slower: `deagle_slowmo 0.02` (F9 keeps that speed) |
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
| `deagle_gunfx 0` / `1` | everything at the gun off / on (kept): muzzle flash, smoke, muzzle light, casings, tracers and the gunshot. Only the impacts show and sound (effect, hole, impact sound, ricochets), to watch them on their own. Setting: `gunFx` |
| `deagle_sounds ron` / `original` | Ready or Not's sound engine (default), or the gun mods' own shot and animation sounds (kept) |
| `deagle_env` | where the mod thinks you are (room type + material) and the ray numbers behind it |
| `deagle_flash ron` / `ours` | the muzzle flash on the gun's cards: Ready or Not's own (its textures and effect, baked by tools/ron/flash_bake.py) or ours; sets `flashMode mesh`; kept |
| `deagle_testwall` | a row of real KCD2 objects 4 m ahead, one per surface: stone, plaster, pots, wood door, crate, wicker, metal door, anvil, glass (standing up, facing you), sack, straw target, hay, vegetables, leather (listed left to right on screen). `deagle_testwall clear` removes them; never saved |
| `deagle_organ <name>` | Sniper Elite V2's real organs as KCD2 models, 0.8 m ahead at eye height: `heart`, `lung_left`, `lung_right`, `liver_upper`, `liver_lower`, `kidney_left`, `kidney_right`, `windpipe`, `windpipe_lower`, `eyeball`, `testicle`, `all`; `clear` removes them. Needs the install with the se2 organ assets |
| `deagle_testnpc` | aim at an NPC (within 60 m) and type it: your rounds don't hurt that NPC (no crime), and each hit shows the body part, whether armour stopped it, and the damage it would have done. `deagle_testnpc clear` ends it. (Spawning test NPCs froze the game, so it marks real ones instead) |
| `deagle_paths 1` / `0` / `clear` | draw every round's path, kept until cleared: yellow flight, red inside a wall / body, cyan after a ricochet / wallbang, crosses at hits (white world, red body) |
| `deagle_slowmo <speed>` | slow motion at that speed, 0.01 (slowest) to 1, e.g. `0.02` for the flash; F9 uses it from then on (kept). `deagle_slowmo off` = normal; no value = on / off like F9 |
| `deagle_zoom <n>` | debug zoom (F2 steps x4 / x10 / off) |
| `\` or `deagle_freecam` | free camera: time keeps running. **WASD** fly, **Space** up, **Left Ctrl** down, **Shift** 5x faster; Henry doesn't walk. The **mouse turns the camera** and Henry's aim stays put; **hold right mouse** to aim Henry instead (the camera stays). A red line shows where a shot goes; left click fires. **Scroll** = faster / slower (or `deagle_freecam speed 10`). Turns itself off on a save load. (testing) |

## Settings worth knowing (`deagle_set <name> <value>`)
Type the whole line: `deagle_set hearing.on 0`. A setting name alone (`hearing.on 0`) is an unknown command.
On / off settings take `1` / `0` (or `true` / `false`, `on` / `off`); list settings take numbers apart by commas.
| Setting | Default | What it changes |
|---|---|---|
| `flashMode` | `gun` | `gun` = a flash spawned per shot on the muzzle (shows a frame or two late); `world` = left where it was fired; `live` = experimental, don't use: its hide doesn't work and after a save load it kept burning in the air (the "fireball") |
| `flashMode` `mesh` | (testing) | flash cards on the gun itself, shown on the exact frame you fire, one shape per shot, turned a random quarter turn round the barrel (`deagle_set flashMesh.roll 0`: never turned). Since 2026-10-14 the cards are skins of the gun (tools/flash_skin.sh): as models on the muzzle bone every card drew the same one |
| `flashMesh.look` | `ours` | the flash cards' look: `ours`, or `ron` = Ready or Not's own pistol flash (its textures and its effect, run and baked onto cards: the Grizzly MK V's for the Deagle, the G19's for the Glock); `deagle_flash ron` / `deagle_flash ours` |
| `flashMesh.roll` | `true` | our flash look turned a random quarter turn round the barrel on each shot (baked into the cards); `0` = never turned. Changes at once |
| `flashRon.mtl` | `deagle_vm_flash_ron` | Ready or Not flash brightness: `deagle_vm_flash_ron_dim` (x1/3) / `deagle_vm_flash_ron` / `_hot` (x3) / `_x10` / `_x30`: brighter turns its red body orange and its core white, as in Ready or Not |
| `flashRon.time` | `0.095` | how long the Ready or Not flash runs, game time: its fireball 0.035-0.04 s, its sparks to 0.095 s |
| `flashMesh.time` | `0.035` | how long the mesh flash lasts, in seconds of game time (Ready or Not 0.035-0.04): 2 frames at 60 fps, 1 under 40 fps, about 10x longer in F9 slow motion |
| `flashMesh.mtl` | `deagle_vm_flash` | mesh flash brightness: `deagle_vm_flash_dim` / `deagle_vm_flash` / `deagle_vm_flash_hot` |
| `flashMesh.light.mode` | `ron` | the mesh flash's light: `ron` = Ready or Not's two flash lights (red-orange as Ready or Not shows them on screen, from the shot's own frame for 0.04 s, falling off like real light with no visible edge, not lighting the fog); `particle` = the old one (a frame late, a hard edge, lit the fog) |
| `flashMesh.light.intensity` | `0.5` | brightness of the `ron` flash light on the Deagle; smaller cartridges get less by their muzzle blast (the Glock 0.48 of it); its second light is 0.43 of it |
| `flashMesh.light.bulb` | `0.3` | size in m of the `ron` flash light's source: bigger = softer near the gun |
| `muzzleFx` / `muzzleFxLarge` | `deagle.muzzle` | particle flash look: try `deagle.muzzle_b4`, `_b4s15`, `_b10s2` (and `deagle.muzzle_large_b4`... for the Deagle) |
| `muzzleFxScale` | `1` | multiplier on the flash size the cartridge gives (0.5 = half) |
| `impactSpawn` | `plugin` | how bullet-hit and blood effects are spawned: `plugin` = drawn from their first frame (held 2 s); `lua` = the old way (KCD2 doesn't draw them for ~0.3 s, so short sparks and flashes never show) |
| `impactLook` | `ron` | water and metal hit looks: `ron` = Ready or Not's; `new` = the test looks (water: a fast jet whose spray splashes where it lands, KCD2's own water materials; metal: a fast flat spark fan that differs every hit) |
| `impactLight.on` / `.max` / `.time` / `.intensity` / `.radius` | `true` / `6` / `0.05` / `0.3` / `15` | light from sparky hits (metal, ricochets, stone; not soil, wood or water): a pool of `max` world lights, each burning `time` s; a hit while all burn gets none (logged). Brighter: `deagle_set impactLight.intensity 0.6`; off: `deagle_set impactLight.on 0` |
| `fxBudget.on` / `.rate` / `.burst` / `.near` | `true` / `30` / `15` / `0.5` | impact and blood effects under heavy fire (a CIWS): a hit within `near` m of a fresh effect of the same kind gets none, and after a burst of `burst` at most `rate` a second spawn (skips logged). A/B: `deagle_set fxBudget.on 0` |
| `soundBudget.on`, `soundBudget.<hit / ricochet / casing>.rate` / `.burst` / `.near` / `.reserve`, `soundBudget.closeM` | `true`; hit 30 / 15 / 0.5 / 5, ricochet 10 / 5 / 1 / 2, casing 12 / 6 / 0.3 / 2; 10 | sounds under heavy fire (a CIWS), per category: a sound within `near` m of a fresh one of the same kind plays none, and after a burst of `burst` at most `rate` a second play; sounds farther than `closeM` m from you leave `reserve` for close ones. Your own gunshot, handling sounds and hits on people always play. Skips logged. 150-290 hit sounds a second took the game to 8 fps. A/B: `deagle_set soundBudget.on 0` |
| `slowmo` | `0.1` | game speed while F9 slow motion is on (0.01-1; changes at once if it's on) |
| `debugShotLog` | `false` | every shot is traced in kcd.log: Henry's eye and aim, the free camera, the round's place each frame, and each hit's surface, angle and outcome (ricochet / impact), so Claude can check what a shot did |
| `impactCap.perRound` | `2` | impact effects (effect, decal, sound) a round makes at the world: its first hit and its first skip; damage, penetration and ricochets are never capped (the user's 75 rounds/s stress test did 2-6 impacts a round) |
| `impactCap.perFrame` | `6` | impact effects all rounds together make in one frame |
| `impactCap.mergeDist` / `.mergeTime` | `0.3` / `0.1` | metres and seconds: a second effect on the same surface this close to the last one looks the same and is skipped |
| `impactCap.maxHops` | `4` | hits a round survives (ricochets and wallbangs chain) |
| `impactCap.maxRounds` | `64` | rounds in flight; past this the oldest is dropped (steady state at 75 rounds/s is about 43: a round lives 0.57 s at 250 m) |
| `killCam.on` | `true` | Sniper Elite V2's kill cam: a long shot that will kill a living NPC slows the game to 3 %, the camera rides behind the bullet (1.1 m easing to 2 m, field of view 10 -> 30 degrees), the hit and the way out play at 10 %, then normal speed (numbers read from its data: docs/se2_killcam.md) |
| `killCam.organs` | `true` | at the hit the NPC's body is hidden and its real organs (Sniper Elite V2's models: heart, lungs, liver, kidneys, windpipe; the eyeballs on a head hit) are shown in its place until the cam ends, then the body comes back. Needs the organ assets in the install |
| `killCam.minDist` | `40` | metres the round must have flown for a kill cam (nearer shots get none) |
| `killCam.killOnly` | `true` | only a round whose damage reaches the NPC's health, or that hits at head height |
| `killCam.speed.pre` / `.internal` / `.post` | `0.03` / `0.03` / `0.1` | game speed before the hit, inside the body, after the way out (TimeScale.PreHit / Internal / PostExit) |
| `killCam.lead` / `.after` / `.internalTime` | `1` / `1` / `0` | seconds of camera time before the impact, after the way out, and inside the body |
| `killCam.offset.1` / `.2` | `-1.1` / `-2` | metres behind the bullet at the start / end (negative = behind) |
| `killCam.fov.1` / `.2` | `10` / `30` | field of view in degrees at the start / end |
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
| `tracer.spawn` | `restart` | `restart`: each round's tracer is one effect the plugin moves and restarts every frame (needs the plugin from 2026-10-15; an older one says so in the log); `plugin`: a new effect every frame per round (the old way: 140 -> 70 fps spraying, and a DX12 crash) |
| `tracer.mode` | `trail` | `trail`: the round's real path drawn as it flies, muzzle to hit (ricochets and wallbangs too); `flying`: the old self-flying particle |
| `tracer.scaled` | `false` | the last piece of a trail shortened to end exactly on the hit (only once the scale-length check passes in game) |
| `range` | `250` | metres a round flies before it's dropped |
| `holeSize` | `0.035` | bullet hole size in m for a 9 mm (bigger rounds scale it up) |
| `holeLife` | `300` | seconds a bullet hole stays |
| `holeBudget.on` | `true` | bullet hole budget: no hole within `holeBudget.near` (0.1 m) of one made in the last `holeBudget.nearTime` (0.5 s) on the same face, then at most `holeBudget.rate` (30) a second after a burst of `holeBudget.burst` (15); skips are logged. `0` = every hit a hole (at 75 rounds/s that took the frame to 250 ms) |
| `hitReactions` | `true` | NPCs flinch / stagger when hit (KCD2's own reactions, needs the plugin) |
| `hitReactType` | `0` | which of KCD2's reaction sets plays (0 = normal hits; 3 = collision, big staggers only) |
| `hitReactMajor` | `50` | health lost from which a hit is a big stagger instead of a flinch |
| `hitReactFlags` | `0x3F` | reaction event flags (plugin bits 0x100 / 0x200 / 0x400 flip direction / body part handling); with `hitAI missile` its AI bit 0x20 is left off |
| `hitAI` | `missile` | who a shot NPC thinks hit it: `missile` = like your arrows, it knows it was you only if it had seen you, otherwise it searches toward the shot (witnesses react either way); `direct` = it always knows (the old way) |
| `armor.punch` | `104` | how hard a 9 mm hits armour (KCD2 stab defense units). Higher = rounds get through plate more often (Glock through a worn cuirass only within ~5 m now; Deagle through most plate) |
| `armor.critScale` | `1` | plate / mail critical angles (plate 15, mail 6 deg off their face) x this: higher = rounds skip off armour more often (separate from the world's) |
| `armor.splashAngle` | `20` | a round plate stops flattens and its fragments spray along the plate this many degrees off its face (no bounce back) |
| `armor.bruise` | `0.1` | share of the wound a round stopped by armour still does (blunt trauma) |
| `wallbang.budget` | `0.1` | metres of wood a 9 mm gets through (others by energy; the Deagle ~0.19) |
| `ricochetKeep` | `0.35` | energy a ricochet keeps off plaster, glass and ground (no study found). Measured ones are fixed: steel 75 %, stone, brick and tile 60 %, wood 40 %, water 10 % |
| `ricochet.critScale` | `1` | every world material's critical angle (steel and sheet steel 15, stone 12, wood 11-16, glass 16, roads / packed ground 14, water 7, forest floor / grass 5 deg off the surface) x this: higher = more ricochets |
| `ricochet.width` | `1` | how soft the cut-off is: at the critical angle half skip, 3 deg flatter ~95 %, 3 deg steeper ~5 % |
| `ricochet.volume` | `2` | ricochet whine loudness x impactVolume, for a round at full speed; a slower round is quieter (amplitude ~ the square root of its energy: a spent one at 54 m/s 18 dB less). Off hard surfaces only (steel, stone, brick, tile); water, ground, wood, plaster and glass skips make only their own hit sound |
| `ricochet.fxScale` | `1.5` | ricochet sparks size |
| `deflect.maxDeg` | `12` | most a round is bent when it cuts through a corner / edge (it tumbles afterwards) |
| `tumble.pen` | `0.5` | how far a tumbling round (after a ricochet or a corner) still gets into walls / armour |
| `holeExitSize` | `0.09` | exit hole size in m |
| `accuracy.scale` | `1` | CS2 guns: 1 = CS2's own accuracy (standing / crouched / moving / jumping / shots in a row), 0.5 = twice as tight, 0 = no spread |
| `impactVolume` | `2` | bullet impact / ricochet / armour sounds x Ready or Not's own level (it mixes them ~9 dB under the shot) |
| `impactNear` | `3` | m within which impacts play at full level; beyond it -6 dB per doubling of the distance, like real sound (plugin; RoN's own fade silenced them by 12 m) |
| `vm.cueLead` | `0.045` | s every Ready or Not handling sound (reload, inspect, draw) starts early for the sound engine's output delay: raise it if they still land late |
| `vm.dryDb` | `-22` | empty click loudness in dB of Ready or Not's mix (the shot is -3; RoN's own click -40 / -47) |
| `hearing.on` | `true` | NPCs hear your shots and impacts (KCD2's own gun_fired / bullet_hit sounds) |
| `hearing.shot` | `1` | how far a 9 mm shot is heard: KCD2's own guns use 1 (louder guns more) |
| `nearMiss` | `2.5` | metres: a round flying this close past a living NPC gives it KCD2's near-miss reaction for an arrow (knows it's you if it saw you, else searches); 0 = off |
| `physics.sizeExponent` | `0.5` | how much harder small bodies are hit (a hare dies to anything, a horse takes ~2.5x the hits) |
| `physics.skullJPerMm2` | `0.4` | a round that reaches the head with this many J per mm² of its face (bone perforation, Sellier & Kneubuehl: 9 mm 26 J, .50 AE 51 J) kills; a helmet that stops it, or padding that drains it below that, leaves a wound |
| `gore.ragdollOnKill` | `true` | a killing round drops the body at once instead of after KCD2's death animation |
| `gore.killPush` | `1` | how hard a kill shoves the body (x the round's real momentum x punch) |
| `eject.speed` | `2.8,4.2` | casing throw speed in m/s (a random pick between the two); standing they land 1.7-2.7 m to the right, plus your own movement |
| `eject.up` | `20,40` | degrees above the gun's right side a casing leaves at (higher = taller arc, closer landing) |
| `eject.back` | `5,25` | degrees a casing goes back toward you |
| `eject.spin` | `100,200` | end-over-end spin in rad/s (needs the plugin's `casespin`: the game caps spin at 20 rad/s without it) |
| `eject.spinCap` | `300` | spin cap the plugin sets for a casing's flight, back to the game's 20 at its first bounce |
| `caseBounce.volume` | `1` | shell clinks x Ready or Not's own level |

Examples: `deagle_set physics.camPerFlip 0.45` (bigger kick for every gun), `deagle_set flashMode world`,
`deagle_set muzzleFxScale 0.5`.

## Reloads (how they work)
- Ammo goes in at the magazine's click sound; you can fire once the gun is back up (a shot pressed earlier waits).
- A shot while the old magazine is still in the gun cancels the reload; your rounds stay.
- A shot after the old magazine is out fires the round in the chamber; the gun has no magazine until you reload.
- An empty gun keeps reloading when you pull the trigger.
- A round in the chamber stays: reloading with rounds left gives 7+1 (Deagle) / 17+1 (Glock).

## Checking effects
- F9 slow motion, then shoot: flash, smoke, casings, light at 1/10 speed. For the flash go slower: `deagle_slowmo 0.02` (1/50) or `0.01` (1/100).
- `deagle_fx <effect>` spawns a particle effect 3 m ahead (e.g. `deagle_fx deagle.muzzle`).
- `deagle_stress [rounds a second] [sky | aim]`: frame-rate test. Look at the sky (it only fires while you do), type it,
  keep looking up for 21 s until "stress test done". Infinite ammo, 10 rounds a second by default (`deagle_stress 75`
  is a CIWS); `aim` fires where you look with no recoil, impacts included, and holds fire while you look at a person or an animal.
  5 s with tracers, a pause, 5 s without. A third word `ab` (`deagle_stress 75 aim ab`, 30 s) fires with everything,
  then with the impact effects off, the holes off, the hit sounds off: which part of a hit costs. A third word `shot`
  (`deagle_stress 250 sky shot`, ~45 s) does the same for the shot itself: everything, then no tracers, no flash, no
  casings, no gunshot sound, no AI hearing, no gun animation, then the bare rounds, 3 s each with a rest between. A
  fire phase that stays over 150 ms for a second ends early (a `STRESS cliff:` line) and the rest waits until frames
  are back near idle (the run stops if they aren't after 10 s). A fourth word picks the parts instead
  (`deagle_stress 250 sky shot casebody,portsmoke`): everything, then without each one. Parts: `tracers`, `flash`,
  `casings`, `casebody` (the casing's physics body, its port smoke stays), `portsmoke` (the smoke, the body stays),
  `gunsound`, `hearing`, `anim`, `shot` (all but the rounds). `deagle_stress stop`
  ends a run at once; any other run stops itself when frames stay over 150 ms for a second. Each `STRESS` line ends
  with the shots fired in it.
- `deagle_lua Deagle:Stress('spawn <name> <per second>')`: one system's cost on its own. A registered spawner runs N
  times a second at the spot you look at (10 m ahead if nothing), 10 s, no gun, same `STRESS` lines. `hole` = bullet
  holes as the game makes them (no budget); `fx_<effect>` = one impact effect raw, no budget, light or extra rays
  (`fx_metal`, `fx_soil`, `fx_stone`, `fx_water`, `fx_water_v3`, `fx_metal_v2`, ...); `light` = the impact light pool; `hitsound` = a stone hit sound with no budget, `hitsound_budget` = the same through the sound budget, `plate` = the steel plate file, `gunshot3p` = the gun's 3D shot there (sound.lua; the plugin log's `ron: update` line gives the microseconds per sound start); a wrong name lists the registered ones. Owners add theirs with
  `Deagle.StressSpawners.<name> = function(pos, normal, i) ... end`. kcd.log gets a
  `STRESS` line every 0.5 s: frame time split into our Lua and the engine, rounds in the air, tracer commands sent;
  after each phase a `STRESS ==` line: frame time p50 / p95 / p99 / max and the mod's Lua per frame against the budget
  (1.0 ms average, 2.0 ms p99, docs/PERFORMANCE.md): PASS or OVER.
- Recording: OBS into `Desktop\Deagle Progress\test records`; Claude reads the clips frame by frame.

## Logs (for Claude)
- Game log: `E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log` (`[Deagle]` lines).
- Plugin log: `...\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\deagle_native.log`.
- `deagle_log_actions` logs every key action the mod receives; `deagle_native_test` pings the plugin with a shot sound.

## Developer only
`deagle_dev_reload` (re-run the script from the dev folder; your kept settings are set again, as at a load), `deagle_lua <code>` (run Lua), `deagle_vm_respawn`
(rebuild the viewmodel), `deagle_input` (reload key bindings), `deagle_fxreload [library]` (the plugin re-reads
`Libs/Particles/<library>.xml`, default `deagle_impacts`, from the dev folder into the running game: impact effects tuned
without a restart; within 3 s of a shot it waits, in game time, and then reloads). Claude uses these through the dev console link
(`tools/live.sh`); they're not needed for play.
