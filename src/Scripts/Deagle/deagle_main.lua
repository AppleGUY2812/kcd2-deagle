-- Desert Eagle for KCD2: a scripted semi-auto pistol shown as a first-person viewmodel (arms + gun + animations
-- converted from the MWII Deagle, GameBanana 532396, private use) with an optional native companion plugin
-- (deagle_native.asi: zero-lag placement, sounds, procedural motion).
-- Everything that touches the engine is pcall-guarded so a missing binding logs an error instead of breaking the game.
-- One file per system (README.md, "Where things are"): this one holds the settings (cfg), the shared helpers (Deagle.U),
-- input, start-up and the commands, and loads the rest (Deagle:LoadModule, after Deagle.U).

Deagle = Deagle or {}

Deagle.cfg = {
	itemId = "d81561f0-5635-4197-84c2-7f2b1a92f013",   -- deagle_pistol (Libs/Tables/item/item__deagle.xml)
	ammoId = "214b29af-66a9-42be-882a-d63562010b7b",   -- deagle_ammo
	useAmmoItems = false,     -- cartridge items stack as one count in KCD2; unlimited reserve until that is solved
	magSize = 7,
	interval = 0.22,          -- seconds between shots (the gun's own / CS2 cycle time replaces it)
	fireRate = "mod",         -- "mod": the GameBanana mod's own cycle time (as fast as you click); "cs2": CS2's
	rangeMod = 1.0,           -- damage x rangeMod per 12.7 m (CS2: per 500 units)
	burstShotGap = 0.05,       -- seconds between a burst's rounds (a Glock 18 fires ~1,100-1,200 rounds a minute)
	burstInterval = 0.3,      -- after a burst
	-- Ready or Not's R for its guns (gun.ronStats; docs/ron/ron_grizzly_interactions.md 4): a tap reloads tactically
	-- (keeps the magazine) tap s after the press, a second press within double s of the first reloads fast (drops the
	-- magazine), holding past hold s checks the magazine instead (its inspect). Real seconds (F9 slow motion too).
	reloadKey = { tap = 0.25, double = 0.2, hold = 0.2 },
	-- Ready or Not's low ready on its guns (docs/ron/ron_grizzly_interactions.md 3, its FUN_144b436f0): rays from start m
	-- ahead of the eye, a ring of radius m round the view line (radiusLow once lowered: it stays down a little longer).
	-- Something within the gun's length (root bone to muzzle + 5 cm) + rangeOffset m lowers it: its lowUp pose while the
	-- view pitch is above pitch deg, lowDown below. Within its length + pushRange m the plugin pulls it back (its Back
	-- transform). Sprinting (over sprintIn m/s, back under sprintOut, not aiming or crouched) plays its sprint clips (the
	-- user, 2026-10-19: low ready = sprint). Lowered it can't fire or aim. blend s into and out of the poses.
	lowReady = { start = 0.05, rangeOffset = 0.18, pushRange = 0.30, radius = 0.09, radiusLow = 0.11, pitch = -25,
		blend = 0.2, sprintIn = 4.6, sprintOut = 3.8 },
	-- Ready or Not's quick lean on Q / E (held) with a gun out (the plugin with "lean"; docs/ron/ron_grizzly_interactions.md,
	-- its PlayerCharacter code + ANIMBP_Player_FP_Master): a lean of amount cm runs out at rateIn cm/s and back by an
	-- FInterpTo at outSpeed; the view follows (VInterpTo camSpeed) amount cm to the side and dip x amount down, without
	-- rolling; the arms and gun cant the same number of degrees (up to cantMax, RInterpTo cantSpeed). Its wall trace: a
	-- capsule (radius, half height m) from the chest to that side, amount + margin cm long; a hit at d cm leaves
	-- clamp(d - margin, 0, margin). on = 0: Q and E stay KCD2's.
	lean = { on = true, amount = 15, margin = 15, rateIn = 400, outSpeed = 30, camSpeed = 8, dip = 0.5, cantSpeed = 8,
		cantMax = 25, radius = 0.05, half = 0.40 },
	heatPerShot = 0.35,       -- degrees of extra spread per shot; cools at heatDecay deg/s (rapid fire opens the cone)
	heatDecay = 4.0,
	kickPitchVar = 0.0,
	range = 1000,             -- m a round flies before it's dropped (tools/gen_tracers.py RANGE matches); its tracer burns
	                          -- all the way (a real one burns for seconds: the user's .50 cal reference clip)
	tracers = true,           -- a glowing tracer on every round (deagle_tracers 0 / 1)
	-- mode "trail": the round glowing as it flies (Deagle:TracerBullet: length m behind it each frame; fx pieces dash
	-- m long at scale 1, tools/gen_tracers.py DASH), from start m out; "flying": one particle per round flying by itself
	-- (deagle_tracers.v<speed>; not seen in game 2026-10-12). scaled: a piece's scale sets its length (seen in game:
	-- scale 3 = 3x longer). anchor: where the effect sits on its piece (mid / start / end, by how its streak extends;
	-- the pieces streak forward from their start). spawn "plugin": the plugin spawns each piece with its bounding box
	-- set, so the engine draws it at once (Lua-spawned 0.12 s pieces never showed); "lua": Particle.SpawnEffect.
	-- fx: in game 2026-10-12, single Lua-spawned pieces showed only with 0.4 s of life (trail_40, "like a shooting
	-- star"); 0.12 / 0.25 s never did, nor plugin-spawned ones with a box written at spawn. The plugin now sets each
	-- new piece's location again for its first frames, as it moves the flash (seen a frame after its spawn): to test
	-- with spawn "plugin" and fx deagle_tracers.trail: seen at once (user). fx "auto": each piece glows about one frame
	-- (trail_025 / _040 / _060 by the frame time), so the tracer is one streak running with the round, like a real one
	-- filmed (the user's reference clip), not a line left behind it. trail_025 / _040 / _060 / _080: pieces
	-- living that long (no fade), a streak running with the round; trail (0.12 s) drew as a 50 m "laser".
	-- spawn "restart" (default): the same pieces from ONE emitter per round, moved and restarted each frame (plugin
	-- fxr): a new emitter per round per frame ("plugin") took 140 -> 70 fps spraying three magazines and crashed KCD2
	-- (DX12 descriptor heap out, 2026-10-15)
	tracer = { mode = "trail", fx = "auto", dash = 1.0, start = 1.0, length = 1.5, scaled = true, anchor = "start",
		spawn = "restart" },
	-- CS2 accuracy: scale 1 = CS2's own numbers (0.5 = twice as tight, 0 = no spread); crouched = eye under crouchEye m
	-- (+- crouchBand; measured live: 1.54 standing, 1.02 crouched)
	accuracy = { scale = 1, crouchEye = 1.27, crouchBand = 0.05 },
	damage = 60,              -- health damage (game applies body_part coeff: head 1.5, limbs 0.7)
	headshotMult = 3.0,       -- extra on top: .50 AE to the head should drop anyone
	stamina = 40,
	impulse = 220,            -- physics push on props and bodies
	spreadHip = 1.6,          -- degrees (guns without a CS2 profile; CS2 guns use CS2's accuracy, Deagle:CsAccuracy)
	spreadAim = 0.25,
	aimFovScale = 0.8,
	hitReactions = true,      -- KCD2's own flinch / stagger on a hit NPC (plugin hitreact)
	hitReactType = 0,         -- the game's hit type for it: 0 has the living rows (weak / minor / major, 4 directions)
	hitReactMajor = 50,       -- health lost from which a hit is a major injury (the game's own threshold)
	hitReactFlags = 0x3F,     -- event flags (0x3F all: game impact FX, AI / crime alert...; plugin bits in main.cpp)
	-- who a hit NPC thinks shot it. "missile": like the game's arrows (Deagle:HitAI): it names the player only if it has
	-- seen him, else it searches toward the shot. "direct": the player named on every hit (DealDamage's attacker): shot
	-- people knew it was you even unseen, and only ran (the user, 2026-10-14: "if u snipe with a bow they dont know its you")
	hitAI = "missile",
	hitMode = "soul",         -- "soul" = soul:DealDamage with native hit reactions; "hit" = gamerules ServerHit (no effect on souls in KCD2)
	hitType = "bullet",
	shotTrigger = "c_f_firearm_shot",   -- the game's gunshot; skipped while the plugin plays the MWII shots
	dryTrigger = nil,
	reloadTrigger = nil,
	muzzleFx = "deagle.muzzle",         -- Libs/Particles/deagle.xml (preloaded via PreloadList.txt): the flash, rides on the gun
	muzzleFxLarge = "deagle.muzzle_large",   -- the same flash burning longer, for big blasts (cfg.physics.largeFlashBlast)
	-- every shot's smoke, left in the world: Ready or Not's P_WeaponSmoke_M2011_Muzzle (its Grizzly .50's, every shot;
	-- tools/ron/casc2pfx.py), at the Deagle's size (the Grizzly's own cartridge) and smaller for smaller blasts;
	-- "deagle.muzzle_smoke" = our earlier smoke + barrel wisp
	muzzleSmokeFx = "deagle.muzzle_smoke_ron",
	-- "gun": the flash rides the muzzle, moved with the gun every frame (like CS2 / CoD / Ready or Not); "world": it stays
	-- where it was fired; "live": one never-stopping emitter shown for the frames of a shot. Not "live": its hide (a
	-- size of nothing) didn't hide it, and letting it go doesn't stop it (the plugin only drops its reference): after
	-- a save load one kept burning where the gun had been (in game 2026-10-12, the "fireball")
	flashMode = "gun",
	-- flashMode "mesh": flash cards on the gun's muzzle bone (tools/flash_mesh.py; docs/flash_mesh_plan.md), shown by
	-- hiding / unhiding them, which the renderer reads when it draws the gun: no particle delay. One random variant and
	-- roll (quarter turns round the barrel, baked into the cards; roll false = unturned only) per shot for time s of
	-- game time (Ready or Not 0.035-0.04 s: 2 frames at 60 fps, 1 under 40 fps, ~10x longer in F9 slow motion; a second
	-- variant halfway read as the flash switching shape: user 2026-10-14); mtl = their strength (deagle_vm_flash,
	-- _dim, _hot); set = the cards by blast (large for big blasts, cfg.physics.largeFlashBlast). look "ron": Ready or
	-- Not's own flash instead (flashRon). Light (light.mode): "ron" = Ready or Not's two flash lights on the gun from the
	-- shot's own frame (Deagle:FlashMeshLight; intensity = the main one's, bulb = its attenuation bulb m), "particle" =
	-- the old per-shot particle light (a frame late, sized with the flash, lit the night fog)
	flashMesh = { look = "ours", time = 0.035, roll = true, mtl = "deagle_vm_flash", large = "deagle_vm_flash", small = "deagle_vm_flash_s",
		light = { mode = "ron", intensity = 0.5, bulb = 0.3, refBlast = 2.08 } },
	-- flashMesh.look "ron" (deagle_flash ron): Ready or Not's pistol flash, its textures and its Cascade system run and baked
	-- onto cards (tools/ron/flash_bake.py): WeaponFlash_Pistol_V3 of the Grizzly MK V .50 AE for big blasts, the G19's V2
	-- for small ones. variants x stages cards; a shot picks a variant and shows the stage its game time is in. Its
	-- Sparks (50-85 ms) are the particle effect sparks, spawned on the muzzle with the cards. Rotations are baked, no
	-- roll. mtl: _dim / _hot (x1/3, x3)
	-- the share of shots with a flash (and its light): Ready or Not's MuzzleFlashChance 0.65; the smoke comes every shot
	flashChance = 0.65,
	-- flashRon: tools/ron/flash_bake.py's cards, one stage per 60 fps frame (time = stages / 60): stage k = Ready or
	-- Not's flash at the end of frame k after the shot. Below 60 fps a frame can skip a stage (30 fps: the shot's frame
	-- shows stage 2, at 16.7 ms into a 33 ms frame, and the next is past the flash); that is right, Ready or Not draws its
	-- pops at ~33 ms of age on that frame too: its sprites live 35-40 ms at any frame rate. Don't "fix" it to show
	-- every stage.
	flashRon = { time = 2 / 60, stages = 2, variants = 16, mtl = "deagle_vm_flash_ron", large = "deagle_vm_flash_ron",
		small = "deagle_vm_flash_ron_s", sparks = "deagle.muzzle_sparks_ron",
		-- its exposure steps (Deagle:FlashEvSuffix): KCD2's eye EV over the day (EV Max 11 from 7 to 19 h, EV Min 7 at
		-- night; foggy_drizzly's time of day), the .mtl suffix baked for each EV (tools/ron/flash_bake.py, solved through
		-- that hour's curve: the flash shows the same by day and by night), indoors at most evIndoor (the eye adapts to
		-- EV Min there), worked out once every evEvery s. A strength picked in flashRon.mtl (_dim, _hot...) turns it off
		evCurve = { { 0, 7 }, { 4, 7 }, { 5, 7.8 }, { 6, 9.34 }, { 7, 11 }, { 19, 11 }, { 20, 9.34 }, { 21, 7.8 }, { 22, 7 }, { 24, 7 } },
		evSteps = { { 11, "" }, { 9.34, "_ev9" }, { 7.8, "_ev8" }, { 7, "_ev7" } }, evIndoor = 7.8, evEvery = 1.0 },
	muzzleFxLive = "deagle.muzzle_live", muzzleFxLargeLive = "deagle.muzzle_large_live",   -- the live flash (FlashArm)
	muzzleLight = "deagle.muzzle_light", muzzleLightLarge = "deagle.muzzle_large_light",   -- its light, per shot
	flashLive = { 0.8, 1.0 },  -- the live flash's size on each frame it's seen (2 frames; 1 frame under 40 fps)
	-- the flash's size on each frame it's seen (plugin), then gone; {} = the particles' own life. OFF: counting frames
	-- from the emitter's first valid bounding box hid it before it was ever drawn (in game 2026-10-12: no flash at
	-- all); a correct way is being researched (docs/muzzle_flash_research.md)
	flashGrow = {},
	-- m: a per-shot flash gets a bounding box this big (x its size) the moment it's spawned, so the engine needn't wait
	-- for its first update to see it (plugin flashseed; 0 = off). Under test: the flash showed one game frame late
	flashSeed = 0,
	slowmo = 0.1,             -- F9 (deagle_slowmo): game speed while slow motion is on, for checking effects
	debugZoom = { 4, 10 },    -- F2 / mouse 4 (deagle_zoom): zoom steps for looking at impacts / tracers far away, then off
	debugPaths = false,       -- deagle_paths 1 / 0 / clear: draw every round's path, kept until cleared
	-- deagle_set debugShotLog 1: every shot logs Henry's eye, the aim, the free camera (plugin log) and the round's place
	-- each frame to kcd.log ("[Deagle] shot N ..."), to line a recording up with where the round really was
	debugShotLog = false,
	-- where the player is, for guns that fire through Ready or Not's own sound engine (gun.ron, docs/ron_live_spec.md):
	-- rays from the eye every period s; starting values from the spec, to tune by ear (deagle_env prints the numbers).
	-- fade: seconds a room / material weight takes from 0 to 1 when the place changes (RoN fades them across doorways)
	-- gun sounds: "ron" = Ready or Not's engine (shots with room sound, reloads / draw / inspect, empty click, aim);
	-- "original" = the gun mods' own recordings (MWII Deagle, Insurgency / MWII Glock: shots and animation sounds).
	-- Impacts and casings are Ready or Not's either way. deagle_sounds ron|original
	sounds = "ron",
	shotSound = true,         -- the gunshot itself (deagle_shotsound 0: silent shots, to hear impacts on their own)
	gunFx = true,             -- everything at the gun: flash, smoke, muzzle light, casings, tracers and the gunshot
	                          -- (deagle_gunfx 0: only the impacts, to watch them on their own)
	env = { on = true, period = 0.25, fade = 0.4, upLen = 30, upCone = 15, roof = 12, roofHits = 3, ringLen = 40, ringNear = 20,
		ringInside = 12, small = 3.5, hall = 7, corrNarrow = 2.5, corrWide = 6, share = 0.5, vegShare = 0.25, trunks = 6 },
	debugPathsMax = 600,      -- line pieces kept (oldest go first; the game's debug-line buffer is limited: at 3000 the newest
	                          -- shots weren't drawn, in game 2026-10-12)
	-- a shot after the old mag has left the gun mid-reload: false = only the chambered round is left (the gun has no mag
	-- until the next reload, like real guns); true = arcade, the rounds you had stay and the mag pops back
	cancelKeepsRounds = false,
	muzzleFxScale = 1.0,      -- multiplier on the flash size the cartridge gives (cfg.physics.flashPerUnit)
	-- the ejection port's puff at every casing, along its throw: Ready or Not's P_WeaponSmoke_M2011_Ejection (its
	-- ShellParticle); "deagle.port_smoke" = our earlier one
	portFx = "deagle.shell_puff_ron",
	-- Ready or Not's barrel smoke after a burst (Deagle:BarrelHeatShot / BarrelHeatTick, weapon.lua): its
	-- BaseMagazineWeapon heat (per shot, threshold, max, cooling a second, minTime s after the last shot; constructor
	-- defaults, the Grizzly keeps them: 4 quick shots smoke, 3 don't), its P_BarrelSmokeTrail as fx; follow / hold:
	-- s the plugin keeps the emitter on the muzzle / holds it (its 1.6 s + 0.02 s delay; + its longest 1.6 s particle)
	barrelSmoke = { fx = "deagle.barrel_smoke_ron", perShot = 0.4, threshold = 1.0, max = 2.0, cool = 0.2, minTime = 0.5,
		follow = 1.62, hold = 3.3 },
	bloodFx = "deagle_blood.entry",           -- Ready or Not's blood (deagle_blood.xml): entry splash, back toward the shooter
	bloodArmorFx = "deagle_blood.armor_entry",  -- through padding / mail / plate: a cloth tuft and less blood
	bloodExitFx = "deagle_blood.exit",        -- out the back, along the round
	armorFx = "deagle_impacts.metal",   -- a round on plate / mail sparks like any metal (KCD2's plate hit effect doesn't exist)
	-- how impact / blood effects are spawned. "plugin": the plugin spawns them and sets their place again for their first
	-- frames (like the tracer pieces), so they're drawn at once; a Lua-spawned emitter isn't drawn for ~0.3 s (MODLOG,
	-- tracer lab), which hid Ready or Not's sparks and flash (0.05-0.3 s). The plugin holds each effect 2 s. "lua":
	-- Particle.SpawnEffect (the old way; also the fallback without the plugin)
	impactSpawn = "plugin",
	-- water and metal hit looks: "ron" = Ready or Not's; "new" = the test looks water_v3 / metal_v2 (deagle_impacts.xml:
	-- a fast jet whose spray splashes where it lands; a fast flat spark fan that differs every hit)
	impactLook = "ron",
	-- light from sparky hits (effects.lua ImpactLight): a pool of max world lights, each burning time s (game time),
	-- radius m (far past where it fades, so no ring), off m out from the surface; brightness x the impact scale
	impactLight = { on = true, max = 6, time = 0.05, intensity = 0.3, radius = 15, bulb = 0.3, off = 0.15 },
	-- impact / blood effects under heavy fire (effects.lua FxBudget): none within near m (x impact scale) of a fresh one
	-- (nearTime s) of the same kind; bursts of burst, then rate a second
	fxBudget = { on = true, rate = 30, burst = 15, near = 0.5, nearTime = 0.15 },
	-- sounds under heavy fire (sound.lua SoundBudget), per category: none within near m of a fresh one of the same kind
	-- (nearTime s); bursts of burst, then rate a second; sounds farther than closeM from the listener leave reserve tokens
	-- for close ones. Your own gunshot, handling sounds and hits on people are never dropped
	soundBudget = { on = true, closeM = 10,
		hit = { rate = 30, burst = 15, near = 0.5, nearTime = 0.12, reserve = 5 },
		ricochet = { rate = 10, burst = 5, near = 1, nearTime = 0.25, reserve = 2 },
		casing = { rate = 12, burst = 6, near = 0.3, nearTime = 0.1, reserve = 2 } },
	headFx = "deagle_blood.head",             -- burst out of the back of a head
	holeSize = 0.035,         -- m, a 9 mm's bullet hole (Ready or Not's pistol holes: 3-4 cm); x the impact scale
	holeExitSize = 0.09,      -- m, the hole a round leaves coming out the far side (Ready or Not's exits: 6-12 cm)
	wallbang = { budget = 0.10 },  -- m of density-1 material (plywood) a 9 mm at the muzzle gets through (Ready or Not's
	                          -- 9 mm guns: 10 cm); others by energy ratio; densities in guns/penetration.lua
	ricochetKeep = 0.35,      -- energy a round keeps when it skips off a surface without its own (guns/penetration.lua keep: GUESS)
	ricochetLift = 0.4,       -- off hard surfaces (steel, stone): the share of its angle to the surface it leaves with
	                          -- (unyielding ones send it off flatter); softer ones have their own (guns/penetration.lua)
	ricochetScatter = 4,      -- deg of random turn on a ricochet
	-- the chance a round skips off: 1 / (1 + e^((angle - crit x critScale) / width)): half at the material's critical
	-- angle (guns/penetration.lua), ~95 % 3 deg flatter, ~5 % 3 deg steeper (width GUESS: whitewood went from always
	-- at 12 deg to never at 13). critScale: every world material's angle x this (game feel; 1 = the measured ones).
	-- volume: the ricochet whine x impactVolume; fxScale: its sparks x the impact's size
	ricochet = { width = 1, critScale = 1, volume = 2, fxScale = 1.5 },
	-- a round cutting through a corner or an edge (in through one face, out through one that isn't the far side): the
	-- material is on one side of it only, so it's pushed off away from the material and starts tumbling. Bent up to
	-- maxDeg x how lopsided the two faces are x (0.5 + half the share of its reach it spent), + scatter deg; the corner
	-- throws chipScale x the normal debris. GUESS (no study of corner clips found)
	deflect = { maxDeg = 12, scatter = 3, chipScale = 1.6 },
	tumble = { pen = 0.5 },   -- a tumbling round (after a ricochet or a corner) meets things side-on: this much of its
	                          -- punch / reach (GUESS); its wounds aren't smaller
	-- armour (Deagle:ThroughArmor): punch = a 9 mm at the muzzle, square-on, in KCD2 stab defense. Set so the Glock
	-- (110) gets through a typical worn cuirass (median plate stab 135 x 0.8 = 108) only within ~5 m and square-on, a
	-- weaker worn one (132: 106) within ~12 m, a new one never; the Deagle (~200) through most plate at any range.
	-- critScale: plate / mail critical angles x this (game feel, apart from the world's); a round plate stops sprays
	-- its fragments along the plate, splashAngle deg off its face
	-- (a glance keeps the energy guns/penetration.lua gives its layer: plate as sheet steel, mail as steel)
	armor = { punch = 104, conditionFloor = 0.6, critScale = 1, bruise = 0.1, scatter = 30, splashAngle = 20 },
	armorRicochetFx = "deagle_impacts.ricochet_metal",
	-- bullet impact / ricochet / armour sounds: x Ready or Not's own level (it mixes impacts ~9 dB under the shot);
	-- impactNear: m within which they play at full level, then -6 dB per doubling of the distance (real sound). RoN's
	-- own fade (PropagateDistance) took them out by 12 m: 5 m -4 dB, 10 m -23, a close-quarters game's choice.
	-- Now: 6 m = RoN's point-blank level, 12 m -6 dB, 24 m -12, 48 m -18
	impactVolume = 2.0, impactNear = 3,
	-- KCD2's AI hearing (XGenAIModule.ProduceSound*): shot = the gun_fired multiplier for a 9 mm (louder guns more;
	-- GUESS ~80 m, calibrate live), hit = bullet_hit multiplier at an impact for a 9 mm
	-- shot 1: KCD2's own guns make their gun_fired at x1 (item.xml FireSoundMultiplicator 1; it was a guessed 3.2)
	hearing = { on = true, shot = 1.0, hit = 1.0 },
	-- a round flying within nearMiss m of a living NPC's chest: KCD2's near-miss message for an arrow (Deagle:FlyBy); 0 = off
	nearMiss = 2.5,
	exitEnergy = 20,          -- J: a round leaving the body with less stays in it (no exit wound)
	-- a killing round: ragdoll at once and a shove by its momentum (x killPush) pushDelay ms later, at the part it hit
	gore = { ragdollOnKill = true, killPush = 1.0, pushDelay = 60 },
	bodyExit = 0.35,          -- m from the entry where a through-and-through round goes on
	holeLife = 300,           -- s a bullet hole stays
	-- bullet holes like the effects and hit sounds (Deagle:HoleAllowed): none within near m (x the impact scale) of one
	-- made in the last nearTime s, then at most rate a second after a burst. At 75 rounds/s into the ground ~200 holes a
	-- second took the frame to 250 ms (engine time: KCD2 builds each decal later in the frame; the user's A/B run
	-- 2026-10-15, holes off held 51-70 fps)
	holeBudget = { on = true, rate = 30, burst = 15, near = 0.1, nearTime = 0.5 },
	bloodPaint = 0.45,        -- actor:AddBlood amount per hit on that body part
	bleeding = true,          -- non-lethal hits apply the game's bleeding buff
	bleedingBuff = "0c903899-fcc9-4cf2-9ee3-1130ac08b0fc",
	armorFxThreshold = 0.5,   -- armor ratio above which hits spark instead of splash
	splatRange = 3.0,         -- how far behind the target blood can land
	splatLife = 180,
	recoil = 1.0,              -- master kick: scales every recoil effect together (camera, sideways, gun model flip + push)
	-- cartridge-driven handling (guns/calibers.lua -> ShotPhysics): physics -> screen. PROVISIONAL constants, set so the
	-- .50 AE Deagle kicks ~4.5 deg; to be fitted to Ready or Not's values for the same guns. A gun with no cartridge
	-- data keeps kickPitch / kickYaw below.
	physics = {
		camPerFlip = 0.35,      -- camera kick (deg) per rad/s of the Deagle's muzzle flip: 20.3 rad/s -> 7.1 deg, above Ready or
		                        -- Not's .357 Python (5-6 deg a shot, docs/ron/); other guns through kickExponent
		-- elastic hold (design 1): the grip springs back up to holdRange degrees of camera kick; what goes past it
		-- stays (you pull it down). A calm Deagle shot returns 65 %, fast follow-ups much less, a Glock ~95 %.
		holdRange = 4.7,        -- deg of camera kick the grip springs back (scaled with camPerFlip: same physical hold as 2.9 at 0.217)
		holdMax = 0.6,          -- most of a kick that ever comes back by itself (a shooter's natural spring back to the point
		                        -- of aim; 0 = Ready or Not's pistols, all of it stays: too stiff for the user, 0.95 too soft)
		holdYaw = 0.5,          -- the sideways range is half the vertical one (sideways control is weaker)
		holds = { pistol = 1.0, pistol1h = 0.5, rifle = 1.2 },
		adsHold = 1.3,          -- aiming tightens the grip
		returnRate = 5,         -- 1/s, how fast the held part settles for the reference gun (Deagle); heavier = slower
		returnInertia = 0.0245, -- kg m^2 of that reference (gun + hand); rate x sqrt(ref / inertia)
		riseRate = 70,          -- 1/s, how fast the kick lands (not a one-frame jump)
		sideSigma = 0.15,       -- sideways kick: random share of the climb (normal, clamped at 2 sigma); per-gun bias 0 for pistols
		gunRollPerYaw = 0.5,    -- the gun model twists with its sideways kick (cosmetic)
		barrelAim = 1.0,        -- shots leave along the gun's un-returned kick (1 = physical, 0 = along the camera)
		vary = 0.15,            -- +- per shot
		adsMult = 0.7,          -- aimed: steadier
		gunBackPerVel = 0.0113, -- gun pushed back (m) per m/s of free recoil (Deagle 4.84 -> 5.5 cm)
		gunFlipPerFlip = 0.467, -- gun model flip (deg) per rad/s, on top of the shot animation (Deagle 9.5 deg)
		-- felt kick: physics alone makes a 9 mm Glock kick a third of a .50 AE Deagle; shooters (and games like Ready or
		-- Not) feel a much smaller gap. Kick goes as (flip / Deagle's flip)^kickExponent x Deagle's: the Deagle is
		-- unchanged, smaller guns are lifted toward it, the order stays (1 = pure physics: Glock 0.35x; 0.67: 0.49x,
		-- Glock camera kick 3.5 deg, the user's pick in game 2026-10-12, with the Deagle at 7.1)
		kickExponent = 0.67,
		kickRefFlip = 20.26,    -- rad/s: the Deagle's muzzle flip (the gun whose kick the constants above are set on)
		kickRefVel = 4.84,      -- m/s: the Deagle's free recoil speed
		flashPerUnit = 0.30,    -- flash size per unit of blast scale G (9 mm = 1: Glock 0.30, Deagle 0.62)
		damageRef = 35,         -- HP a 9 mm 124 gr FMJ at 360 m/s takes from the torso (3 hits on a 100 HP NPC)
		staminaRef = 20,        -- stamina a 9 mm's shove takes; others by momentum
		headMult = 2.0,         -- on a head hit that doesn't get into the skull (KCD2 adds nothing of its own to DealDamage)
		skullJPerMm2 = 0.4,     -- J per mm2 of the bullet's face that perforate bone (Sellier & Kneubuehl, Wound Ballistics):
		                        -- a round reaching the head with that much (9 mm 26 J, .50 AE 51 J) kills
		bodyMass = 80,          -- kg: the body the wound numbers are set on (KCD2's people); smaller bodies take more
		sizeExponent = 0.5,     -- wound x (bodyMass / target mass)^this (a hare dies to anything, a horse takes ~2.5x hits)
		punchPerMomentum = 25.6,-- physics push on bodies / props per kg m/s of bullet momentum (Deagle 8.6 -> 220)
		largeFlashBlast = 1.5,  -- blast scale from which the flash burns longer (0.13 s instead of 0.1; Deagle 2.08)
		loudnessSpan = 0.25,    -- share of the real loudness difference played (Deagle +6.5 dB over the Glock -> Glock 0.83x)
	},
	kickPitch = 2.6,          -- degrees of muzzle climb per shot
	kickYaw = 0.8,
	modelDir = "objects/manmade/weapons/deagle/",
	-- casing bounce sounds (Deagle:CaseTick): bounces under minSpeed m/s are silent (RoN's ShellNormalizeMin 0.5), at
	-- most max per casing, followed for life s; volume x RoN's own level
	caseBounce = { minSpeed = 0.5, drop = 0.4, max = 3, life = 4, volume = 1.0 },
	-- casing throw (Deagle:EjectCase), Ready or Not's BulletShell (docs/ron/gun_smoke_shells.md 2, from its decompiled
	-- shell init): out of the gun's right side angle deg up, at max(base m/s, Henry's sideways speed) x a random times;
	-- spin a random whole 0..spin rad/s on each world axis (the same number), cut to spinCap: Unreal's default top spin
	-- 3600 deg/s (RoN sets none; Unreal's source default, not read from RoN's exe); KCD2's own cap is 20 rad/s in free
	-- flight (MODLOG 2026-10-13), so the plugin sets spinCap (casespin). nudge: random whole deg of pitch and yaw on the
	-- case's own rotation. damping /s (its BodyInstance, linear = angular). freeze / life: s until its physics stops / it
	-- is put away. RoN draws its first 0.3 s with the weapon's field of view; our casings are world objects, as is our gun.
	-- ghost: casings in flight pass through each other (until their first bounce); 0 = they collide all the way
	eject = { angle = 40, base = 2.0, times = { 1, 3 }, spin = 360, spinCap = 62.83, nudge = 10, damping = 1.0, freeze = 4.0, life = 6.0,
		ghost = true },
}

-- The viewmodel: its own character placed on the camera (its camera bone tag_view is the model origin, +Y forward),
-- so the arms never depend on Henry's body pose, pitch or strafing.
Deagle.cfg.vm = {
	cdf = "objects/manmade/weapons/deagle/vm/deagle_vm.cdf",
	chr = "objects/manmade/weapons/deagle/vm/deagle_vm.chr",
	skin = "objects/manmade/weapons/deagle/vm/deagle_vm_skin.skin",
	offset = { fwd = 0.0, right = 0.0, up = 0.0 },   -- camera-space nudge (m)
	fps = 24, layer = 0,
	-- transition blends (seconds), checked offline against every clip pair (transition audit, MODLOG):
	-- rest -> clip, refire mid-recoil, a shot cut by reload / holster / aim, an inspect cut by anything (the gun may be
	-- upside down). Clip end -> rest is instant: every clip ends on its rest pose.
	blend = 0.08, refireBlend = 0.12, cutBlend = 0.2, inspectCutBlend = 0.3,
	trimBlend = 0.1,          -- a clip ended where its hands settled (gun.times trim) is a few mm off its rest pose
	ads = false,              -- aim down sights on right click (deagle_ads 1); off = CS-style hip only
	recoilLayer = 1,          -- aimed shots: additive recoil on top of the ADS pose
	adsBlend = 0.14,          -- seconds to blend hip <-> ADS (pose and zoom)
	adsLead = 0.2,            -- reload / draw hand over to a held aim this long before they end, blending as long
	holsterSpeed = 1.6,
	anims = {   -- clip, frame count (24 fps)
		draw = { "deagle_vm_draw", 37 }, holster = { "deagle_vm_holster", 37 },
		idle = { "deagle_vm_idle1", 2 }, idleEmpty = { "deagle_vm_idle_empty", 2 },
		ads = { "deagle_vm_ads", 2 }, adsEmpty = { "deagle_vm_ads_empty", 2 },
		shoot = { { "deagle_vm_shoot1", 35 }, { "deagle_vm_shoot2", 35 } }, shootEmpty = { "deagle_vm_shoot_empty", 23 },
		shootAdd = { { "deagle_vm_shoot1_add", 35 }, { "deagle_vm_shoot2_add", 35 } }, shootEmptyAdd = { "deagle_vm_shoot_empty_add", 23 },
		reload = { "deagle_vm_reload", 65 }, reloadEmpty = { "deagle_vm_reload_empty", 68 }, inspect = { "deagle_vm_inspect", 153 }, inspectEmpty = { "deagle_vm_inspect_empty", 153 },
		drawEmpty = { "deagle_vm_draw_empty", 37 }, holsterEmpty = { "deagle_vm_holster_empty", 37 },
	},
	hideBody = true,          -- Henry's body and worn gear are hidden in first person while the gun is up (shadow stays)
	fpMaxDist = 0.35,         -- camera within this of the Head bone = first person (dialogue / cutscene cameras aren't)
	gearDist = 0.35,          -- worn items within this of the hips / spine are hidden with the body
	soundVol = { shot = 1.0, foley = 0.75, ron = 1.0 },   -- ron: Ready or Not handling events, x its own level
	-- s a Ready or Not handling sound starts early on top of its click time: the engine's output latency (its 256 x 4
	-- mix buffer + the device, ~25-40 ms, and the wait for the next camera step); deagle_set vm.cueLead
	cueLead = 0.045,
	dryDb = -22,              -- dB the empty click plays at in Ready or Not's mix (its shot: -3); RoN's own: -40 to -47
	motion = 1.0,             -- plugin bob / turn sway / sprint pose / landing strength (deagle_motion)
	arms = Deagle.cfg and Deagle.cfg.vm and Deagle.cfg.vm.arms or "mw",   -- "mw" or "medieval" (deagle_arms)
}

-- ammo HUD, drawn every frame with System.DrawText (bottom right); falls back to info text if DrawText is missing
Deagle.cfg.hud = { enabled = true, x = 0.885, y = 0.86, size = 2.6, font = "default",
	col = { 1.0, 0.95, 0.86, 0.92 }, empty = { 0.95, 0.35, 0.3, 0.95 } }

-- the settings as shipped, for deagle_reset (the player's own are kept by the plugin and replayed at every load: Set)
local function deepcopy(v)
	if type(v) ~= "table" then return v end
	local out = {}
	for k, x in pairs(v) do out[k] = deepcopy(x) end
	return out
end
Deagle.cfgDefaults = deepcopy(Deagle.cfg)
-- the kept ones as Set saw them (the load's replay goes through Set too): a dev reload sets them again (DevReload)
Deagle.kept = Deagle.kept or {}

Deagle.state = Deagle.state or {
	ammo = 7,
	drawn = false,
	aiming = false,
	reloading = false,
	lastShot = -100,
	logActions = false,
	baseFov = nil,
}

-- MWII sound cues per clip (clip-timeline seconds from clip start, i.e. 24 fps export frames / 24; PlayCues divides by
-- Rate()), from assets/build/vm_sound_cues.json; "shot" = random gunshot.
-- Retimed 2026-10-12 from the clips' own tracks (E:\Tools\deagle-work\out\tracks.json, gun-space j_mag1/j_mag2/j_slide):
-- the MWII events sit ~4 frames before the impacts in our clips, and each file has a lead-in before its hit, so the
-- hits came 80-90 ms (game time) before the impacts. Now cue = impact frame / 24 - lead-in x Rate (1.25):
--   reload       magin       1.375 -> 1.475  (j_mag2 seats at frame 37 = 1.542; hit 53 ms into the file)
--   reload_empty magin       1.125 -> 1.240  (j_mag1 seats at frame 31 = 1.292; hit 42 ms in)
--   reload_empty slideback   1.750 -> 1.781  (j_slide rearmost at frame 44 = 1.833; hit 42 ms in)
--   reload_empty slideclose  1.833 -> 1.934  (j_slide home at frame 48 = 2.000; hit 53 ms in)
-- Mag release / mag out already land within a frame of the mag leaving. (The inspect cues are 3-7 frames early too;
-- not retimed yet.)
Deagle.vmSoundCues = {
	["shoot1"] = { { 0, "shot" } },
	["shoot2"] = { { 0, "shot" } },
	["shoot_empty"] = { { 0, "shot" } },
	["reload"] = { { 0.000, "decho/p25_pi_decho_reload_raise.wav" }, { 0.667, "decho/p25_pi_decho_reload_magrelease.wav" }, { 0.708, "decho/p25_pi_decho_reload_magout.wav" }, { 1.042, "decho/p25_pi_decho_reload_magfindwell.wav" }, { 1.475, "decho/p25_pi_decho_reload_magin.wav" }, { 1.750, "decho/p25_pi_decho_reload_end.wav" } },
	["draw"] = { { 0.000, "decho/p25_pi_decho_raise_first_raise.wav" }, { 0.417, "decho/p25_pi_decho_raise_first_switch.wav" }, { 0.625, "decho/p25_pi_decho_raise_first_end.wav" } },
	["reload_empty"] = { { 0.000, "decho/p25_pi_decho_reload_empty_raise.wav" }, { 0.125, "decho/p25_pi_decho_reload_empty_pivot.wav" }, { 0.208, "decho/p25_pi_decho_reload_empty_magout.wav" }, { 0.917, "decho/p25_pi_decho_reload_empty_magfindwell.wav" }, { 1.240, "decho/p25_pi_decho_reload_empty_magin.wav" }, { 1.375, "decho/p25_pi_decho_reload_empty_tilt.wav" }, { 1.781, "decho/p25_pi_decho_reload_empty_slideback.wav" }, { 1.934, "decho/p25_pi_decho_reload_empty_slideclose.wav" }, { 2.042, "decho/p25_pi_decho_reload_empty_end.wav" } },
	["inspect"] = { { 0.000, "decho/p25_pi_decho_inspect_raise.wav" }, { 0.083, "decho/p25_pi_decho_inspect_pivot.wav" }, { 0.208, "decho/p25_pi_decho_inspect_magrelease.wav" }, { 0.208, "decho/p25_pi_decho_inspect_magout.wav" }, { 0.750, "decho/p25_pi_decho_inspect_magcatch.wav" }, { 2.500, "decho/p25_pi_decho_inspect_magfindwell.wav" }, { 2.917, "decho/p25_pi_decho_inspect_magin.wav" }, { 3.375, "decho/p25_pi_decho_inspect_rotate.wav" }, { 3.792, "decho/p25_pi_decho_inspect_slideback.wav" }, { 4.542, "decho/p25_pi_decho_inspect_slideclose.wav" }, { 4.667, "decho/p25_pi_decho_inspect_end.wav" } },
}
-- empty inspect: same cues, but the locked slide never closes
Deagle.vmSoundCues["inspect_empty"] = {}
for _, c in ipairs(Deagle.vmSoundCues["inspect"]) do
	if not c[2]:find("slideclose") then table.insert(Deagle.vmSoundCues["inspect_empty"], c) end
end
local CUE_SEQ = { shoot = "shoot1", shootEmpty = "shoot_empty", reload = "reload", reloadEmpty = "reload_empty", draw = "draw", drawEmpty = "draw", inspect = "inspect", inspectEmpty = "inspect_empty" }

-- ---- guns: the built-in Deagle plus every converted GameBanana gun (Scripts/Deagle/guns/<id>.lua, made by tools/gb).
-- A gun brings its viewmodel, clips (with their own frame rates), sound cues, gunshot sounds, muzzle / ejection
-- bones, magazine size and magazine-swap timings; the gameplay (damage, recoil, hits) stays the same.
Deagle.guns = {}
function Deagle:RegisterGun(id, g)
	g.id = id
	self.guns[id] = g
end
do
	local cues, origShots = {}, {}
	for key, seq in pairs(CUE_SEQ) do cues[key] = Deagle.vmSoundCues[seq] end
	for i = 1, 8 do origShots[i] = "decho_orig/decho-" .. i .. ".wav" end   -- the MWII Deagle's own shots (deagle_sounds original)
	Deagle:RegisterGun("deagle", { title = "Desert Eagle", magSize = Deagle.cfg.magSize, cdf = Deagle.cfg.vm.cdf, anims = Deagle.cfg.vm.anims,
		cues = cues, shotSounds = origShots, ron = { fire = "event:/Weapons/Grizzly/1P Grizzly Fire", fire3p = "event:/Weapons/Grizzly/3P Grizzly Fire", dry = "event:/Weapons/FN57/FN57 Dry Fire",   -- SoundData_Grizzly
			adsIn = "event:/Weapons/Shared/ADS/ADS Pistol Start", adsOut = "event:/Weapons/Shared/ADS/ADS Pistol Stop", shell = "event:/Weapons/Shared/9mmShell" },
		bones = { muzzle = "tag_flash", brass = "tag_brass" }, rounds = 6, mags = true,
		swapTail = { reload = 0.6 }, newMagAt = 0.69, armsLooks = true, cs2 = "weapon_deagle",
		cycle = 0.08,             -- the mod's weapon script (cycletime): CS2's Deagle is 0.225
		-- the clips were exported frame for frame at 24 fps, but the mod's sequences run at 30 (shots at 40): play them
		-- faster (measured against YabrakProds' own video of the mod: reload back at idle ~1.7 s, as at 30 fps)
		rate = { default = 30 / 24, shoot = 40 / 24, shootAdd = 40 / 24 },
		-- clip-timeline seconds (tools/clip_times.py): clean reload cancel, new mag seated, ready to fire, settled
		times = { reload = { cancel = 0.375, out = 0.833, ready = 2.375, seat = 1.475, trim = 2.375 },   -- seat = the mag-in cue
			reloadEmpty = { cancel = 0.333, out = 0.333, ready = 2.292, seat = 1.240, trim = 2.542 },
			inspect = { trim = 5.833 }, inspectEmpty = { trim = 5.833 },
			draw = { ready = 0.958, trim = 1.292 }, drawEmpty = { ready = 0.958, trim = 1.292 } } })
end
Deagle.gun = Deagle.guns.deagle

-- playback speed of a clip relative to its export rate; clip-timeline seconds (cues, mag timings) divide by it
function Deagle:Rate(key)
	local r = self.gun and self.gun.rate
	return r and (r[key] or r.default) or 1.0
end

-- a point of a clip from the gun's measured times (cancel / seat / ready / trim), in real seconds from its start
function Deagle:ClipTime(key, what)
	local t = self.gun and self.gun.times and self.gun.times[key] and self.gun.times[key][what]
	return t and t / self:Rate(key) or nil
end
local SHOT_CUE = { { 0, "shot" } }
local REST = { idle = true, idleEmpty = true, ads = true, adsEmpty = true }   -- looping rest poses
local HOLDS_AIM = { draw = true, drawEmpty = true, drawFirst = true, holster = true, holsterEmpty = true, reload = true, reloadEmpty = true, reloadSpeed = true }   -- aim waits for these; shots and inspect it cuts
local DRAWING = { draw = true, drawEmpty = true, drawFirst = true }        -- no shooting, reloading or inspecting until the gun is up
local HOLDS_TOGGLE = { draw = true, drawEmpty = true, drawFirst = true, reload = true, reloadEmpty = true, reloadSpeed = true, reloadAds = true, reloadEmptyAds = true }   -- a holster asked for meanwhile waits
local SHOTS = { shoot = true, shootEmpty = true, shootBurst = true, shootAds = true, shootEmptyAds = true }
local RELOADS = { reload = true, reloadEmpty = true, reloadSpeed = true, reloadAds = true, reloadEmptyAds = true }
local INSPECTS = { inspect = true, inspectEmpty = true }
local GAME_MAPS_OFF = { "combat" }   -- KCD2 action maps on our keys, off while the gun is out (the torch: OnAction)
-- and with the plugin's lean: Q (KCD2's chat). E's use / talk live in an include map: the plugin's no_interaction filter
local LEAN_MAPS_OFF = { "chat_init", "chat_init_interactor" }

-- ---------------------------------------------------------------------------
-- helpers
-- ---------------------------------------------------------------------------
-- Console %line / %1 substitutions arrive wrapped in double quotes.
-- console arguments arrive in quotes ("clear", checked live): the bare word, trimmed
function Deagle.Arg(v) return (string.gsub(tostring(v or ""), '"', '')):match("^%s*(.-)%s*$") end

local function unquote(str)
	str = string.gsub(str or "", '^%s*"', "")
	return (string.gsub(str, '"%s*$', ""))
end

local function log(msg)
	System.LogAlways("$5[Deagle] " .. tostring(msg))
end

local function try(label, fn, ...)
	local ok, err = pcall(fn, ...)
	if not ok then
		log("$4" .. label .. " failed: " .. tostring(err))
	end
	return ok, err
end

local function vadd(a, b) return { x = a.x + b.x, y = a.y + b.y, z = a.z + b.z } end
local function vscale(a, s) return { x = a.x * s, y = a.y * s, z = a.z * s } end
local function vlen(a) return math.sqrt(a.x * a.x + a.y * a.y + a.z * a.z) end
local function vnorm(a)
	local l = vlen(a)
	if l < 1e-6 then return { x = 0, y = 1, z = 0 } end
	return vscale(a, 1 / l)
end
local function vcross(a, b)
	return { x = a.y * b.z - a.z * b.y, y = a.z * b.x - a.x * b.z, z = a.x * b.y - a.y * b.x }
end
local function matToAngles(m)   -- rows of a rotation matrix -> CryEngine Ang3 (XYZ)
	return { x = math.atan2(m[3][2], m[3][3]), y = math.asin(math.max(-1, math.min(1, -m[3][1]))), z = math.atan2(m[2][1], m[1][1]) }
end

local function coneDir(dir, degrees)
	if degrees <= 0 then return dir end
	local up = { x = 0, y = 0, z = 1 }
	local right = vnorm(vcross(dir, up))
	local realUp = vnorm(vcross(right, dir))
	local r = math.tan(math.rad(degrees)) * math.sqrt(math.random())
	local a = math.random() * 2 * math.pi
	return vnorm(vadd(dir, vadd(vscale(right, r * math.cos(a)), vscale(realUp, r * math.sin(a)))))
end

-- CS2's shot direction: an inaccuracy part and a spread part, each at a random angle with a radius uniform from 0 to
-- its size (so shots bunch toward the middle), radians (CS:GO / CS2 FX_FireBullets)
local function csDir(dir, inacc, spread)
	local right = vnorm(vcross(dir, { x = 0, y = 0, z = 1 }))
	local up = vnorm(vcross(right, dir))
	local t0, r0 = math.random() * 2 * math.pi, math.random() * inacc
	local t1, r1 = math.random() * 2 * math.pi, math.random() * spread
	local x, y = math.cos(t0) * r0 + math.cos(t1) * r1, math.sin(t0) * r0 + math.sin(t1) * r1
	return vnorm(vadd(dir, vadd(vscale(right, x), vscale(up, y))))
end

local function camBasis()
	local pos, f = System.GetViewCameraPos(), System.GetViewCameraDir()
	local r = vnorm(vcross(f, { x = 0, y = 0, z = 1 }))
	return pos, f, r, vcross(r, f)
end

function Deagle:Player()
	return g_localActor or player
end

-- ---------------------------------------------------------------------------
-- feedback: sound, particles, light, view kick
-- ---------------------------------------------------------------------------
function Deagle:PlayTrigger(name)
	if not name then return end
	if self.native and name == self.cfg.shotTrigger then return end   -- the plugin plays the MWII shot instead
	local p = self:Player()
	try("audio " .. name, function()
		local id = AudioUtils.LookupTriggerID(name)
		if id then p:ExecuteAudioTrigger(id, p:GetDefaultAuxAudioProxyID()) end
	end)
end
local function vsub(a, b) return { x = a.x - b.x, y = a.y - b.y, z = a.z - b.z } end
local function vdot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
local function entityHex(id) return string.match(tostring(id), "(%x+)%s*$") or "0" end

-- helpers for the mod's other script files (ballistics.lua: rounds, ricochets, wallbangs, armour)
Deagle.U = { log = log, try = try, vadd = vadd, vsub = vsub, vscale = vscale, vlen = vlen, vnorm = vnorm, vcross = vcross,
	vdot = vdot, coneDir = coneDir }
-- and for the per-system files below, which take what they need as locals when they load
local U = Deagle.U
U.DRAWING = DRAWING
U.GAME_MAPS_OFF = GAME_MAPS_OFF
U.LEAN_MAPS_OFF = LEAN_MAPS_OFF
U.HOLDS_AIM = HOLDS_AIM
U.HOLDS_TOGGLE = HOLDS_TOGGLE
U.INSPECTS = INSPECTS
U.REST = REST
U.RELOADS = RELOADS
U.SHOTS = SHOTS
U.SHOT_CUE = SHOT_CUE
U.camBasis = camBasis
U.csDir = csDir
U.entityHex = entityHex
U.matToAngles = matToAngles
U.unquote = unquote

-- One of the mod's script files (path from the game's root, "Scripts/Deagle/..."): Script.LoadScript(file, bReload =
-- true, bRaiseError = true) always runs the file (Script.ReloadScript skips one that's already loaded, so after a dev
-- reload, which recreates Deagle.guns, only the Deagle was left). During a dev reload the file comes from the dev copy
-- of the scripts instead (build.sh --dev mirrors src/Scripts to E:/Tools/deagle-dev/Scripts), so a reload picks up
-- every file, not only deagle_main.lua.
Deagle.devRoot = Deagle.devRoot or "E:/Tools/deagle-dev/"
function Deagle:LoadModule(path)
	if not self.devLoading then return Script.LoadScript(path, true, true) end
	local chunk, err = loadfile(self.devRoot .. path)
	if not chunk then log("$4dev reload: " .. tostring(err)) return false end
	local ok, perr = pcall(chunk)
	if not ok then log("$4dev reload: " .. path .. ": " .. tostring(perr)) end
	return ok
end
-- the systems, one file each (their helpers from Deagle.U above; a file only uses what the ones before it made)
for _, f in ipairs({ "surfaces", "physics", "effects", "sound", "flash", "debug", "recoil", "hits", "npc", "tracers", "shells", "weapon", "viewmodel", "freecam", "flight" }) do
	Deagle:LoadModule("Scripts/Deagle/" .. f .. ".lua")
end

-- ---------------------------------------------------------------------------
-- native companion (deagle_native.asi): System.LogAlways("[DN] ...") lines are swallowed by the plugin as commands;
-- it answers the hello with the deagle_native_ok console command. Without it the mod works, minus sounds and motion.
-- ---------------------------------------------------------------------------
function Deagle:Native(cmd) System.LogAlways("[DN] " .. cmd) end

function Deagle:NativeHello()
	self.native, self.ronParamsFor = false, nil   -- the plugin forgets a Ready or Not gun's numbers at hello
	self.leanSent, self.leanParamsSent, self.sprintSent, self.wallSent = nil, nil, nil, nil   -- and the lean's, upright
	self:Native("hello")
end

-- which entity to place, offset, whether to follow the camera now, motion strength
function Deagle:NativeSyncVm()
	if not self.native then return end
	local c = self.cfg.vm
	if self.vmId then self:Native("vm 0x" .. entityHex(self.vmId)) end
	self:Native(string.format("off %.4f %.4f %.4f", c.offset.right, c.offset.fwd, c.offset.up))
	self:Native("follow " .. ((self.vmShown and self.vmInFp) and "1" or "0"))
	self:Native(string.format("motion %.2f", c.motion))
	self:Native("flashgrow " .. table.concat(self.cfg.flashGrow, " "))
	self:Native(string.format("flashseed %.3f", self.cfg.flashSeed))
	self:Native(string.format("speed %.3f", self:GameSpeed()))
end

-- ---------------------------------------------------------------------------
-- input: our action map + a wrapper around the player's action hook
-- ---------------------------------------------------------------------------
function Deagle:OnAction(action, activation, value)
	local s = self.state
	-- the Flight mod's keys come from the plugin (FlyKey) once it's linked: its own actions would count twice
	if self.flyKeys and type(action) == "string" and action:find("^flight_") then return true end
	if action == "deagle_look_x" or action == "deagle_look_y" then return self:FreecamLook(action, tonumber(value) or 0) end
	if action == "moveleft" or action == "moveright" then   -- the strafe keys (Ready or Not's roll: motion.lua), KCD2's still
		self.strafeKeys = self.strafeKeys or {}
		self.strafeKeys[action == "moveleft" and "left" or "right"] = activation ~= "release"
	end
	if action == "deagle_inspect" and s.drawn then
		if activation == "press" then self:Inspect() end
		return true
	end
	if not self.inputLoaded then self:EnsureInit() end
	if s.reloading then self:FinishReload() end
	if s.logActions then log("action " .. tostring(action) .. " " .. tostring(activation)) end
	-- R is the game's torch key too: KCD2 delivers toggle_torch to the player's OnAction (logged live, right before
	-- our deagle_reload), so while the gun is out we take it and the torch stays put. Switching the
	-- draw_holster_torch map off did nothing: it's an include map, its actions live inside "player".
	if s.drawn and (action == "toggle_torch" or action == "xi_toggle_torch") then return true end
	if action == "deagle_toggle" then
		if activation == "press" then self:Toggle() end
		return true
	end
	if action == "deagle_slowmo" then
		if activation == "press" then self:SlowMo(not self.slowOn) end
		return true
	end
	if action == "deagle_fc_faster" or action == "deagle_fc_slower" then   -- scroll: free camera speed
		if not self.fc and self.FlightScroll then return self:FlightScroll(action == "deagle_fc_faster", activation) end   -- flying: its speed (flight.lua)
		if not self.fc or activation ~= "press" then return false end
		local k = action == "deagle_fc_faster" and 1.5 or 1 / 1.5
		self.fcSpeed = math.max(0.5, math.min(200, (self.fcSpeed or 4) * k))
		self:Native(string.format("freecam speed %.2f", self.fcSpeed))
		self:Notify(string.format("free camera %.1f m/s", self.fcSpeed))
		return true
	end
	if action == "deagle_freecam_aim" then   -- right mouse in the free camera: the mouse aims Henry while held
		self.fcAim = activation == "press"
		if self.fc then self:Native("nocamera " .. (self.fcAim and "0" or "1")) end
		return self.fc ~= nil
	end
	if action == "deagle_zoom" or action == "deagle_zoom_mouse" then   -- F2 / mouse side button (KCD2 uses F10)
		if activation == "press" then self:CycleZoom() end
		return true
	end
	if not s.drawn then return false end
	if action == "deagle_fire" then
		if activation == "press" then self:TriggerPress() else self.state.trigger = false end
		return true
	elseif action == "deagle_firemode" then
		if activation == "press" then self:CycleFireMode() end
		return true
	elseif action == "deagle_aim" then
		self:SetAim(activation == "press")
		return true
	elseif action == "deagle_reload" then
		self:ReloadKey(activation == "press")
		return true
	elseif action == "deagle_lean_left" or action == "deagle_lean_right" then
		if not self:LeanOn() then return false end   -- no lean: the key is KCD2's
		self.leanKeys = self.leanKeys or {}
		self.leanKeys[action == "deagle_lean_left" and "left" or "right"] = activation == "press"
		return true
	end
	return false
end

local function wrapOnAction(tbl, label)
	local orig = rawget(tbl, "OnAction")
	if not orig or orig == Deagle.wrapped[label] then return false end
	local wrapper = function(selfEnt, action, activation, value)
		local consumed = false
		local ok, err = pcall(function() consumed = Deagle:OnAction(action, activation, value) end)
		if not ok then log("$4OnAction error: " .. tostring(err)) end
		if consumed then return end
		return orig(selfEnt, action, activation, value)
	end
	tbl.OnAction = wrapper
	Deagle.wrapped[label] = wrapper
	log("action hook installed on " .. label)
	return true
end

-- KCD2 delivers input actions to the player entity's OnAction (Scripts/Entities/actor/player.lua)
function Deagle:HookActions()
	self.wrapped = self.wrapped or {}
	if type(Player) == "table" then wrapOnAction(Player, "Player") end
	local p = self:Player()
	if p then wrapOnAction(p, "instance") end
	return self.wrapped.Player ~= nil or self.wrapped.instance ~= nil
end

-- Flying (Flight.active: flight.lua, or the old Flight mod): Henry isn't on his feet, so the gun's animation reads
-- him as standing still: no sprint pose, steps, sway, bob or jump loop at 10 m/s (viewmodel.lua, motion.lua)
function Deagle:Flying() return type(Flight) == "table" and Flight.active == true end
-- The Flight mod (Mods/flight, someone else's: Scripts/mods/flight.lua, global Flight) reads its keys from key
-- actions, and KCD2 sends Lua none while Henry carries a body (logged live 2026-10-18), so it can't fly with one. With
-- the plugin linked, the plugin reads those keys from the keyboard instead (flykeys) and FlyKey sets them; F4 toggles
function Deagle:FlyLink()
	if type(Flight) ~= "table" or type(Flight.Keys) ~= "table" then return end
	self:Native("flykeys 1")   -- the plugin answers deagle_flykey linked 1; until then Flight keeps its own keys
end
-- The plugin reads the keys the keybind menu gives the Flight rows (zzz_keybind_merge): keys = what it read (on a
-- change), clash = one key on two flight actions (both act on it), nobind = it can't read them (flight has no keys)
function Deagle:FlyKey(line)
	local arg = tostring(self.Arg(line))
	local kind, rest = arg:match("^(%a+)%s+(.+)$")
	if kind == "keys" then log("flight keys (the keybind menu's): " .. rest) return end
	if kind == "clash" then
		local key, a, b = rest:match("^(%S+)%s+(%a+)%+(%a+)$")
		if key then
			log("$6flight keys: " .. key .. " is on both " .. a .. " and " .. b)
			self:Notify(string.format("Flight keys: %s is on both %s and %s. Rebind one in Controls > Flight", key, a, b))
		end
		return
	end
	if kind == "nobind" then
		log("$6flight keys: the plugin can't read the keybinds (" .. rest .. "): flight has no keys")
		self:Notify("Flight keys: can't read the keybinds (" .. rest .. "), so F4 and the flight keys do nothing")
		return
	end
	local name, v = arg:match("^(%a+)%s+(%d)")
	if not name or type(Flight) ~= "table" or type(Flight.Keys) ~= "table" then return end
	if name == "linked" then
		self.flyKeys = v == "1"
		log("Flight mod: its keys come from the plugin " .. (self.flyKeys and "(they work while carrying a body)" or "no more"))
	elseif name == "toggle" then
		if v == "1" then
			Flight.active = not Flight.active
			if Flight.log then Flight.log(Flight.active and "Flight On" or "Flight Off") end
		end
	elseif Flight.Keys[name] ~= nil then
		Flight.Keys[name] = v == "1"
	end
end

function Deagle:LoadInput()
	local p = self:Player()
	if not p then return false end
	local ok = try("action map", function()
		ActionMapManager.LoadFromXML("Libs/Config/deagle_actionmap.xml")
		ActionMapManager.EnableActionMap("deagle", true)
	end)
	self.inputLoaded = ok
	log("input map loaded: " .. tostring(ok))
	return ok
end

function Deagle:EnsureInit()
	if self:HookActions() and not self.inputLoaded and self:Player() then
		self:LoadInput()
	end
	if self.native == nil then self:NativeHello() end
end

-- Script timers are cleared when a save loads, so init is driven by gameplay events, plus the first player action
-- as a fallback. Every load says hello again (the plugin forgets our viewmodel too).
function Deagle:OnGameplayStarted()
	log("gameplay started")
	local s = self.state
	s.reloading, s.drawn, s.aiming, s.aimHeld, s.lastShot, s.ammo = false, false, false, false, -100, self.cfg.magSize
	s.magOut, s.fireQueued = false, false
	s.baseFov, s.fovNow = nil, nil
	self.inputLoaded = false
	self:EnsureInit()
	self.vmId, self.casePools, self.caseThrow, self.flashUntil = nil, nil, nil, nil   -- a load ends the old world: its ids may now name other entities
	self.drawnOnce, self.rKey = nil, nil                        -- the first draw after a load is the gun's drawFirst
	self.leanKeys, self.leanSent, self.leanParamsSent = nil, nil, nil
	self.ronShake = nil                                         -- Ready or Not's shake back at its idle 0.05
	self.flashArmed = nil                                       -- the plugin forgets its live flash at the hello
	self.rounds, self.roundsToken = nil, nil                    -- rounds in flight belong to the old world too
	self.paths, self.pathsToken = nil, nil
	self.fc, self.fcAim = nil, nil                          -- the plugin turns the free camera off at the hello
	-- a load can set the game clock back, and times kept from before it then lie ahead: the gun's kicks grew without
	-- bound and every shot left along NaN, hitting nothing for minutes (in game 2026-10-14, a save loaded in a house)
	self.gunKick, self.camRecoil, self.camRecoilLast, self.motionIn, self.flashLights = nil, nil, nil, nil, nil
	s.heat, s.accFire, s.accT, s.burstReady = 0, 0, nil, nil
	if self.slowOn then self:SlowMo(false) end
	self:RemoveStaleEntities()   -- at load, not just on draw: a holstered gun would leave the pyramid in view
	self:NativeHello()
end

-- ---------------------------------------------------------------------------
-- console
-- ---------------------------------------------------------------------------
-- What a setting changes besides itself (the plugin holds some; others are applied when they change)
local AFTER_SET = {
	flashGrow = function(self) self:NativeSyncVm() end,
	slowmo = function(self) if self.slowOn then self:SlowMo(true) else self:NativeSyncVm() end end,
	flashSeed = function(self) self:NativeSyncVm() end,
	["vm.motion"] = function(self) self:NativeSyncVm() end,
	["vm.arms"] = function(self) self.vmArmsApplied = nil; self:VmApplyArms() end,
	["vm.ads"] = function(self) self:SetAim(false) end,
	["hud.enabled"] = function(self) self.hudOk = nil end,
	tracers = function(self) self:Notify("tracers " .. (self.cfg.tracers and "on" or "off")) end,
	debugPaths = function(self) if not self.cfg.debugPaths then self.paths = {} end end,
	["flashMesh.look"] = function(self) self:FlashMeshRebuild() end,
	["flashMesh.roll"] = function(self) self:FlashMeshRebuild() end,
}

-- deagle_set <key> <value>: change a setting; the plugin keeps it (deagle_settings.txt next to it) and sets it again
-- at every load, until deagle_reset. "gun <id>" picks the gun.
function Deagle:Set(line)
	local key, value = string.match(unquote(line), "^%s*(%S+)%s+(.-)%s*$")
	if not key then
		log("usage: deagle_set <key> <value>   (nested: deagle_set physics.camPerFlip 0.3)")
		return
	end
	if key == "gun" then
		if self:UseGun(value) then self:Native("cfgsave gun " .. value); self.kept.gun = value end
		return
	end
	local t, k = self.cfg, key
	for part, rest in string.gmatch(key, "([^.]+)%.(.+)") do t, k = t[part], rest end   -- one level of nesting
	if type(t) ~= "table" then log("no setting " .. key) return end
	if t[k] == nil and value ~= "nil" then
		-- a misspelt name would become a new setting nothing reads, and be kept: refuse it and name the close ones
		local prefix, lk, near = string.sub(key, 1, #key - #k), string.lower(k), {}
		local function edits(a, b)   -- letters to change, add or drop to turn a into b
			local row = {}
			for j = 0, #b do row[j] = j end
			for i = 1, #a do
				local diag, cur = row[0], i
				row[0] = i
				for j = 1, #b do
					local up = row[j]
					cur = math.min(up + 1, row[j - 1] + 1, diag + (a:byte(i) == b:byte(j) and 0 or 1))
					diag, row[j] = up, cur
				end
			end
			return row[#b]
		end
		for name in pairs(t) do
			local ln = string.lower(tostring(name))
			if ln:find(lk, 1, true) or lk:find(ln, 1, true) or edits(lk, ln) <= 2 then near[#near + 1] = prefix .. name end
		end
		table.sort(near)
		local msg = "no setting " .. key .. (#near > 0 and (" (did you mean " .. table.concat(near, ", ") .. "?)") or " (deagle_cfg lists them)")
		log(msg); self:Notify(msg)
		return
	end
	-- the value takes the setting's own type: on / off settings read 1 / 0 / true / false / on / off, lists read
	-- numbers apart by commas or spaces (flashGrow 0.7,1,1.1), numbers read numbers
	local cur, low = t[k], string.lower(value)
	local function same(a, b)
		if type(a) == "table" and type(b) == "table" then return table.concat(a, ",") == table.concat(b, ",") end
		return a == b
	end
	if value == "nil" then t[k] = nil
	elseif type(cur) == "boolean" then
		if low == "1" or low == "true" or low == "on" or low == "yes" then t[k] = true
		elseif low == "0" or low == "false" or low == "off" or low == "no" then t[k] = false
		else log("$4" .. key .. " is on / off: use 1 or 0") return end
	elseif type(cur) == "table" then
		local list = {}
		for v in string.gmatch(value, "[^,%s]+") do
			local n = tonumber(v)
			if not n then log("$4" .. key .. " is a list of numbers (e.g. 0.7,1,1.1)") return end
			list[#list + 1] = n
		end
		t[k] = list
	elseif tonumber(value) then t[k] = tonumber(value)
	else t[k] = value end
	local v = t[k]
	if AFTER_SET[key] and not same(cur, v) then AFTER_SET[key](self) end   -- a load's replay of an unchanged one: nothing
	local shown = type(v) == "table" and table.concat(v, ", ") or tostring(v)
	log("cfg." .. key .. " = " .. shown)
	local kept = type(v) == "boolean" and (v and "1" or "0") or type(v) == "table" and table.concat(v, ",") or tostring(v)
	self:Native("cfgsave " .. key .. " " .. kept)
	self.kept[key] = kept
end

-- deagle_tracers / deagle_ads / deagle_hud ...: on unless the argument has a 0 (no argument = on), kept like deagle_set
function Deagle:SetOnOff(key, arg)
	self:Set(key .. " " .. (Deagle.Arg(arg):find("0") and "0" or "1"))
end

-- deagle_reset: every setting back as shipped, the kept ones forgotten
function Deagle:ResetSettings()
	for k in pairs(self.cfg) do if self.cfgDefaults[k] == nil then self.cfg[k] = nil end end
	for k, v in pairs(self.cfgDefaults) do self.cfg[k] = deepcopy(v) end
	self:Native("cfgreset")
	self.kept = {}
	if self.gun.id ~= "deagle" and self.guns.deagle then self:UseGun("deagle")   -- the gun as shipped too
	else
		local g = self.gun
		self.cfg.vm.cdf, self.cfg.vm.anims, self.cfg.magSize = g.cdf, g.anims, g.magSize
		self:ApplyGunStats(g)
		if self.MotionParams then self:MotionParams(g) end
	end
	self.vmArmsApplied, self.hudOk = nil, nil
	self:VmApplyArms()
	self:NativeSyncVm()
	self:Notify("Deagle settings reset")
end

function Deagle:Cfg()
	local keys = {}
	for k in pairs(self.cfg) do keys[#keys + 1] = k end
	table.sort(keys)
	for _, k in ipairs(keys) do log("cfg." .. k .. " = " .. tostring(self.cfg[k])) end
end

function Deagle:Status()
	local s = self.state
	log(string.format("status hookPlayer=%s hookInstance=%s input=%s drawn=%s ammo=%d reloading=%s",
		tostring(self.wrapped and self.wrapped.Player ~= nil), tostring(self.wrapped and self.wrapped.instance ~= nil),
		tostring(self.inputLoaded), tostring(s.drawn), s.ammo, tostring(s.reloading)))
end

-- dev: re-run this script from a loose file (tools/live.sh reload), run a Lua line. The re-run puts every setting back
-- as shipped, so the player's kept ones are set again, as a load does (the gun first: it sets the gun's own values)
Deagle.devPath = Deagle.devPath or "E:/Tools/deagle-dev/deagle_main.lua"
function Deagle:DevReload()
	local ok, err = pcall(function()
		local chunk, perr = loadfile(self.devPath)
		if not chunk then error(perr) end
		self.devLoading = true   -- the files it loads (LoadModule) come from the dev copy too
		chunk()
	end)
	self.devLoading = false
	log(ok and ("dev reload ok: " .. self.devPath) or ("$4dev reload failed: " .. tostring(err)))
	if not ok then return end
	local keys = {}
	for k in pairs(self.kept) do if k ~= "gun" then keys[#keys + 1] = k end end
	table.sort(keys)
	if self.kept.gun then table.insert(keys, 1, "gun") end
	for _, k in ipairs(keys) do self:Set(k .. " " .. self.kept[k]) end
	log("dev reload: " .. #keys .. " kept settings set again")
end

function Deagle:DevLua(line)
	local chunk, err = loadstring(unquote(line))
	if not chunk then log("$4lua: " .. tostring(err)) return end
	local ok, res = pcall(chunk)
	log("lua -> " .. tostring(ok) .. " " .. tostring(res))
end

System.AddCCommand("deagle_paths", "Deagle:Paths(Deagle.Arg([[%1]]))", "deagle_paths 1|0|clear: draw every round's path (yellow flight, red inside, cyan after a ricochet / wallbang)")
System.AddCCommand("deagle_freecam", "Deagle:Native('freecam ' .. Deagle.Arg([[%line]]))", "deagle_freecam [on|off|status|speed <m/s>|fast <x>|max <m>|keys wasd|nav|arrows|key <vk>]: free camera (time runs, Henry still aims and shoots, and doesn't walk). \\ toggles; WASD fly, Space up, Left Ctrl down, Shift faster")
System.AddCCommand("deagle_freecam_state", "Deagle:FreecamState([[%1]])", "internal: the plugin's free camera came on (1) or went off (0)")
-- deagle_points: Henry's unspent perk points (native/src/perkpoints.cpp, Claude 1). A plugin older than 2026-10-18
-- doesn't know the verb and stays silent, so say what was asked; the answer comes back as deagle_points_said.
System.AddCCommand("deagle_points", "local a = Deagle.Arg([[%line]]); System.LogAlways('[Deagle] points: asked the plugin' .. (Deagle.native and '' or ' (not linked: no answer will come)') .. ' for: ' .. (a ~= '' and a or 'the list')); Deagle:Native('points ' .. a)", "deagle_points [<skill> <n>|+n|-n]: Henry's unspent perk points; no argument lists them (e.g. deagle_points alchemy 7, deagle_points survival +2; stats are stat0-stat3)")
System.AddCCommand("deagle_points_said", "System.LogAlways('[Deagle] points: ' .. [[%line]])", "internal: the plugin's answer to deagle_points")
-- deagle_respec <skill>: one skill's perks off and their points back. The plugin lists the owned ones
-- (deagle_respec_remove <skill> <guid>...), Lua removes them (soul:RemovePerk), and 0.5 s later the plugin counts
-- what came off and refunds that many points (respec_refund).
function Deagle:RespecRemove(line)
	local words = {}
	for w in string.gmatch(Deagle.Arg(line), "%S+") do words[#words + 1] = w end
	local soul = player and player.soul
	local bad = 0
	for i = 2, #words do
		if not (soul and pcall(function() soul:RemovePerk(words[i]) end)) then bad = bad + 1 end
	end
	System.LogAlways(string.format("[Deagle] points: respec %s: RemovePerk called %d times%s; points in 0.5 s", tostring(words[1]),
		#words - 1 - bad, bad > 0 and (", " .. bad .. " calls failed") or ""))
	Script.SetTimer(500, function() Deagle:Native("respec_refund") end)   -- the next frames: let the perk list update first
end
System.AddCCommand("deagle_respec", "local a = Deagle.Arg([[%line]]); System.LogAlways('[Deagle] points: asked the plugin to reset ' .. a .. (Deagle.native and '' or ' (not linked: no answer will come)')); Deagle:Native('respec ' .. a)", "deagle_respec <skill>: that skill's perks come off and their points come back (e.g. deagle_respec alchemy)")
System.AddCCommand("deagle_respec_remove", "Deagle:RespecRemove([[%line]])", "internal: the plugin's list of perks for deagle_respec to remove")
System.AddCCommand("deagle_slowmo", "Deagle:SlowMoCmd(Deagle.Arg([[%1]]))", "deagle_slowmo [speed|off]: slow motion at that game speed (0.01-1, kept: F9 uses it too); no value = on / off like F9")
System.AddCCommand("deagle_zoom", "Deagle:SetZoom(tonumber(Deagle.Arg([[%1]])))", "deagle_zoom <factor>: debug zoom (1 / nothing = off); F2 / mouse 4 step x4, x10, off")
System.AddCCommand("deagle_toggle", "Deagle:EnsureInit() Deagle:Toggle()", "Draw / holster the Desert Eagle")

System.AddCCommand("deagle_fire", "Deagle:EnsureInit() Deagle:Fire()", "Fire one shot")
System.AddCCommand("deagle_stress", "Deagle:EnsureInit() Deagle:Stress([[%line]])", "deagle_stress [rounds a second, default 10] [sky | aim]: frame-rate test, infinite ammo, frame times in kcd.log")
System.AddCCommand("deagle_reload", "Deagle:Reload()", "Reload")
System.AddCCommand("deagle_inspect", "Deagle:Inspect()", "Play the inspect animation")
System.AddCCommand("deagle_give", "Deagle:EnsureInit() Deagle:Give()", "Add the Desert Eagle (if missing) and 49 cartridges")
System.AddCCommand("deagle_give_ammo", "Deagle:EnsureInit() Deagle:Give(Deagle.Arg([[%1]]))", "deagle_give_ammo <n>: add n cartridges")
System.AddCCommand("deagle_arms", "Deagle:Set('vm.arms ' .. (Deagle.Arg([[%1]]):find('mw') and 'mw' or 'medieval'))", "deagle_arms medieval|mw: viewmodel sleeve/glove look")
System.AddCCommand("deagle_motion", "Deagle:Set('vm.motion ' .. (tonumber(Deagle.Arg([[%1]])) or 1))", "deagle_motion <0..2>: viewmodel bob/sway strength (0 = off)")
System.AddCCommand("deagle_ads", "Deagle:SetOnOff('vm.ads', [[%1]])", "deagle_ads 1|0: aim down sights on right click")
System.AddCCommand("deagle_testnpc", "Deagle:TestNpc([[%line]])", "deagle_testnpc: the NPC you aim at takes no damage from your rounds | deagle_testnpc clear")
System.AddCCommand("deagle_testwall", "Deagle:TestWall([[%line]])", "deagle_testwall: a row of KCD2 objects, one per surface (stone, plaster, pots, wood, wicker, metal, glass, sack, straw, hay, food, leather), 4 m ahead | clear")
System.AddCCommand("deagle_shotsound", "Deagle:SetOnOff('shotSound', [[%1]])", "deagle_shotsound 0|1: gunshot sound off / on (impacts still play)")
System.AddCCommand("deagle_flash", "Deagle:FlashLookCmd(Deagle.Arg([[%1]]))", "deagle_flash ron|ours: Ready or Not's own muzzle flash or ours, both on the gun's flash cards (sets flashMode mesh; kept)")
System.AddCCommand("deagle_flashhold", "Deagle:FlashHoldCmd([[%line]])", "deagle_flashhold <variant> <stage> | next | off: show one muzzle flash card and keep it (debugging; shots leave it alone); next steps through every card")
System.AddCCommand("deagle_gunfx", "Deagle:SetOnOff('gunFx', [[%1]])", "deagle_gunfx 0|1: everything at the gun off / on (flash, smoke, light, casings, tracers, gunshot): only the impacts show")
System.AddCCommand("deagle_sounds", "Deagle:Set('sounds ' .. (Deagle.Arg([[%1]]):find('orig') and 'original' or 'ron'))", "deagle_sounds ron|original: Ready or Not's sound engine, or the gun mods' own shot and animation sounds")
System.AddCCommand("deagle_tracers", "Deagle:SetOnOff('tracers', [[%1]])", "deagle_tracers 1|0: a glowing tracer on every round")
System.AddCCommand("deagle_hud", "Deagle:SetOnOff('hud.enabled', [[%1]])", "deagle_hud 1|0: ammo HUD on/off")
System.AddCCommand("deagle_set", "Deagle:Set([[%line]])", "deagle_set <key> <value>: change a setting; kept across game starts (deagle_reset)")
System.AddCCommand("deagle_reset", "Deagle:ResetSettings()", "deagle_reset: every Deagle setting back as shipped (forgets the kept ones)")
System.AddCCommand("deagle_cfg", "Deagle:Cfg()", "Print all settings")
System.AddCCommand("deagle_status", "Deagle:EnsureInit() Deagle:Status()", "Print hook / input / ammo state")
System.AddCCommand("deagle_input", "Deagle:LoadInput()", "Reload the Deagle key bindings")
System.AddCCommand("deagle_log_actions", "Deagle.state.logActions = not Deagle.state.logActions", "Toggle logging of every input action")
System.AddCCommand("deagle_fx", "Deagle:FxTest(Deagle.Arg([[%1]]))", "deagle_fx <effect>: spawn a particle effect 3 m ahead")
System.AddCCommand("deagle_vm_respawn", "Deagle:VmRespawn()", "Rebuild the viewmodel entity")
System.AddCCommand("deagle_native_ok", "Deagle.native = true; System.LogAlways('[Deagle] native plugin linked'); if Deagle.MotionParams then Deagle:MotionParams() end; Deagle:RonWarm(); Deagle:FlyLink()", "internal: native plugin handshake")
System.AddCCommand("deagle_invtime", "Deagle:Native('invtime ' .. Deagle.Arg([[%1]]))", "deagle_invtime 1|2|0: log how long the inventory / map screen's black screen waits at each step (plugin log, invtime lines); 2 = the same plus a test that skips the clothing-loaded gate (save first); off by default")
System.AddCCommand("deagle_flykey", "Deagle:FlyKey([[%line]])", "internal: the plugin's read of a Flight mod key (<name> 0|1)")
System.AddCCommand("deagle_ron_state", "Deagle:RonState([[%1]])", "internal: Ready or Not's sound engine started in the plugin (1) or failed (0)")
System.AddCCommand("deagle_native_test", "Deagle:NativeHello() Deagle:Snd('decho/decho-1.wav', 1)", "ping the native plugin and play a shot sound")
System.AddCCommand("deagle_dev_reload", "Deagle:DevReload()", "Re-run the Deagle script from the dev path")
System.AddCCommand("deagle_fxreload", "Deagle:FxReload(Deagle.Arg([[%1]]))", "deagle_fxreload [library]: re-read Libs/Particles/<library>.xml (default deagle_impacts) from the dev folder into the running game")
System.AddCCommand("deagle_lua", "Deagle:DevLua([==[%line]==])", "deagle_lua <code>: run Lua (dev)")

Deagle:HookActions()
if not Deagle.listening then
	Deagle.listening = true
	try("event listener", function()
		UIAction.RegisterEventSystemListener(Deagle, "System", "OnGameplayStarted", "OnGameplayStarted")
	end)
end

-- handling from the gun's CS2 weapon (Scripts/Deagle/guns/cs2.lua): damage, headshots, range falloff, fire rate,
-- recoil (scaled so the Deagle keeps the kick it was tuned with), fire modes. Without one the defaults above stay.
local BASE = { damage = Deagle.cfg.damage, headshotMult = Deagle.cfg.headshotMult, interval = Deagle.cfg.interval,
	rangeMod = Deagle.cfg.rangeMod, kickPitch = Deagle.cfg.kickPitch, kickPitchVar = Deagle.cfg.kickPitchVar, kickYaw = Deagle.cfg.kickYaw }

function Deagle:ApplyGunStats(g)
	local c = self.cfg
	for k, v in pairs(BASE) do c[k] = v end
	local w = g.cs2 and Deagle.cs2 and Deagle.cs2[g.cs2]
	if not w then return end
	c.damage = w.damage
	c.headshotMult = w.head / 1.5                 -- KCD2 already multiplies head hits by 1.5
	c.interval = (c.fireRate == "mod" and g.cycle) or w.cycle
	c.burstInterval = w.burstCycle
	c.rangeMod = w.rangeMod
	c.kickPitch = BASE.kickPitch * w.recoil / 48.2   -- CS2's Deagle recoil (48.2) = the kick this mod was tuned with
	c.kickPitchVar = BASE.kickPitch * w.recoilVar / 48.2
	c.kickYaw = BASE.kickYaw * w.angleVar / 60
	if not g.modes then
		g.modes = w.auto and { "auto", "semi" } or { "semi" }
		if w.burst then g.modes[#g.modes + 1] = "burst" end
	end
end

-- switch the pistol's look: viewmodel, clips, sounds, magazine (ammo is kept, up to the new magazine size)
function Deagle:UseGun(id)
	id = id ~= "" and id or nil
	local g = id and self.guns[id]
	if not g then
		local ids = {}
		for k in pairs(self.guns) do ids[#ids + 1] = k end
		table.sort(ids)
		log((id and "$4no gun " .. id .. ". " or "") .. "guns: " .. table.concat(ids, ", ") .. " (current: " .. tostring(self.gun.id) .. ")")
		return false
	end
	self.gun, self.gunId = g, id
	self.cfg.vm.cdf, self.cfg.vm.anims, self.cfg.magSize = g.cdf, g.anims, g.magSize
	self:ApplyGunStats(g)
	if self.MotionParams then self:MotionParams(g) end
	self.state.modeIndex, self.state.trigger = 1, false
	self.state.ammo = math.min(self.state.ammo, g.magSize)
	self.vmAnim, self.vmArmsApplied = nil, nil
	if self.vmShown then
		self:VmRespawn()
	elseif self.vmId then                       -- holstered: the next draw builds the new viewmodel
		pcall(System.RemoveEntity, self.vmId); self.vmId, self.vmInFp = nil, nil
	end
	log("gun " .. id .. ": " .. tostring(g.title))
	return true
end

function Deagle:LoadGuns()
	local function load(f) return self:LoadModule(f) end
	pcall(load, "Scripts/Deagle/guns/cs2.lua")
	pcall(load, "Scripts/Deagle/guns/ron_stats.lua")   -- Ready or Not's handling numbers for the guns imported from it (ronStats)
	pcall(load, "Scripts/Deagle/guns/calibers.lua")
	pcall(load, "Scripts/Deagle/guns/penetration.lua")
	pcall(load, "Scripts/Deagle/guns/armor.lua")
	pcall(load, "Scripts/Deagle/guns/hitbones.lua")
	pcall(load, "Scripts/Deagle/motion.lua")          -- weapon movement inputs (Deagle:MotionTick / MotionParams)
	pcall(load, "Scripts/Deagle/guns/shot_gain.lua")
	pcall(load, "Scripts/Deagle/guns/index.lua")
	for _, id in ipairs(self.gunIds or {}) do pcall(load, "Scripts/Deagle/guns/" .. id .. ".lua") end
	pcall(load, "Scripts/Deagle/guns/ron_handling.lua")   -- after the guns: it adds their Ready or Not handling cues
end
Deagle:LoadModule("Scripts/Deagle/ballistics.lua")   -- rounds, ricochets, wallbangs, armour
Deagle:LoadGuns()
if Deagle.gunId and Deagle.gunId ~= "deagle" and Deagle.guns[Deagle.gunId] then Deagle:UseGun(Deagle.gunId)   -- keep it across a dev reload
else Deagle:ApplyGunStats(Deagle.gun) end
-- what the current gun's cartridge does (deagle_physics)
function Deagle:PrintPhysics()
	local ph = self:ShotPhysics()
	if not ph then log("no cartridge data for " .. tostring(self.gun.id) .. " (guns/calibers.lua)") return end
	local m = self.cfg.physics
	local hp, st = self:Wound(ph, 0)
	local hp50 = self:Wound(ph, 50)
	log(string.format("%s %s: bullet %.0f m/s %.0f J; recoil %.2f N s, %.2f m/s, %.1f J; flip %.1f rad/s -> camera %.1f deg, gun %.1f deg %.1f cm back; muzzle gas %.0f J, blast x%.2f, flash x%.2f; torso hit %.0f HP (%.0f at 50 m), head %.0f, stamina %.0f",
		self.gun.id, ph.caliber, ph.velocity, ph.energy, ph.impulse, ph.recoilVel, ph.recoilEnergy, ph.flip, m.camPerFlip * ph.flip,
		m.gunFlipPerFlip * ph.flip, m.gunBackPerVel * ph.recoilVel * 100, ph.gasEnergy, ph.blast, m.flashPerUnit * ph.blast,
		hp, hp50, hp * m.headMult, st))
end
System.AddCCommand("deagle_physics", "Deagle:PrintPhysics()", "deagle_physics: what the current gun's cartridge does (recoil, flip, flash, energy)")
System.AddCCommand("deagle_gun", "local id = Deagle.Arg([[%1]]) if id == '' then Deagle:UseGun('') else Deagle:Set('gun ' .. id) end", "deagle_gun <id>: which gun the pistol looks / sounds like (no id: list)")

log("loaded v2.1")
