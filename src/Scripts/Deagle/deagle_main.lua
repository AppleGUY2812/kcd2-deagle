-- Desert Eagle for KCD2: a scripted semi-auto pistol shown as a first-person viewmodel (arms + gun + animations
-- converted from the MWII Deagle, GameBanana 532396, private use) with an optional native companion plugin
-- (deagle_native.asi: zero-lag placement, sounds, procedural motion).
-- Everything that touches the engine is pcall-guarded so a missing binding logs an error instead of breaking the game.

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
	muzzleSmokeFx = "deagle.muzzle_smoke",   -- smoke + barrel wisp, left in the world
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
	-- for small ones. variants x stages cards; a shot picks a variant and shows the stage its game time is in (sparks
	-- outlive the 0.035-0.04 s pop: time 0.095 s). Rotations are baked, no roll. mtl: _dim / _hot (x1/3, x3)
	flashRon = { time = 0.095, stages = 10, variants = 4, mtl = "deagle_vm_flash_ron", large = "deagle_vm_flash_ron",
		small = "deagle_vm_flash_ron_s" },
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
	portFx = "deagle.port_smoke",
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
	-- casing throw (Deagle:EjectCase): speed m/s, up = degrees above the gun's right, back = degrees rearward, spin
	-- rad/s end over end. Lands 1.7-2.7 m away standing (real pistols: 1-3 m). Was 1.75 m/s at ~52 deg up: 0.8 m, and a
	-- slow-motion hang at the top. KCD2's physics caps a rigid body's spin at 20 rad/s (measured live): the plugin lifts
	-- a casing's cap to spinCap for its flight (casespin), back to 20 at its first bounce
	eject = { speed = { 2.8, 4.2 }, up = { 20, 40 }, back = { 5, 25 }, spin = { 100, 200 }, spinCap = 300 },
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
local HOLDS_AIM = { draw = true, drawEmpty = true, holster = true, holsterEmpty = true, reload = true, reloadEmpty = true }   -- aim waits for these; shots and inspect it cuts
local DRAWING = { draw = true, drawEmpty = true }        -- no shooting, reloading or inspecting until the gun is up
local HOLDS_TOGGLE = { draw = true, drawEmpty = true, reload = true, reloadEmpty = true }   -- a holster asked for meanwhile waits
local SHOTS = { shoot = true, shootEmpty = true, shootBurst = true }
local INSPECTS = { inspect = true, inspectEmpty = true }
local GAME_MAPS_OFF = { "combat" }   -- KCD2 action maps on our keys, off while the gun is out (the torch: OnAction)

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

local PLUGIN_FX = { deagle_impacts = true, deagle_blood = true }
-- per-hit variety (user 2026-10-14: the sparks "look the same" every hit): the effect's axis tilts up to `tilt` deg at a
-- random heading, and `jets` extra `jet` emitters point along the surface at random headings, up to `lift` deg off it
local FX_VARY = {
	["deagle_impacts.metal_v2"] = { tilt = 20, jet = "deagle_impacts.metal_v2_jet", jets = { 1, 3 }, lift = 20 },
}
local LOOK_NEW = { ["deagle_impacts.water"] = "deagle_impacts.water_v3", ["deagle_impacts.metal"] = "deagle_impacts.metal_v2" }
function Deagle:SpawnFx(name, pos, normal, scale)
	if not name then return end
	if self.cfg.impactLook == "new" and LOOK_NEW[name] then name = LOOK_NEW[name] end
	if name == self.cfg.muzzleFx then scale = self.cfg.muzzleFxScale end
	local vary = FX_VARY[name]
	if vary and pos and normal then
		local a = math.abs(normal.z) < 0.9 and { x = 0, y = 0, z = 1 } or { x = 1, y = 0, z = 0 }
		local u = vnorm(vcross(normal, a))
		local v = vcross(normal, u)
		for _ = 1, math.random(vary.jets[1], vary.jets[2]) do
			local h, e = math.random() * 2 * math.pi, math.rad(math.random() * vary.lift)
			local along = vadd(vscale(u, math.cos(h)), vscale(v, math.sin(h)))
			self:SpawnFx(vary.jet, pos, vadd(vscale(along, math.cos(e)), vscale(normal, math.sin(e))), scale)
		end
		normal = coneDir(normal, vary.tilt)
	end
	local lib = string.match(name, "^([%w_]+)%.")
	if self.cfg.impactSpawn == "plugin" and self.native and PLUGIN_FX[lib] and pos and normal then
		-- fxs <effect> <scale> <box length m along the normal> <pos> <dir>: drawn from its first frame
		self:Native(string.format("fxs %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", name, scale or 1.0, 0.3,
			pos.x, pos.y, pos.z, normal.x, normal.y, normal.z))
		return
	end
	try("fx " .. name, Particle.SpawnEffect, name, pos, normal, scale or 1.0)
end

-- The flash rides the gun: the plugin spawns it the way Particle.SpawnEffect does (a free emitter) and moves it onto
-- the muzzle right after placing the gun in every camera step, keeping its bounding box valid (the engine's move
-- empties it; see native/src/main.cpp), so it sits on the muzzle in every frame drawn. Lua sends the muzzle (position + barrel direction in the viewmodel's axes) at the
-- shot and every frame while it burns. Checked in game 2026-10-12 (slow motion, scripted turns, recorded): effects on
-- entities (bone attachments, viewmodel slots, a helper entity) drew on some shots only, never on the Deagle, and
-- trailed by a frame. flashMode "world" leaves the flash where the muzzle was instead. No fallback: without the plugin
-- the flash is an error in the log. Smoke stays in the world.
local FLASH_FOLLOW = 0.3   -- s of game time the flash entity keeps following the muzzle (the longest flash part lives 0.11)
local function vsub(a, b) return { x = a.x - b.x, y = a.y - b.y, z = a.z - b.z } end
local function vdot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
local function entityHex(id) return string.match(tostring(id), "(%x+)%s*$") or "0" end

function Deagle:MuzzleFlash(dir)
	local c = self.cfg
	local pos = self:MuzzlePos()
	local phys = self:ShotPhysics()
	local scale = (phys and c.physics.flashPerUnit * phys.blast or 1) * c.muzzleFxScale
	-- A bigger blast burns a little longer (the engine can't stretch a flash per shot, so two versions of it). Neither
	-- can be shorter than ~0.08 s: a new emitter is first drawn a frame or two late (tools/check_fx.py).
	local fx = (phys and phys.blast >= c.physics.largeFlashBlast) and c.muzzleFxLarge or c.muzzleFx
	if c.flashMode == "world" then
		try("fx", Particle.SpawnEffect, fx, pos, dir, scale)
	elseif c.flashMode == "mesh" and self.vmInFp and self.vmShown and self:FlashMeshShot() then
		local vm = self.vmId and System.GetEntity(self.vmId)
		local okM, lp, ld = false, nil, nil
		if vm then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if okM and c.flashMesh.light.mode == "ron" then self:FlashMeshLight(vm, lp, ld, fx == c.muzzleFxLarge)
		elseif okM then
			local light = (fx == c.muzzleFxLarge) and c.muzzleLightLarge or c.muzzleLight
			self:Native(string.format("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", light, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
		end
	elseif c.flashMode == "live" and self.flashArmed and self.vmInFp and self.vmShown then
		-- the live flash (armed when the gun came out, FlashArm): seen in the frame it's fired, for 2 frames, 1 under
		-- 40 fps (Ready or Not: 0.035-0.04 s); its light from a short per-shot emitter on the muzzle
		local sizes = c.flashLive
		local n = (self.frameDt or 0.016) > 1 / 40 and 1 or #sizes
		self:Native("flashshot " .. table.concat(sizes, " ", 1, n))
		local vm = self.vmId and System.GetEntity(self.vmId)
		local okM, lp, ld = false, nil, nil
		if vm then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if okM then
			local light = (fx == c.muzzleFxLarge) and c.muzzleLightLarge or c.muzzleLight
			self:Native(string.format("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", light, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
		end
	elseif self.vmInFp and self.vmShown then
		local vm = self.vmId and System.GetEntity(self.vmId)
		local okM, lp, ld = false, "no viewmodel", nil
		if vm then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if not self.native then
			if not self.flashNoNativeSaid then   -- once: every shot would flood the log
				self.flashNoNativeSaid = true
				log("$4muzzle flash: the native plugin isn't running (deagle_native.asi): no flash on the gun")
			end
		elseif not okM then
			log("$4muzzle flash: no muzzle on the gun: " .. tostring(lp))
		else
			self:Native(string.format("flashfx %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", fx, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
		end
	end
	try("fx", Particle.SpawnEffect, c.muzzleSmokeFx, pos, dir, scale / 0.6)
end

-- the muzzle in the viewmodel entity's own axes: where the muzzle bone is and which way the barrel points (the bone's
-- +Y: the exporters keep the muzzle bone rigid on the barrel with +Y down the bore). GetBoneLocal turns a world
-- direction into the bone's axes, so the bone's +Y in the world is the y of each world axis turned into it.
function Deagle:MuzzleLocal(vm)
	local bone = self.gun.bones.muzzle
	local p, o = vm:GetBonePos(bone), vm:GetWorldPos()
	local X, Y, Z = vm:GetDirectionVector(0), vm:GetDirectionVector(1), vm:GetDirectionVector(2)
	local bx = vm:GetBoneLocal(bone, { x = 1, y = 0, z = 0 })
	local by = vm:GetBoneLocal(bone, { x = 0, y = 1, z = 0 })
	local bz = vm:GetBoneLocal(bone, { x = 0, y = 0, z = 1 })
	local d, r = { x = bx.y, y = by.y, z = bz.y }, vsub(p, o)
	return { x = vdot(r, X), y = vdot(r, Y), z = vdot(r, Z) }, vnorm({ x = vdot(d, X), y = vdot(d, Y), z = vdot(d, Z) })
end

-- The live flash (flashMode "live"): once the gun is out and placed, the plugin keeps one never-stopping flash emitter
-- on its muzzle at a size of nothing (native/src/main.cpp "the live flash"); a shot scales it up for the frames it's
-- seen. Armed again for another gun or blast size; let go when the gun goes away.
function Deagle:FlashArm()
	local c = self.cfg
	if c.flashMode ~= "live" or not self.native or not (self.vmShown and self.vmInFp) then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or System.GetCurrTime() - (self.vmShownAt or 0) < 0.2 then return end   -- the plugin has placed the gun
	local phys = self:ShotPhysics()
	local scale = (phys and c.physics.flashPerUnit * phys.blast or 1) * c.muzzleFxScale
	local fx = (phys and phys.blast >= c.physics.largeFlashBlast) and c.muzzleFxLargeLive or c.muzzleFxLive
	local key = tostring(self.vmId) .. fx .. string.format("%.3f", scale)
	if self.flashArmed == key then return end
	local ok, lp, ld = pcall(self.MuzzleLocal, self, vm)
	if not ok then return end
	self:Native(string.format("flasharm %s %.4f %.4f %.4f %.4f %.4f %.4f %.4f", fx, scale, lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
	self.flashArmed = key
end

function Deagle:FlashDisarm()
	if self.flashArmed then self:Native("flashdisarm") end
	self.flashArmed = nil
end

-- The mesh flash: every card of both looks and both sizes is a skin attachment in the viewmodel's .cdf, built on that
-- gun's skeleton with every vertex on the muzzle bone (tools/flash_skin.sh, tools/flash_cdf.py), named after the card.
-- A .cdf's attachments show at load, so all are hidden when the viewmodel is made and at every look switch; a shot shows
-- one. As bone attachments (a model on the muzzle bone) every card drew the first card's model in KCD2, made at load or
-- at run time, even an apple put on one (in game 2026-10-14); the gun's skins (gun, magazines, rounds) draw their own.
-- the cards of the current look: ours (FLASH_SHAPES variants, each also turned FLASH_TURNS - 1 quarter turns round the
-- barrel: the roll) or Ready or Not's (flashRon: variants x stages); cdfMtl: the material the .cdf gives them
local FLASH_SHAPES, FLASH_TURNS = 4, 4
function Deagle:FlashLook()
	local c, r, phys = self.cfg.flashMesh, self.cfg.flashRon, self:ShotPhysics()
	local large = phys and phys.blast >= self.cfg.physics.largeFlashBlast
	if c.look == "ron" then
		return { look = "ron", set = large and r.large or r.small, variants = r.variants, stages = r.stages, time = r.time,
			mtl = r.mtl, cdfMtl = "deagle_vm_flash_ron" }
	end
	return { look = "ours", set = large and c.large or c.small, variants = c.roll and FLASH_SHAPES * FLASH_TURNS or FLASH_SHAPES,
		stages = 1, time = c.time, mtl = c.mtl, cdfMtl = "deagle_vm_flash" }
end
-- every card in the .cdf (both looks, both sizes): the attachments hidden at setup
function Deagle:FlashCardNames()
	local c, r, out = self.cfg.flashMesh, self.cfg.flashRon, {}
	for _, set in ipairs({ c.large, c.small }) do
		for v = 1, FLASH_SHAPES * FLASH_TURNS do out[#out + 1] = set .. "_" .. v end
	end
	for _, set in ipairs({ r.large, r.small }) do
		for v = 1, r.variants do
			for st = 1, r.stages do out[#out + 1] = set .. "_" .. v .. "_" .. st end
		end
	end
	return out
end
function Deagle:FlashMeshSetup(vm)
	local L = self:FlashLook()
	for _, name in ipairs(self:FlashCardNames()) do pcall(vm.HideAttachment, vm, 0, name, true, true) end
	local m = { names = {}, shown = {}, set = L.set, look = L }
	for v = 1, L.variants do
		for st = 1, L.stages do
			m.names[(v - 1) * L.stages + st] = L.stages > 1 and (L.set .. "_" .. v .. "_" .. st) or (L.set .. "_" .. v)
		end
	end
	self.flashMesh = m
	self:FlashMeshMaterial(vm)
end
-- another look (deagle_flash ron / ours): every card hidden, the new look's names taken; the .cdf's attachments stay
function Deagle:FlashMeshRebuild()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	self:FlashMeshSetup(vm)
end
-- another strength (flashMesh.mtl / flashRon.mtl: _dim, _hot, ...) on the current look's cards; only a card whose
-- material differs from it is changed (the .cdf's own otherwise: flashCardMtl is reset with every new viewmodel)
function Deagle:FlashMeshMaterial(vm)
	local m = self.flashMesh
	if not m or not vm then return end
	local L = self:FlashLook()
	self.flashCardMtl = self.flashCardMtl or {}
	for _, name in ipairs(m.names) do
		if (self.flashCardMtl[name] or L.cdfMtl) ~= L.mtl then
			pcall(vm.ChangeAttachmentMaterial, vm, name, "objects/manmade/weapons/deagle/vm/" .. L.mtl)
			self.flashCardMtl[name] = L.mtl
		end
	end
	m.mtl = L.mtl
end
function Deagle:FlashMeshShow(i, on)
	local vm = self.vmId and System.GetEntity(self.vmId)
	local m = self.flashMesh
	if not vm or not m or not m.names[i] then return end
	pcall(vm.HideAttachment, vm, 0, m.names[i], not on, true)
	m.shown[i] = on or nil
end
-- the card for game time e after the shot (nil once the flash is over): its variant's stage that e is in
local function flashCard(m, e)
	local L = m.look
	if e >= L.time then return nil end
	return (m.variant - 1) * L.stages + math.min(L.stages, math.floor(e / L.time * L.stages) + 1)
end
-- at the shot: a random variant, its first card seen in this frame; FlashMeshTick moves through its stages, then hides it
function Deagle:FlashMeshShot()
	local m = self.flashMesh
	if not m then return false end
	if m.mtl ~= self:FlashLook().mtl then self:FlashMeshMaterial(self.vmId and System.GetEntity(self.vmId)) end
	for i in pairs(m.shown) do self:FlashMeshShow(i, false) end
	m.variant = math.random(m.look.variants)
	m.on, m.frame, m.t0 = true, System.GetFrameID(), System.GetCurrTime()
	m.cur = flashCard(m, (self.frameDt or 0.016) / 2) or flashCard(m, 0)
	self:FlashMeshShow(m.cur, true)
	return true
end
-- every viewmodel frame after the shot's own: the flash lasts flashMesh.time (flashRon.time) of game time, so F9 slow motion stretches
-- it (it blinked by in 1-2 frames there: user 2026-10-14). A frame shows it while the frame's middle is still inside
-- that time (60 fps: 2 frames, under 40 fps: 1).
-- The shot's own frame is told by its engine frame id, not the viewmodel tick: a shot comes before its frame's tick,
-- which counted as the next frame and hid variant a before it was ever drawn (traced in game 2026-10-14)
function Deagle:FlashMeshTick()
	local m = self.flashMesh
	if not m or not m.on or System.GetFrameID() == m.frame then return end
	local want = flashCard(m, System.GetCurrTime() - m.t0 + (self.frameDt or 0.016) / 2)
	if want ~= m.cur then
		if m.cur then self:FlashMeshShow(m.cur, false) end
		if want then self:FlashMeshShow(want, true) end
		m.cur = want
	end
	if not want then m.on = nil end
end
-- Ready or Not's flash lights (flashMesh.light.mode "ron"), from its WeaponFlash_Pistol_V3 (the Deagle) and V2 (the
-- Glock) ParticleModuleLight: a main light on the burst 25 cm (V2: 10 cm) ahead of the muzzle for 0.04 s, and a second
-- one 12 (5) cm ahead for 0.035 s at 0.43 of its brightness (brightness 15-50 against 70-80); red, its hue 38250 : 450 :
-- 200. Entity lights in the viewmodel's slots: on the shot's own frame, for that game time (F9 stretches them), never
-- in the volumetric fog. Falloff: KCD2's lights fall off with the square of the distance from their bulb (light.bulb m)
-- and then fade linearly to nothing over the last 20 % of their radius: a radius that ends where the light still shows
-- draws a ring ("i can see the cutoff which shouldnt happen in real life", the user 2026-10-14; the particle light it
-- replaces ended at 1.5 m on the Glock, sized with its flash). So the radius is LIGHT_RADIUS, far past where the light
-- fades below sight (intensity 20, bulb 0.3: 1/10000 of it by 30 m), and only the square law shapes it.
local LIGHT_RADIUS = 40
local RON_LIGHTS = {
	{ slot = 3, at = 0.25, atSmall = 0.10, time = 0.04, share = 1 },
	{ slot = 4, at = 0.12, atSmall = 0.05, time = 0.035, share = 0.43 },
}
-- The colour is what the light adds on screen in Ready or Not, not its raw hue: its emitter colour (38250 : 450 : 200)
-- is red, and Ready or Not's tonemapper turns it red-orange on what it lights; KCD2's kept it red, the whole yard red
-- (the user's screenshot, 2026-10-14). Measured on the user's Ready or Not clip (frames 229 -> 231, linear light,
-- surfaces away from the fireball): added light 1 : 0.25-0.29 : 0.09-0.21.
local RON_LIGHT_HUE = { 1.0, 0.27, 0.15 }
function Deagle:FlashMeshLight(vm, lp, ld, large)
	local L = self.cfg.flashMesh.light
	local now = System.GetCurrTime()
	local phys = self:ShotPhysics()
	-- set on the Deagle; a smaller cartridge's flash lights by its blast (the muzzle gas: Glock 0.48 of the Deagle)
	local blast = phys and phys.blast / L.refBlast or 1
	self.flashLights = self.flashLights or {}
	for _, l in ipairs(RON_LIGHTS) do
		local i = L.intensity * blast * l.share
		local col = { x = i * RON_LIGHT_HUE[1], y = i * RON_LIGHT_HUE[2], z = i * RON_LIGHT_HUE[3] }
		local ok = pcall(vm.LoadLight, vm, l.slot, {
			style = 0, deferred_light = 1, radius = LIGHT_RADIUS, diffuse_color = col, specular_color = col,
			hdrdyn = 0, lifetime = 0, realtime = 1, dot3 = 1, cast_shadow = 0, attenuation_bulbsize = L.bulb,
			volumetric_fog = false, volumetric_multiplier_wh = 0,
			corona_scale = 0, corona_dist_size_factor = 1, corona_dist_intensity_factor = 1 })
		if ok then
			pcall(vm.SetSlotPos, vm, l.slot, vadd(lp, vscale(ld, large and l.at or l.atSmall)))
			self.flashLights[l.slot] = now + l.time
		end
	end
end
-- every viewmodel frame: a flash light whose game time is up goes
function Deagle:FlashLightTick()
	if not self.flashLights then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	local now = System.GetCurrTime()
	for slot, t in pairs(self.flashLights) do
		if now >= t or not vm then
			if vm then pcall(vm.FreeSlot, vm, slot) end
			self.flashLights[slot] = nil
		end
	end
end

-- every frame while a flash burns: the muzzle moves with the shot animation; afterwards the plugin lets go
function Deagle:FlashFollow()
	if not self.flashUntil then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if System.GetCurrTime() >= self.flashUntil or not vm then
		self.flashUntil = nil
		self:Native("flashoff")
		return
	end
	local ok, lp, ld = pcall(self.MuzzleLocal, self, vm)
	if not ok then log("$4muzzle flash: lost the muzzle: " .. tostring(lp)) self.flashUntil = nil self:Native("flashoff") return end
	self:Native(string.format("flashpos %.4f %.4f %.4f %.4f %.4f %.4f", lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
end

-- slow motion for checking effects (F9): KCD2 has CryEngine's t_Scale
-- Debug zoom (F2 or mouse button 4 / deagle_zoom): steps through cfg.debugZoom (x4, x10) and back to normal, to look at impacts, holes
-- and tracers far away. The gun is hidden from view while zoomed (it would fill the screen); it keeps working.
function Deagle:SetZoom(factor)
	self.zoom = factor and factor > 1 and factor or nil
	self:EaseFov(self.state.aiming)
	self:Notify(self.zoom and string.format("zoom x%g", self.zoom) or "zoom off")
end

function Deagle:CycleZoom()
	local list = self.cfg.debugZoom
	self.zoomStep = ((self.zoomStep or 0) + 1) % (#list + 1)
	self:SetZoom(list[self.zoomStep])
end

-- the game's speed now: cfg.slowmo while slow motion is on, kept to 0.01-1 (the plugin's clock and sounds stop at 0.01)
function Deagle:GameSpeed()
	if not self.slowOn then return 1 end
	return math.max(0.01, math.min(1, tonumber(self.cfg.slowmo) or 0.1))
end

function Deagle:SlowMo(on)
	self.slowOn = on and true or false
	pcall(System.SetCVar, "t_Scale", self:GameSpeed())
	self:Native(string.format("speed %.3f", self:GameSpeed()))   -- our sounds slow down with the game's
	log("slow motion " .. (on and ("x" .. tostring(self:GameSpeed())) or "off"))
end

-- deagle_slowmo [speed|off]: no value toggles it like F9; a speed is kept (F9 uses it from then on) and turns it on
function Deagle:SlowMoCmd(arg)
	if arg == "" then return self:SlowMo(not self.slowOn) end
	if arg == "off" or arg == "0" then return self:SlowMo(false) end
	local v = tonumber(arg)
	if not v or v <= 0 or v > 1 then log("usage: deagle_slowmo [speed 0.01-1 | off]") return end
	self:Set("slowmo " .. v)   -- applies it live when slow motion is on (AFTER_SET)
	if not self.slowOn then self:SlowMo(true) end
end

-- What one shot does, from the gun's cartridge and build (SI units; docs/handling-physics-design.md):
-- - recoil impulse = bullet momentum + powder gas leaving ~1.5x the bullet's speed from a pistol, 1.75x from a rifle
--   (SAAMI free-recoil formula); free recoil speed / energy = that over the gun's mass;
-- - muzzle flip = impulse x bore height, turning gun + hand around the wrist;
-- - muzzle gas energy = the powder's heat (~4.5 MJ/kg) minus what went into the bullet: it makes the flash and the bang,
--   and the blast scale G = its cube root against a 9 mm (Hopkinson-Cranz scaling of blasts and fireballs);
-- - air-drag length for the bullet's energy (range loss).
local WRIST_ARM, HAND_INERTIA = 0.10, 0.004    -- m from the wrist to the gun's centre of mass; kg m^2 of the hand
local POWDER_HEAT = 4.5e6                      -- J/kg released by double-base pistol powder (typical value)
local AIR_RHO, AIR_CD = 1.2, 0.37              -- kg/m^3; drag coefficient fitted to factory tables (~8 % speed lost by 50 yd)
local function muzzleGas(cal, v) return cal.powder * POWDER_HEAT - 0.5 * cal.bullet * v * v end
function Deagle:ShotPhysics(g)
	g = g or self.gun
	local ph = g.physics or (g.cs2 and self.gunPhysics and self.gunPhysics[g.cs2])
	local cal = ph and self.calibers and self.calibers[ph.caliber]
	if not cal then return nil end
	local v = cal.velocity * (ph.barrel / cal.barrel) ^ 0.25          -- longer barrel, faster bullet (pistol rule of thumb)
	local impulse = cal.bullet * v + cal.powder * (ph.hold == "rifle" and 1.75 or 1.5) * v
	local vr = impulse / ph.mass
	local inertia = ph.mass * WRIST_ARM * WRIST_ARM + HAND_INERTIA
	if ph.hold == "rifle" then inertia = inertia * 4 end               -- a stock in the shoulder: the body takes it
	local ref = self.calibers["9x19"]
	local gas = muzzleGas(cal, v)
	local area = math.pi * (cal.d / 2) ^ 2
	return { caliber = cal.name, bullet = cal.bullet, d = cal.d, velocity = v, energy = 0.5 * cal.bullet * v * v,
		momentum = cal.bullet * v, impulse = impulse, recoilVel = vr, recoilEnergy = 0.5 * ph.mass * vr * vr,
		inertia = inertia, flip = impulse * ph.bore / inertia, gasEnergy = gas,
		blast = (gas / muzzleGas(ref, ref.velocity)) ^ (1 / 3),
		dragLength = 2 * cal.bullet / (AIR_RHO * AIR_CD * area) }
end

-- How loud a gunshot plays: the file's own gain (tools/shot_loudness.py evens out how loud each sample was mastered)
-- times the gun's blast level against the loudest gun in the mod. Blast peak pressure grows with the cube root of the
-- muzzle-gas energy (Hopkinson-Cranz): +(20/3) log10 of the energy ratio in dB (9 mm vs .357 measured within 1 dB).
local function blastDb(phys) return (20 / 3) * math.log10(phys.gasEnergy) end
function Deagle:ShotGain(file, own)   -- own: the gain itself (Ready or Not's live engine: its mix sets the level)
	local g = own or (self.shotGain and self.shotGain[file])
	if not g then
		log("$4no loudness for " .. tostring(file) .. " (run tools/shot_loudness.py)")
		g = 1
	end
	local phys = self:ShotPhysics()
	if not phys then return g end
	local loudest = blastDb(phys)
	for _, other in pairs(self.guns) do
		local o = self:ShotPhysics(other)
		if o then loudest = math.max(loudest, blastDb(o)) end
	end
	return g * 10 ^ (self.cfg.physics.loudnessSpan * (blastDb(phys) - loudest) / 20)
end

-- Wounding from the energy the bullet leaves in the body (crush + stretch; FBI 1989, Fackler): tissue drag slows it
-- along the path, a through-and-through round carries the rest out. Health damage grows with the square root of that
-- energy (wounds grow slower than energy); one anchor: a 9 mm 124 gr FMJ at 360 m/s takes 35 HP. Stamina (the shove
-- of a hit) follows momentum. Phase 2 puts armour layers before this (BALLISTICS.md).
local TISSUE_RHO, TISSUE_CD, BODY_PATH = 1000, 0.3, 0.3   -- kg/m^3, drag in tissue (gelatin fits), torso path m
local function deposited(bullet, d, energy)
	local k = TISSUE_RHO * TISSUE_CD * math.pi * (d / 2) ^ 2 / (2 * bullet)
	return energy * (1 - math.exp(-2 * k * BODY_PATH))
end
-- energy = what reaches the body (after armour); without it, the muzzle energy less air drag over dist. Also returns
-- the energy that comes out the far side (through and through).
function Deagle:Wound(phys, dist, energy)
	local m = self.cfg.physics
	local eIn = energy or phys.energy * math.exp(-2 * (dist or 0) / phys.dragLength)
	local ref = self.calibers["9x19"]
	local eRef = deposited(ref.bullet, ref.d, 0.5 * ref.bullet * 360 * 360)
	local dep = deposited(phys.bullet, phys.d, eIn)
	local health = m.damageRef * math.sqrt(dep / eRef)
	local stamina = m.staminaRef * (math.sqrt(2 * eIn / phys.bullet) * phys.bullet) / (ref.bullet * 360)
	return health, stamina, eIn, eIn - dep
end

function Deagle:Recoil()
	local phys = self:ShotPhysics()
	if phys then return self:PhysicsRecoil(phys) end
	local p = self:Player()
	local k = self.cfg.recoil
	try("view kick", function()
		local d = System.GetViewCameraDir()
		local pitch = math.asin(math.max(-1, math.min(1, d.z)))
		local yaw = math.atan2(-d.x, d.y)
		local up = math.rad(self.cfg.kickPitch + self.cfg.kickPitchVar * (math.random() * 2 - 1)) * k * (self.state.aiming and 0.7 or 1)
		local side = math.rad(self.cfg.kickYaw) * k * (math.random() * 2 - 1)
		p.actor:PlayerSetViewAngles({ x = pitch + up, y = 0, z = yaw + side })
	end)
	try("recoil", function()
		p.actor:SetViewShake({ x = math.rad(2.2) * k, y = math.rad(0.6) * k, z = math.rad(0.4) * k },
			{ x = 0.004 * k, y = 0.02 * k, z = 0.006 * k }, 0.16, 1 / 30, 0.2)
	end)
end

-- Kick from the physics. The camera climbs by the muzzle flip and kicks sideways by a share of it (a learnable
-- pattern per gun, CS2-style: the same random numbers for the same shot of a string). The elastic hold takes back what
-- fits in its range and keeps the rest; a shot fired while the last one is still springing back finds less room, so
-- fast fire climbs. The gun model is pushed back, flipped, kicked sideways and twisted by the plugin's springs.
function Deagle:PhysicsRecoil(phys)
	local m, s = self.cfg.physics, self.state
	local ph = self.gun.physics or self.gunPhysics[self.gun.cs2]
	local aim = s.aiming and m.adsMult or 1
	local n = self.sprayN or 0; self.sprayN = n + 1
	local feltFlip, feltVel = self:FeltKick(phys)
	local up = m.camPerFlip * feltFlip * (1 + m.vary * self:SprayRand(n, 1)) * aim * self.cfg.recoil
	local side = up * ((ph.side or 0) + m.sideSigma * self:SprayRand(n, 2))
	local range = m.holdRange * m.holds[ph.hold] * (s.aiming and m.adsHold or 1)
	local r = self.camRecoil
	if not r then   -- a new string: its clock starts now (a stale one made the first frame 0.1 s: half the kick gone, no rise)
		r = { pitch = 0, yaw = 0, applied = { pitch = 0, yaw = 0 }, perm = { pitch = 0, yaw = 0 } }
		self.camRecoil, self.camRecoilLast = r, System.GetCurrTime()
	end
	r.rate = m.returnRate * math.sqrt(m.returnInertia / phys.inertia)
	local back = math.min(m.holdMax * up, math.max(0, range - r.pitch))
	r.pitch, r.perm.pitch = r.pitch + back, r.perm.pitch + up - back
	local sideBack = math.min(m.holdMax * math.abs(side), math.max(0, range * m.holdYaw - math.abs(r.yaw)))
	if side < 0 then sideBack = -sideBack end
	r.yaw, r.perm.yaw = r.yaw + sideBack, r.perm.yaw + side - sideBack
	self.camRecoilT = System.GetCurrTime()
	if self.native then
		local flip = m.gunFlipPerFlip * feltFlip * aim * self.cfg.recoil
		local yaw = up > 0 and flip * side / up or 0
		self:Native(string.format("kick %.4f %.3f %.3f %.3f", m.gunBackPerVel * feltVel * aim * self.cfg.recoil, flip, yaw, m.gunRollPerYaw * yaw))
		self.gunKicks = self.gunKicks or {}
		table.insert(self.gunKicks, { t = self.camRecoilT, pitch = flip, yaw = yaw })
	end
end

-- the muzzle flip and recoil speed as they're felt (cfg.physics.kickExponent): the reference gun keeps its numbers,
-- smaller ones are lifted toward it
function Deagle:FeltKick(phys)
	local m = self.cfg.physics
	return m.kickRefFlip * (phys.flip / m.kickRefFlip) ^ m.kickExponent, m.kickRefVel * (phys.recoilVel / m.kickRefVel) ^ m.kickExponent
end

-- the same pseudo-random number for the same gun, shot of the string and channel: a learnable spray (CS2-style).
-- Roughly normal (sum of three uniforms), clamped to +-2.
function Deagle:SprayRand(n, ch)
	local seed = 0
	for i = 1, #self.gun.id do seed = (seed * 31 + string.byte(self.gun.id, i)) % 1000003 end
	local sum = 0
	for k = 1, 3 do
		local x = math.sin(seed * 12.9898 + n * 78.233 + ch * 37.719 + k * 4.581) * 43758.5453
		sum = sum + (x - math.floor(x))
	end
	return math.max(-2, math.min(2, (sum - 1.5) * 2))
end

-- where the barrel points right now, relative to the camera (deg up, deg right): the plugin's gun-kick springs
-- (w = 28 rad/s, damping 0.89) still hold part of each recent kick; the next bullet leaves along it
local KICK_W, KICK_ZETA = 28, 50 / (2 * 28)
function Deagle:GunKickNow()
	local kicks = self.gunKicks
	if not kicks then return 0, 0 end
	local now = System.GetCurrTime()
	local wd = KICK_W * math.sqrt(1 - KICK_ZETA * KICK_ZETA)
	local p, y, keep = 0, 0, {}
	for _, k in ipairs(kicks) do
		local t = now - k.t
		if t >= 0 and t < 0.6 then   -- a kick from ahead of the clock (kept over a load) is dropped
			local f = KICK_W * math.exp(1) / wd * math.exp(-KICK_ZETA * KICK_W * t) * math.sin(wd * t)
			p, y = p + k.pitch * f, y + k.yaw * f
			keep[#keep + 1] = k
		end
	end
	self.gunKicks = keep
	return p, y
end

-- every frame: move the view by how much the recoil offset changed (never sets the aim outright)
function Deagle:CamRecoilTick()
	local r = self.camRecoil
	if not r then return end
	local now = System.GetCurrTime()
	local dt = math.min(0.1, now - (self.camRecoilLast or now)); self.camRecoilLast = now
	if dt <= 0 then return end
	local m = self.cfg.physics
	local decay = math.exp(-(r.rate or m.returnRate) * dt)
	r.pitch, r.yaw = r.pitch * decay, r.yaw * decay
	local tp, ty = r.perm.pitch + r.pitch, r.perm.yaw + r.yaw
	local k = 1 - math.exp(-m.riseRate * dt)
	local dp, dy = (tp - r.applied.pitch) * k, (ty - r.applied.yaw) * k
	if math.abs(dp) < 1e-4 and math.abs(dy) < 1e-4 then
		if math.abs(r.pitch) < 1e-3 and math.abs(r.yaw) < 1e-3 then self.camRecoil, self.sprayN = nil, 0 end   -- settled: a new string
		return
	end
	r.applied.pitch, r.applied.yaw = r.applied.pitch + dp, r.applied.yaw + dy
	local p = self:Player()
	try("view kick", function()
		local d = System.GetViewCameraDir()
		local pitch = math.asin(math.max(-1, math.min(1, d.z)))
		local yaw = math.atan2(-d.x, d.y)
		p.actor:PlayerSetViewAngles({ x = pitch + math.rad(dp), y = 0, z = yaw + math.rad(dy) })
	end)
end

-- helpers for the mod's other script files (ballistics.lua: rounds, ricochets, wallbangs, armour)
Deagle.U = { log = log, try = try, vadd = vadd, vsub = vsub, vscale = vscale, vlen = vlen, vnorm = vnorm, vcross = vcross,
	vdot = vdot, coneDir = coneDir }

-- ---------------------------------------------------------------------------
-- hits
-- ---------------------------------------------------------------------------
-- What a round does on each of KCD2's 74 surface types (Libs/MaterialEffects/SurfaceTypes.xml): our impact effect
-- (deagle_impacts.<family>, built from Ready or Not's, docs/impact_fx_build.md) and bullet hole
-- (Materials/deagle/decals/bullet_hole_<hole>), or "pass": invisible helpers (window and map barriers, player / NPC /
-- horse-only colliders, designer blockers) and foliage, which a round flies through. Worn armour (plate / mail combos)
-- sparks like metal and gets no hole (decals don't follow a moving body).
local SURFACES = {}
local function surfaces(fx, hole, names, snd) for _, n in ipairs(names) do SURFACES[n] = { fx = fx, hole = hole, snd = snd or fx } end end
surfaces("metal", "metal", { "mat_metal", "mat_super_cool_sparkly_type" })
surfaces("metal", "metal", { "mat_door_metal" }, "metal_thin")
surfaces("metal", "metal", { "mat_bells" }, "bell")
surfaces("metal", nil, { "mat_plate", "mat_chainmail", "mat_chainmail_plate", "mat_fabric_plate", "mat_fabric_chainmail",
	"mat_fabric_chainmail_plate", "mat_leather_chainmail" }, "armor")
surfaces("stone", "stone", { "mat_rock", "mat_rock_unwalk", "mat_rock_horse_ignore", "mat_stairs_stone", "mat_default", "mat_invulnerable" })
surfaces("stone", "ceramic", { "mat_ceramics" }, "ceramic")
surfaces("plaster", "plaster", { "mat_plaster" })
surfaces("wood", "wood", { "mat_wood", "mat_wood_unwalk", "mat_door_wood", "mat_shield_wood", "mat_stairs_wood" })
surfaces("wood", "wood_soft", { "mat_wood_soft", "mat_wicker" })
surfaces("soil", "soil", { "mat_soil", "mat_soil_unwalk", "mat_mud", "mat_gravel", "mat_road", "mat_field" })
surfaces("soil", "soil", { "mat_forest", "mat_grass", "mat_grassdry" }, "grass")
surfaces("water", nil, { "mat_water", "mat_mudwater", "mat_slowwater", "mat_rockwater", "mat_puddle" })
surfaces("straw", nil, { "mat_thatch", "mat_organic_stuff" })
surfaces("cloth", "cloth", { "mat_sack", "mat_fabric_items", "mat_fabric", "mat_leather" })
surfaces("glass", "glass", { "mat_glass" })
surfaces("blood", nil, { "mat_flesh" }, "flesh")
-- The "see-through" surfaces are crude hulls that block walking, not shots: KCD2 gives them pierceability 11, like its
-- invisible map barriers (SurfaceTypes.xml), and its own arrows pierce anything above 9 (Ammo/Arrow.xml). 402 models
-- use them: most forest trees (a box over the branches; the trunks have their own mat_wood proxy), fences, palisades,
-- scaffolds, ladders, vines. A round flies through them like an arrow does (in game 2026-10-13: rounds stopped in the
-- air between a coppiced beech's trunks).
surfaces("pass", nil, { "mat_railing_wood_seethrough", "mat_metal_seethrough" })
surfaces("pass", nil, { "mat_none", "mat_window", "mat_player_collider", "mat_player_only", "mat_no_photomode", "mat_npc_only",
	"mat_horse_only", "mat_horse_ignore", "mat_player_only_with_bark", "mat_level_barrier", "mat_deep_water_barrier",
	"mat_cart_slowdown", "mat_corpses_barrier", "mat_prepadeni_roadTocamp_barrier", "mat_slope", "mat_barrier_navignore",
	"mat_nodraw_designer", "mat_raycast", "mat_canopy", "mat_vegetation", "mat_vegetation_seethrough",
	"mat_vegetation_horse_ignore", "mat_bushes", "mat_grass_tall" })
-- the hit's sound: Ready or Not's Bullet Impact Surfaces event with its Surface index (RoN's physical surfaces,
-- docs/ron_fmod_events.md "Surface / Material numbers"; RoN's Soil / Mud / Leaves / Snow play nothing, so earth is Dirt
-- and plants are Grass). Picked by the user by ear from tools/sound/ron_sound_check.py (2026-10-13): stone is
-- ConcreteStrong (RoN's "Rock" and "Limestone" play its brick clatter), glass is Glass_Windshield, hay is Grass, bells
-- ring RoN's Tower Bell event.
local RON_SURFACE = {}
local function ronSurface(index, names) for _, n in ipairs(names) do RON_SURFACE[n] = index end end
ronSurface(8, { "mat_rock", "mat_rock_unwalk", "mat_rock_horse_ignore", "mat_default", "mat_invulnerable",
	"mat_stairs_stone" })                                                                                           -- ConcreteStrong
ronSurface(34, { "mat_plaster" })                                                                                   -- Plaster
ronSurface(6, { "mat_ceramics" })                                                                                   -- Ceramic
ronSurface(33, { "mat_wood", "mat_wood_unwalk", "mat_door_wood", "mat_stairs_wood" })                                 -- Pine
ronSurface(46, { "mat_wood_soft" })                                                                                 -- Treewood
ronSurface(36, { "mat_shield_wood" })                                                                               -- Plywood
ronSurface(5, { "mat_wicker" })                                                                                     -- Cardboard
ronSurface(44, { "mat_metal", "mat_super_cool_sparkly_type", "mat_door_metal", "mat_plate",
	"mat_chainmail", "mat_chainmail_plate", "mat_fabric_plate", "mat_fabric_chainmail", "mat_fabric_chainmail_plate",
	"mat_leather_chainmail" })                                                                                          -- Steel
ronSurface(9, { "mat_soil", "mat_soil_unwalk", "mat_mud", "mat_road", "mat_field", "mat_forest" })                  -- Dirt
ronSurface(21, { "mat_gravel" })                                                                                    -- Gravel
ronSurface(20, { "mat_grass", "mat_grassdry", "mat_thatch", "mat_organic_stuff" })                                  -- Grass
ronSurface(48, { "mat_water", "mat_mudwater", "mat_slowwater", "mat_rockwater", "mat_puddle" })                     -- Water
ronSurface(19, { "mat_glass" })                                                                                     -- Glass_Windshield
ronSurface(15, { "mat_fabric", "mat_fabric_items", "mat_sack" })                                                    -- Fabric_Thin
ronSurface(53, { "mat_leather" })                                                                                   -- Leather
ronSurface(16, { "mat_flesh" })                                                                                     -- Flesh
RON_SURFACE.mat_bells = "bell"                                                                                      -- Tower Bell event
-- Steel: a real plate hit instead of RoN's Steel (the user: "we need to change the steel impact sound its sounds like
-- ass"): plate A of Hickok45's Sunday Shoot-a-Round #351 (3 takes; ~22 m from his camera), its clank, early shimmer and
-- ring lifted from the recording (tools/sound/plate_rings.py, plate_sounds.py: files and vol), as loud next to RoN's MK V
-- shot as in the clip (the user's pick, pack 20 "B"). Played by the plugin like RoN's events (Falloff, travel time);
-- a plugin without "file:" sounds (deagle_native_caps) keeps RoN's Steel, ron.
-- Every surface that played RoN's Steel plays it: doors, plate and mail too (the user: "armor as a type of steel target
-- plate we have for hickock's sample").
-- near: full level within it (the plugin's Falloff), 6 m instead of impactNear: up close it was "a little too loud"
-- (the user, 2026-10-14, at the test wall 4 m out); vol halved with it, so from 6 m out it's the level the user
-- approved (plate_sounds.py: 3.37 at 3 m), at 4 m -3.5 dB, at 3 m and closer -6 dB.
local PLATE = { files = { "steel/plate_01.wav", "steel/plate_02.wav", "steel/plate_03.wav" }, vol = 1.685, near = 6, ron = 44 }
for n, v in pairs(RON_SURFACE) do if v == 44 then RON_SURFACE[n] = PLATE end end
for n, sf in pairs(SURFACES) do sf.ron = RON_SURFACE[n] end
Deagle.surfaces = SURFACES

local function surfaceName(hit)
	local name = ""
	pcall(function() name = System.GetSurfaceTypeNameById(hit.surface) or "" end)
	return name
end
Deagle.U.surfaceName = surfaceName

-- Body part from where the ray hit, relative to the target's feet and facing (human skeletons only).
local function bodyPartFor(e, pos)
	if not (e.actor and e.human) then return nil end
	local base = e:GetWorldPos()
	local h = pos.z - base.z
	if h > 1.5 then return "head" end
	local fwd = e:GetDirectionVector(1)
	local right = vnorm(vcross(fwd, { x = 0, y = 0, z = 1 }))
	local side = (pos.x - base.x) * right.x + (pos.y - base.y) * right.y
	if h < 0.9 then return side >= 0 and "leg_right" or "leg_left" end
	if math.abs(side) > 0.2 then return side >= 0 and "arm_right" or "arm_left" end
	return "torso"
end

-- first entry of a ray (origin o, unit direction d) into a sphere / a capsule from pa to pb (radius r): distance
-- along the ray, 0 when it starts inside, nil when it misses (the capsule: nearest of its body and its two end spheres)
local function raySphere(o, d, c, r)
	local oc = vsub(o, c)
	local b, cc = vdot(d, oc), vdot(oc, oc) - r * r
	if cc <= 0 then return 0 end
	local h = b * b - cc
	if h < 0 then return nil end
	local t = -b - math.sqrt(h)
	return t >= 0 and t or nil
end
local function rayCapsule(o, d, pa, pb, r)
	local best
	local function take(t) if t and (not best or t < best) then best = t end end
	local ba, oa = vsub(pb, pa), vsub(o, pa)
	local baba = vdot(ba, ba)
	if baba > 1e-10 then
		local bard, baoa = vdot(ba, d), vdot(ba, oa)
		local a = baba - bard * bard
		if a > 1e-10 then
			local b = baba * vdot(d, oa) - baoa * bard
			local cc = baba * vdot(oa, oa) - baoa * baoa - r * r * baba
			local h = b * b - a * cc
			if h >= 0 then
				local t = (-b - math.sqrt(h)) / a
				local y = baoa + t * bard
				if t >= 0 and y > 0 and y < baba then take(t) end
			end
		end
		take(raySphere(o, d, pb, r))
	end
	take(raySphere(o, d, pa, r))
	return best
end

-- Where a round really hits a person. Rays only report the living capsule round an NPC (checked live: surface
-- mat_flesh, no part), so the round's line is tested against the body's own capsules (guns/hitbones.lua: KCD2's
-- ragdoll proxies measured from male.chr, placed on the live skeleton by GetBonePos): the first one it enters. Torso
-- capsules use their outer radius (the chest's flat sides), limbs and head their fitted one. Returns the capsule
-- entry, its point and the outward normal there; false when the line misses every capsule (past an arm, between the
-- legs: the round flies on); nil when the skeleton can't be read (not a human).
function Deagle:BoneHit(e, from, dir)
	if not (self.hitBones and e.human and e.GetBonePos) then return nil end
	local cache, read = {}, 0
	local function bone(name)
		local p = cache[name]
		if p == nil then
			local ok, v = pcall(e.GetBonePos, e, name)
			p = ok and type(v) == "table" and v.x and v or false
			if p then read = read + 1 end
			cache[name] = p
		end
		return p or nil
	end
	local best, bestT, bestR, pa0, pb0
	for _, c in ipairs(self.hitBones) do
		local a, b = bone(c.a), bone(c.b)
		if a and b then
			local ab = vsub(b, a)
			local pa, pb = vadd(a, vscale(ab, c.t0)), vadd(a, vscale(ab, c.t1))
			local r = c.zone == "torso" and c.rMax or c.r
			local t = rayCapsule(from, dir, pa, pb, r)
			if t and t >= 0 and (not bestT or t < bestT - 1e-4 or (math.abs(t - bestT) <= 1e-4 and r < bestR)) then
				best, bestT, bestR, pa0, pb0 = c, t, r, pa, pb
			end
		end
	end
	if read == 0 then return nil end
	if not best then return false end
	local pos = vadd(from, vscale(dir, bestT))
	local ab = vsub(pb0, pa0)
	local k = vdot(ab, ab) > 1e-10 and math.max(0, math.min(1, vdot(vsub(pos, pa0), ab) / vdot(ab, ab))) or 0
	local normal = vnorm(vsub(pos, vadd(pa0, vscale(ab, k))))
	return best, pos, normal
end

-- Blood on what's behind the target and under it, Ready or Not's way: a spray on a wall within splatRange (3 m) behind
-- when the round came out the back (a burst behind a head), drips on the floor under the wound. Their decals at natural
-- size (m, docs/impact_fx_build.md section 6) x the round's impact scale, +-15 %, turned at random.
local SPATTER = {
	wall = { { "blood_spatter_1", 0.45 }, { "blood_spatter_2", 0.5 }, { "blood_spatter_3", 0.4 } },
	head = { { "blood_spatter_5", 1.0 } },
	floor = { { "blood_spatter_6", 0.18 }, { "blood_spatter_7", 0.18 } },
}
function Deagle:Splatter(e, hit, dir, scale, exits)
	local c = self.cfg
	local function decal(at, set)
		local d = set[math.random(#set)]
		local size = d[2] * scale * (0.85 + 0.3 * math.random())
		try("blood decal", Particle.CreateMatDecal, vadd(at.pos, vscale(at.normal, 0.01)), at.normal, size, c.splatLife,
			"Materials/deagle/decals/" .. d[1], math.random() * 360, dir)
	end
	local hits = {}
	if exits then
		local ok, n = pcall(Physics.RayWorldIntersection, vadd(hit.pos, vscale(dir, 0.15)), vscale(dir, c.splatRange), 1, ent_all, e.id, self:Player().id, hits)
		if ok and n and n > 0 and hits[1] then decal(hits[1], hit.bodyPart == "head" and SPATTER.head or SPATTER.wall) end
	end
	hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, hit.pos, { x = dir.x * 0.4, y = dir.y * 0.4, z = -2.5 }, 1, ent_static + ent_terrain, e.id, nil, hits)
	if ok and n and n > 0 and hits[1] then decal(hits[1], SPATTER.floor) end
end

-- A wound's blood (deagle_blood.xml, Ready or Not's): the entry splash back toward the shooter (with a cloth tuft when
-- the round came through padding / mail / plate), the exit spray along the round only when it came out the back (a
-- burst out of the back of a head), blood on the wall behind and the floor, KCD2's blood paint and bleeding.
function Deagle:Bleed(e, hit, dir, exits, scale, padded)
	local c = self.cfg
	local part = hit.bodyPart
	scale = scale or 1
	exits = exits ~= false
	self:SpawnFx(padded and c.bloodArmorFx or c.bloodFx, hit.pos, hit.normal or vscale(dir, -1), scale)
	if exits then
		self:SpawnFx(part == "head" and c.headFx or c.bloodExitFx, vadd(hit.pos, vscale(dir, part == "head" and 0.15 or 0.25)), dir, scale)
	end
	self:Splatter(e, hit, dir, scale, exits)
	if part and c.bloodPaint > 0 then
		try("paint blood", function() e.actor:AddBlood(c.bloodPaint, part) end)
	end
	local dead = false
	pcall(function() dead = e.actor:IsDead() or e.actor:GetHealth() <= 0 end)
	if c.bleeding and not dead then
		try("bleeding", function() e.soul:AddBuff(c.bleedingBuff) end)
	end
end

-- KCD2's own flinch / stagger on the one hit: Lua's DealDamage tags every hit as a fall, which has no reactions
-- (docs/kcd2_hit_reaction_re.md), so the plugin builds the game's hit-reaction event itself. Strength by the game's
-- own rule for a hit: health lost = minor injury (5), 50 or more = major (6); stamina only (a round stopped by armour):
-- under 10 tickle (2), under 50 unpleasant (3), else exhausting (4). Not on the dead or unconscious.
function Deagle:HitReact(e, hit, dir, dmg, stam)
	local c = self.cfg
	if not c.hitReactions then return end
	if not self.native then
		if not self.saidNoHitReact then
			self.saidNoHitReact = true
			log("$4hit reactions: the native plugin isn't running (deagle_native.asi): NPCs won't flinch")
		end
		return
	end
	local down = false
	pcall(function() down = e.actor:IsDead() or e.actor:GetHealth() <= 0 or (e.actor.IsUnconscious and e.actor:IsUnconscious()) end)
	if down then return end
	local flags = c.hitReactFlags
	if c.hitAI == "missile" and flags % 0x40 >= 0x20 then flags = flags - 0x20 end   -- its AI stimulus: HitAI's instead
	self:Native(string.format("hitreact 0x%s 0x%s %d %d %s %.3f %.3f %.3f %.4f %.4f %.4f %x", entityHex(e.id),
		entityHex(self:Player().id), c.hitReactType, self:HitStrength(dmg, stam), hit.bodyPart or "torso", hit.pos.x,
		hit.pos.y, hit.pos.z, dir.x, dir.y, dir.z, flags))
end
-- the game's HitReactionStrength for a hit: health lost = minor (5), cfg.hitReactMajor or more = major (6); stamina
-- only (stopped by armour): under 10 tickle (2), under 50 unpleasant (3), else exhausting (4)
function Deagle:HitStrength(dmg, stam)
	stam = stam or 0
	return dmg > 0 and (dmg >= self.cfg.hitReactMajor and 6 or 5) or (stam >= 50 and 4 or stam >= 10 and 3 or 2)
end

-- A gun hit as KCD2's behaviour trees take an arrow (cfg.hitAI "missile"): a hitReaction message of type Bullet (6) from
-- the player. In the trees (Scripts.pak AI/npc/basic/switch/handleHitReaction.xml, AI/crime/translateHitReaction.xml,
-- handleStimulusShooter.xml) Bullet is a missile hit: the player's (attack, flee, crime) if the NPC has seen him
-- (HasSeenPlayer), else a search toward the shot; either way the player's last hit is linked and witnesses get the crime
-- hit volume. Damage itself names no attacker (DealDamage's hits are type Fall, which the trees file as "unknown"). The
-- game's own Lua messages a brain this way (Scripts/Entities/AI/Horse.lua OnStartle).
local HIT_BULLET = 6   -- enum HitReactionType.Bullet (Tables.pak Libs/Tables/ai/ai_enums.xml)
-- KCD2's own XP for a ranged hit and kill (Tables.pak rpg_param.xml: SkillXPHit 1, SkillXPKill 18), to marksmanship,
-- the skill of its bows, crossbows and guns; the kill's on top of its hit
local XP_HIT, XP_KILL = 1, 18
function Deagle:ShotXP(killed)
	local p = self:Player()
	try("XP", function() p.soul:AddSkillXP("marksmanship", XP_HIT + (killed and XP_KILL or 0)) end)
end

-- A round passing a living NPC within cfg.nearMiss m of its chest: the message KCD2's engine sends for a player's arrow
-- flying by (weapon:onPlayerArrowFlyingBy {GUID, Position, Direction}; Scripts.pak AI/npc/basic/switch/switch.xml ->
-- handleArrowFlyby -> handleStimulusShooter): it names the player if it has seen him, else looks and searches toward
-- him. Once per NPC per round, never to one the round hit. Called for every flight step (ballistics.lua StepRound).
-- The NPCs come from Deagle:NearMissList, not a query per step: KCD2's GetEntitiesInSphere costs ~6 ms a call in a town
-- whatever its radius (measured live 2026-10-15: 3412 calls, 20 s; the frame rate fell while rounds flew, "when the
-- bullet falls out of range then it unlags")
-- The list keeps where each NPC's chest was; a step only asks the engine about an NPC within NEAR_CULL m of its path
-- there. Asking about every NPC in it for every step of every round (44 in town: IsDead + GetWorldPos, ~10 rounds in
-- the air) still took the frame rate from 93 to 55 (the user's test 2026-10-15: gone with nearMiss 0). The check is
-- plain numbers, no tables: building a few small tables per NPC per step still cost 2 ms a frame (every function in the
-- game timed live while the user shot into the sky: FlyBy 2.00 of the 2.31 ms the rounds added). A step whose path
-- is nowhere near the list's box (a round high in the sky) returns at once.
local NEAR_LIST_R, NEAR_LIST_EVERY = 300, 2   -- m around the player, s of game time between queries
local NEAR_CULL = 30                          -- m an NPC can move between queries (a galloping horse ~15 m/s)
local CHEST = 1.25                            -- m above an NPC's feet
-- squared distance from (px, py, pz) to the segment from (fx, fy, fz) along (sx, sy, sz), and the closest point on it
local function segDist2(px, py, pz, fx, fy, fz, sx, sy, sz, len2)
	local t = ((px - fx) * sx + (py - fy) * sy + (pz - fz) * sz) / len2
	t = t < 0 and 0 or (t > 1 and 1 or t)
	local cx, cy, cz = fx + sx * t, fy + sy * t, fz + sz * t
	return (px - cx) ^ 2 + (py - cy) ^ 2 + (pz - cz) ^ 2, cx, cy, cz
end
function Deagle:NearMissList()
	local now = System.GetCurrTime()
	local L = self.nearList
	if L and now - L.t >= 0 and now - L.t < NEAR_LIST_EVERY then return L end
	local p = self:Player()
	L = { t = now, n = 0, x0 = math.huge, y0 = math.huge, z0 = math.huge, x1 = -math.huge, y1 = -math.huge, z1 = -math.huge }
	local okP, at = pcall(p.GetWorldPos, p)
	local ok, ents = false, nil
	if okP and at then ok, ents = pcall(System.GetEntitiesInSphere, at, NEAR_LIST_R) end
	if ok and type(ents) == "table" then
		for _, e in ipairs(ents) do
			if e ~= p and e.soul and e.actor and e.this then
				local okW, w = pcall(e.GetWorldPos, e)
				if okW and w then
					local x, y, z = w.x, w.y, w.z + CHEST
					L[#L + 1] = { e = e, x = x, y = y, z = z }
					L.x0, L.y0, L.z0 = math.min(L.x0, x), math.min(L.y0, y), math.min(L.z0, z)
					L.x1, L.y1, L.z1 = math.max(L.x1, x), math.max(L.y1, y), math.max(L.z1, z)
				end
			end
		end
	end
	self.nearList = L
	return L
end
function Deagle:FlyBy(r, from)
	local near = self.cfg.nearMiss
	if not near or near <= 0 then return end
	local fx, fy, fz, tx, ty, tz = from.x, from.y, from.z, r.pos.x, r.pos.y, r.pos.z
	local sx, sy, sz = tx - fx, ty - fy, tz - fz
	local len2 = sx * sx + sy * sy + sz * sz
	if len2 < 1e-6 then return end
	local L = self:NearMissList()
	local m = near + NEAR_CULL
	local x0, x1 = math.min(fx, tx) - m, math.max(fx, tx) + m
	local y0, y1 = math.min(fy, ty) - m, math.max(fy, ty) + m
	local z0, z1 = math.min(fz, tz) - m, math.max(fz, tz) + m
	if L.x1 < x0 or L.x0 > x1 or L.y1 < y0 or L.y0 > y1 or L.z1 < z0 or L.z0 > z1 then return end
	local m2, near2 = m * m, near * near
	for i = 1, #L do
		local n = L[i]
		local x, y, z = n.x, n.y, n.z
		if x >= x0 and x <= x1 and y >= y0 and y <= y1 and z >= z0 and z <= z1
			and segDist2(x, y, z, fx, fy, fz, sx, sy, sz, len2) <= m2 then
			local e = n.e
			r.flybys = r.flybys or {}
			if not r.flybys[e.id] and not (r.hitIds and r.hitIds[e.id]) then
				local okD, dead = pcall(e.actor.IsDead, e.actor)
				local okP, at = pcall(e.GetWorldPos, e)
				if not (okD and dead) and okP and at then
					local d2, cx, cy, cz = segDist2(at.x, at.y, at.z + CHEST, fx, fy, fz, sx, sy, sz, len2)
					if d2 <= near2 then
						r.flybys[e.id] = true
						local len = math.sqrt(len2)
						try("near miss", function()
							local msg = table.MakeFromType("weapon:onPlayerArrowFlyingBy", { GUID = e.this.id,
								Position = { x = cx, y = cy, z = cz }, Direction = { x = sx / len, y = sy / len, z = sz / len } })
							XGenAIModule.SendMessageToEntityData(e.this.id, "weapon:onPlayerArrowFlyingBy", msg)
						end)
					end
				end
			end
		end
	end
end

function Deagle:HitAI(e, dmg, stam)
	if self.cfg.hitAI ~= "missile" then return end
	local p = self:Player()
	try("hit AI", function()
		local msg = table.MakeFromType("hitReaction", { attacker = p.this.id, hitType = HIT_BULLET, hitStrength = self:HitStrength(dmg, stam) })
		XGenAIModule.SendMessageToEntityData(e.this.id, "hitReaction", msg)
	end)
end

-- An NPC our round hits says what KCD2's NPCs say when a blow lands in a fight: its "got hit" bark, in its own voice
-- (metarole SKIRMISH_HITTARGET_TARGET: soldiers' and bandits' SOLDIER_DOSTAL_ZASAH_V_COMBATU and a few named people's;
-- rpg/role.xml), asked for the way KCD2's DialogUtils.RequestPlayerMonologByMetarole asks. KCD2 has no hit grunts:
-- voices.xml's v_hit_passed / v_hit_stopped / v_attack triggers are empty. Townsfolk have no such line and stay quiet.
-- (The user: "do the npcs make pain sounds", 2026-10-14.) Not the dead; one per NPC every PAIN_GAP s, so rapid fire
-- doesn't cut each line off.
local PAIN_GAP = 2.5
function Deagle:PainVoice(e)
	local hp = 1
	pcall(function() hp = e.actor:GetHealth() end)
	if not hp or hp <= 0 then return end
	local now, key = System.GetCurrTime(), tostring(e.id)
	self.painT = self.painT or {}
	local last = self.painT[key]
	if last and now >= last and now - last < PAIN_GAP then return end   -- now < last: a save load set the clock back
	self.painT[key] = now
	try("pain voice", function()
		XGenAIModule.SendMessageToEntityData(e.this.id, "dialog:monologRequest",
			table.MakeFromType("dialog:monologRequest", { metarole = "SKIRMISH_HITTARGET_TARGET" }))
	end)
end

-- A hit's sound from where it happened: Ready or Not's own event, 3D in its engine (direction). what = a RoN Surface
-- index (Bullet Impact Surfaces), a RON_HIT or ARMOR_HIT key, or one of our recordings ({ files, vol, ron }).
-- These events don't fade with FMOD's own 3D distance: RoN's game code sets PropagateDistance (m along the sound's path)
-- and the event's volume follows it (impacts measured: -3 dB at 5 m, -22 dB at 10 m, silent from 20 m; ricochets carry
-- to ~45 m). KCD2 has no portal volumes, so the path is the straight line. The plugin works it out from wherever the
-- listener is (the free camera too) and holds the sound back by its travel time (343 m/s): "PropagateDistance=@".
-- KCD2's AI hears it too (enum_sound.bullet_hit at the spot).
local SOUND_SPEED = 343
local RON_IMPACT = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces"
local RON_HIT = {
	ricochet = "event:/Surfaces/Impacts/Bullet Impacts/Ricochets",
	bell = "event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Tower Bell",
}
-- Worn armour (ballistics ArmorHit; _block: stopped or turned it). Our armour stops rounds real armour doesn't, the way
-- a steel target plate does, so it rings like the plate; a round through it makes no ring you'd hear over the gunshot
-- (the user, 2026-10-14: "armor as a type of steel target plate", "car doors it just goes through so no noticeable
-- sound"), only the flesh hit after it.
local ARMOR_HIT = { armor_helmet_block = PLATE, armor_body_block = PLATE, armor_helmet = false, armor_body = false }
Deagle.ronHitEvents = { RON_IMPACT }
for _, ev in pairs(RON_HIT) do Deagle.ronHitEvents[#Deagle.ronHitEvents + 1] = ev end
function Deagle:HitSound(what, pos, scale, vol)
	local c = self.cfg
	if ARMOR_HIT[what] ~= nil then what = ARMOR_HIT[what] end
	if self.native and what then
		local ev, params
		-- Falloff: the plugin's distance law from the listener (the free camera too), instead of RoN's PropagateDistance
		local prop = "Occlusion=0 PropagateDistance=@ Falloff=" .. c.impactNear
		if type(what) == "table" then   -- our own recordings: { files, vol, ron = RoN's Surface for an older plugin }
			if self.nativeCaps and self.nativeCaps.files then
				ev, params, vol = "file:" .. what.files[math.random(1, #what.files)], "Falloff=" .. (what.near or c.impactNear), what.vol * (vol or 1)
			else what = what.ron end
		end
		if ev then
		elseif type(what) == "number" then ev, params = RON_IMPACT, string.format("Surface=%d %s", what, prop)
		else ev, params = RON_HIT[what], prop end
		if ev then self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|%s", c.impactVolume * (vol or 1), pos.x, pos.y, pos.z, ev, params)) end
	end
	if c.hearing.on then try("AI hears the hit", XGenAIModule.ProduceSound, enum_sound.bullet_hit, pos, c.hearing.hit * (scale or 1)) end
end

-- deagle_testnpc: the NPC you aim at (within 60 m) takes no damage from our rounds (so no crime either) and a message
-- says what each hit would have done: for testing hits, armour and sounds on real NPCs. deagle_testnpc clear ends it.
-- (Spawning NPCs instead froze the game: KCD2's own CombatDebug spawner needs XGenAIModule.SpawnEntity, missing in the
-- shipped game, and an NPC spawned outside it hung the game a few minutes later; MODLOG 2026-10-13.)
function Deagle:IsTestNpc(e)
	return self.testNpcs ~= nil and e ~= nil and self.testNpcs[tostring(e.id)] == true
end
function Deagle:TestNpc(args)
	if tostring(args or ""):lower():find("clear") then
		self.testNpcs = nil
		self:Notify("test NPCs: back to normal")
		return
	end
	local pl = self:Player()
	local hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, System.GetViewCameraPos(), vscale(System.GetViewCameraDir(), 60), 1, ent_all,
		pl and pl.id, nil, hits)
	local e = ok and n and n > 0 and hits[1] and hits[1].entity
	if not (e and e.actor and e.soul) then self:Notify("deagle_testnpc: aim at an NPC (within 60 m) first") return end
	self.testNpcs = self.testNpcs or {}
	self.testNpcs[tostring(e.id)] = true
	local name = ""
	pcall(function() name = e:GetName() end)
	self:Notify("test NPC: " .. name .. " takes no damage from your rounds (deagle_testnpc clear)")
end

-- deagle_testwall: a row of KCD2's own objects 4 m ahead, one per surface type, to shoot and hear / see each impact.
-- Each is a real game model whose physics proxy has that SurfaceType (found by parsing every .cgf's material chunk and
-- its .mtl: IPL_Objects paks; scratch notes in MODLOG 2026-10-13). Static, never saved. deagle_testwall clear.
-- KCD2 has no object with mat_bells (church bells are mat_metal) and none flat in mat_wood_soft / mat_stairs_stone.
-- up = stand it up (90 deg about its X: the window pane is modelled lying flat, 1.0 x 1.44 m, to be set into a wall),
-- lift = m off the ground
local TEST_WALL = {
	{ "stone", "objects/manmade/common_decorations/streets_deco/guard_stone_wall_a.cgf" },
	{ "plaster", "objects/manmade/task_specific_props/food_processing/baking/oven_bread_a.cgf" },
	{ "pots", "objects/manmade/task_specific_props/container_making/pottery/ceramics_set_c.cgf" },
	{ "wood door", "objects/manmade/common_fixtures/doors/lvl2_door_a_static.cgf" },
	{ "wood crate", "objects/manmade/common_furniture/crates/crate_short.cgf" },
	{ "wicker", "objects/manmade/common_furniture/baskets/basket_with_lid_a.cgf" },
	{ "metal door", "objects/manmade/common_fixtures/doors/lvl4_door_b_static.cgf" },
	{ "anvil", "objects/manmade/task_specific_props/metal_industry/smithing/anvil_small.cgf" },
	{ "glass", "objects/intermediates/elements/window_glass_a.cgf", up = true, lift = 0.9 },
	{ "sack", "objects/manmade/common_furniture/sacks/sack_b.cgf" },
	{ "straw target", "objects/manmade/task_specific_props/combat/archery/target_b.cgf", lift = 0.55 },   -- origin at its middle
	{ "hay", "objects/manmade/structures/industrial/hay/hay_sheaf_a.cgf" },
	{ "vegetables", "objects/manmade/food/food/vegetable/basket_rectangular_a_turnip.cgf" },
	{ "leather", "objects/manmade/common_tools/repairkit/repairkit_armour_big.cgf" },
}
function Deagle:TestWall(args)
	for _, id in ipairs(self.testWall or {}) do if System.GetEntity(id) then System.RemoveEntity(id) end end
	self.testWall = nil
	if tostring(args or ""):lower():find("clear") then self:Notify("test wall removed") return end
	local pl = self:Player()
	if not pl then return end
	local p, d = pl:GetPos(), pl:GetWorldDir()
	local yaw = math.atan2(-d.x, d.y)
	local gap, ids, names = 1.6, {}, {}
	for i, w in ipairs(TEST_WALL) do
		local side = (i - (#TEST_WALL + 1) / 2) * gap
		local pos = { x = p.x + 4 * d.x + side * d.y, y = p.y + 4 * d.y - side * d.x, z = p.z + (w.lift or 0) }
		local ok, e = pcall(System.SpawnEntity, { class = "BasicEntity", name = "deagle_testwall_" .. i, position = pos,
			flags = ENTITY_FLAG_NO_SAVE, properties = { object_Model = w[2], Physics = { bPhysicalize = true, bRigidBody = false, Mass = 0, Density = -1 } } })
		if ok and e then
			pcall(e.SetFlags, e, ENTITY_FLAG_NO_SAVE, 0)
			pcall(e.SetAngles, e, { x = w.up and math.pi / 2 or 0, y = 0, z = yaw })   -- x first, then the yaw
			ids[#ids + 1] = e.id
			names[#names + 1] = w[1]
		else log("$4test wall: " .. w[1] .. " didn't spawn: " .. tostring(e)) end
	end
	self.testWall = ids
	local msg = "test wall, left to right: " .. table.concat(names, ", ") .. " (deagle_testwall clear)"
	log(msg); self:Notify(msg)
end

-- KCD2's AI hears the shot: its own gun_fired sound (rpg_sound.xml, the loudest a person makes; guards look around,
-- come to investigate, panic up close; docs/kcd2_ai_hearing.md), on the shooter, louder for a louder gun (x the blast
-- loudness over a 9 mm's, the same blast level the gunshot plays at)
function Deagle:ShotHeard(phys)
	local h = self.cfg.hearing
	if not h.on then return end
	local mult = h.shot
	if phys then
		local ref = self:ShotPhysics({ physics = { caliber = "9x19", mass = 0.9, barrel = 0.102, bore = 0.03, hold = "pistol" } })
		mult = mult * 10 ^ ((20 / 3) * math.log10(phys.gasEnergy / ref.gasEnergy) / 20)
	end
	try("AI hears the shot", function() XGenAIModule.ProduceSoundWUID(enum_sound.gun_fired, self:Player().this.id, mult) end)
end

-- the mark a round leaves: the surface's effect, sized by the round, and a bullet hole that stays a while
function Deagle:Mark(s, hit, dir, scale, size)
	local c = self.cfg
	if s.fx then self:SpawnFx(s.fx == "blood" and c.bloodFx or ("deagle_impacts." .. s.fx), hit.pos, hit.normal, scale) end
	if s.ron then self:HitSound(s.ron, hit.pos, scale) end
	if s.hole then
		size = (size or c.holeSize) * scale * (0.85 + 0.3 * math.random())
		try("bullet hole", Particle.CreateMatDecal, vadd(hit.pos, vscale(hit.normal, 0.005)), hit.normal, size, c.holeLife,
			"Materials/deagle/decals/bullet_hole_" .. s.hole, math.random() * 360, dir, hit.entity and hit.entity.id or nil, hit.partid)
	end
end

-- A killing round drops the body at once (KCD2 would play a death animation first and ragdoll ~1 s later,
-- docs/kcd2_gore_ragdoll_re.md) and shoves it by the round's momentum at the part it hit: a ray along the round
-- against the fresh ragdoll returns that part. punch per momentum like corpses, x cfg.gore.killPush.
function Deagle:KillShove(e, hit, dir, phys, energy)
	local g = self.cfg.gore
	if not g.ragdollOnKill then return end
	try("ragdoll on kill", function() e.actor:RagDollize() end)
	local speed = math.sqrt(2 * (energy or phys.energy) / phys.bullet)
	local push = self.cfg.physics.punchPerMomentum * phys.bullet * speed * g.killPush
	Script.SetTimer(g.pushDelay, function()
		local part, hits = -1, {}
		local ok, n = pcall(Physics.RayWorldIntersection, vsub(hit.pos, vscale(dir, 0.6)), vscale(dir, 1.2), 1, ent_all, Deagle:Player().id, nil, hits)
		if ok and n and n > 0 and hits[1] and hits[1].entity == e and hits[1].partid then part = hits[1].partid end
		try("kill shove", function() e:AddImpulse(part, hit.pos, dir, push, 1) end)
		log(string.format("kill shove %s part %s push %.0f", tostring(e:GetName()), tostring(part), push))
	end)
end

-- A round hitting something. r = the round in flight (nil for an instant ray): true when it flies on (through a body
-- or wall, or turned off armour / stone), with r moved to where it goes on from.
function Deagle:ApplyHit(hit, dir, phys, r)
	local c = self.cfg
	phys = phys or self:ShotPhysics()
	local e = hit.entity
	local p = self:Player()
	local dead, goesOn = false, false
	if e and e.actor then pcall(function() dead = e.actor:IsDead() end) end
	if e and e ~= p and (dead or (e.class and string.find(string.lower(e.class), "dead"))) then
		hit.bodyPart = bodyPartFor(e, hit.pos)
		local scale = self:ImpactScale(phys, hit.dist, hit.energy)
		self:Bleed(e, hit, dir, true, scale)
		self:HitSound(SURFACES.mat_flesh.ron, hit.pos, scale)   -- a corpse was silent (the user, 2026-10-13)
		log("corpse hit " .. tostring(e:GetName()))
	elseif e and e.flock_entity and e ~= p then
		-- a chicken, cat, rat or fish: one Boid entity of a flock, no soul. KCD2 kills it through Boid:OnHit ->
		-- Boids.OnBoidHit(flock, boid, hit), which reads pos, dir and damage (the body is shoved by dir x damage: the
		-- round's momentum). Rounds only made a world impact on it (the user, 2026-10-14: "i cant shoot chickens"). A body
		-- this small doesn't stop a round: on through it
		local scale = self:ImpactScale(phys, hit.dist, hit.energy)
		if not e.bWasHit then
			e.bWasHit = 1
			try("boid hit", function() Boids.OnBoidHit(e.flock_entity, e, { pos = hit.pos, dir = dir, damage = phys and phys.momentum or 1,
				shooterId = p.id, shooter = p }) end)   -- the shooter: KCD2's animal-kill crime checks the killer (AI/animal/animals.xml)
		end
		self:SpawnFx(c.bloodFx, hit.pos, hit.normal or vscale(dir, -1), scale)
		self:SpawnFx(c.bloodExitFx, vadd(hit.pos, vscale(dir, 0.1)), dir, scale)
		self:HitSound(SURFACES.mat_flesh.ron, hit.pos, scale)
		log("boid hit " .. tostring(e:GetName()))
		if r then r.skip, r.pos = e.id, hit.pos; goesOn = true end
	elseif e and e.soul and e ~= p then
		if c.hitMode == "hit" then
			try("CreateHit", function()
				g_gameRules:CreateHit(e.id, p.id, p.id, c.damage, 0, nil, hit.partid or -1, c.hitType, hit.pos, dir, hit.normal)
			end)
			self:Bleed(e, hit, dir)
		else
			local bone, at, normal = self:BoneHit(e, vsub(hit.pos, vscale(dir, 1.5)), dir)
			if bone == false then                       -- through the gaps of the body: on past it
				if r then r.skip, r.pos = e.id, hit.pos end
				return r ~= nil
			end
			local part
			if bone then
				part, hit.pos, hit.normal, hit.zone = bone.armor, at, normal, bone.zone
			else
				if e.human and not self.saidNoBones then
					self.saidNoBones = true
					log("$4hit zones: can't read " .. tostring(e:GetName()) .. "'s skeleton (GetBonePos): zone from the hit height")
				end
				part = bodyPartFor(e, hit.pos)
			end
			hit.bodyPart = part
			local dmg, stam, through = 0, 0, nil
			local stopped, hard, glance, padded, scale
			if phys then        -- from the cartridge: through what's worn there, then the energy left in the body
				local a = self:ArmorHit(e, hit, dir, phys, r, part)   -- ballistics.lua
				dmg, stam, through, stopped, hard, glance, padded, scale = a.dmg, a.stam, a.through, a.stopped, a.hard, a.glance, a.padded, a.scale
				goesOn = goesOn or a.goesOn
				if part == "head" then
					dmg = dmg * c.physics.headMult
					-- into the skull, dead: a Glock round through a head at 0.7 m left 31 of 100 HP (in game 2026-10-14)
					if (a.left or 0) >= c.physics.skullJPerMm2 * math.pi * (phys.d * 1000 / 2) ^ 2 then
						local hp = 100
						pcall(function() hp = e.actor:GetHealth() end)
						dmg = math.max(dmg, hp)
					end
				end
				-- a wound means more to a small body: x (a man's mass / its mass)^sizeExponent (a 2.5 kg hare: x5.7, a 25 kg
				-- dog x1.8, a 500 kg horse x0.4; KCD2 gives every creature 100 HP)
				local mass = 0
				pcall(function() mass = e:GetMass() end)
				if mass and mass > 0 then
					local k = (c.physics.bodyMass / mass) ^ c.physics.sizeExponent
					dmg, stam = dmg * k, stam * k
				end
			else                -- a gun without cartridge data: its CS2 numbers
				dmg = c.damage * (part == "head" and c.headshotMult or 1) * c.rangeMod ^ ((hit.dist or 0) / 12.7)
				stam = c.stamina
			end
			local dummy = self:IsTestNpc(e)
			if dummy then   -- deagle_testnpc: takes no damage (no crime either); says what the hit would have done
				self:Notify(string.format("test NPC: %s, %s%s, would lose %.0f health %.0f stamina", tostring(part),
					hard and "armour " or "", stopped and "stopped it" or "went through", dmg or 0, stam or 0))
			else
				local attacker = p.id
				if c.hitAI == "missile" then attacker = __null end   -- who did it is HitAI's message (no attacker: as Quick.lua's dd)
				try("DealDamage", function() e.soul:DealDamage(stam, dmg, attacker, false, part) end)
			end
			self:HitReact(e, hit, dir, dmg, stam)
			if not dummy then self:HitAI(e, dmg, stam) end
			if r then r.hitIds = r.hitIds or {}; r.hitIds[e.id] = true end
			if not dummy then self:PainVoice(e) end
			local killed = false
			pcall(function() killed = e.actor:GetHealth() <= 0 end)
			if killed and phys then self:KillShove(e, hit, dir, phys, hit.energy) end
			if not dummy then self:ShotXP(killed) end
			local exits = through and through > c.exitEnergy
			if not stopped then self:Bleed(e, hit, dir, exits == true, scale, padded) end
			if exits and r then                         -- through and through: on with what's left, past this body
				r.pos, r.vel, r.skip = vadd(hit.pos, vscale(dir, c.bodyExit)), vscale(dir, math.sqrt(2 * through / phys.bullet)), e.id
				r.dist = r.dist + c.bodyExit
				goesOn = true
			end
			local hp = "?"
			pcall(function() hp = string.format("%.1f/%.1f", e.actor:GetHealth(), e.actor:GetMaxHealth()) end)
			log(string.format("hit %s part %s dist %.1f hp %s%s%s", tostring(e:GetName()), tostring(part), hit.dist or -1, hp,
				stopped and (" stopped by " .. tostring(stopped.a.name) .. (glance and " (glanced)" or "")) or "",
				exits and string.format(", out the back with %.0f J", through) or ""))
		end
	else
		goesOn = self:Impact(hit, dir, phys, r)
	end
	-- props and corpses are shoved (dead comes from the actor: an NPC entity has no IsDead of its own, so corpses never
	-- moved); the living get KCD2's hit reaction instead
	if e and e ~= p and e.AddImpulse and (not e.soul or dead) and not e.flock_entity then
		local push = phys and c.physics.punchPerMomentum * phys.momentum or c.impulse   -- the shove follows momentum
		try("impulse", function() e:AddImpulse(hit.partid or -1, hit.pos, dir, push, 1) end)
	end
	return goesOn
end

-- ---------------------------------------------------------------------------
-- shooting, casings
-- ---------------------------------------------------------------------------
-- ---------------------------------------------------------------------------
-- tracers and debug paths (the rounds themselves fly in ballistics.lua)
-- ---------------------------------------------------------------------------
local TRACER_SPEEDS = { 250, 1000, 25 }   -- deagle_tracers.v<speed> (tools/gen_tracers.py): first, last, step

-- Debug: every round's path drawn in the world and kept until cleared (deagle_paths 1 / 0 / clear): flight yellow,
-- after a ricochet / wallbang / body cyan, inside a wall or body red, hits as small crosses (white world, red body).
-- Lines are redrawn every frame (System.DrawLine) from a list capped at cfg.debugPathsMax.
local PATH_FLY, PATH_ON, PATH_IN = { 1, 0.9, 0.1 }, { 0.2, 0.9, 1 }, { 1, 0.15, 0.1 }
Deagle.U.PATH = { fly = PATH_FLY, on = PATH_ON, inside = PATH_IN }
function Deagle:PathAdd(a, b, color)
	if not self.cfg.debugPaths then return end
	local list = self.paths or {}
	self.paths = list
	list[#list + 1] = { a = a, b = b, c = color }
	while #list > self.cfg.debugPathsMax do table.remove(list, 1) end
	-- (re)start the frame loop: none running, or one that stopped ticking (a dev reload can break a timer chain and
	-- leave its token behind: the lines were never drawn again, in game 2026-10-12)
	if not self.pathsToken or System.GetCurrTime() - (self.pathsTickT or -1e9) > 0.5 then
		local token = {}
		self.pathsToken = token
		Script.SetTimer(0, function() Deagle:PathTick(token) end)
	end
end
function Deagle:PathMark(p, color)
	for _, d in ipairs({ { x = 0.06, y = 0, z = 0 }, { x = 0, y = 0.06, z = 0 }, { x = 0, y = 0, z = 0.06 } }) do
		self:PathAdd(vsub(p, d), vadd(p, d), color)
	end
end
function Deagle:PathTick(token)
	if token ~= self.pathsToken then return end
	self.pathsTickT = System.GetCurrTime()
	if not self.cfg.debugPaths or not self.paths or #self.paths == 0 then self.pathsToken = nil; return end
	for _, l in ipairs(self.paths) do pcall(System.DrawLine, l.a, l.b, l.c[1], l.c[2], l.c[3], 1) end
	Script.SetTimer(0, function() Deagle:PathTick(token) end)
end
function Deagle:Paths(arg)
	arg = Deagle.Arg(arg)   -- here too: a command registered before a dev reload still passes it quoted
	if arg == "clear" then self.paths = {}
	else self:Set("debugPaths " .. (arg:find("0") and "0" or "1")) end
	self:Notify("bullet paths " .. (self.cfg.debugPaths and "on" or "off") .. (arg == "clear" and " (cleared)" or ""))
end

-- Tracers. cfg.tracers / deagle_tracers turns them off; Ready or Not hides your own tracers, ours show (no NPC carries
-- a gun yet).
-- "trail" (cfg.tracer.mode, the default): the path the round flies in each frame is laid with still glowing pieces
-- (cfg.tracer.fx, cfg.tracer.dash m each, living 0.12 s and fading: tools/gen_tracers.py), so the streak is exactly
-- where the round went, ricochets and wallbangs included, and ends on what it hit. The round leaves the eye; the
-- streak leaves the muzzle and joins the round's path by the point the shot is headed for (the first thing along it,
-- or 100 m out): Deagle:TrailPoint.
-- "flying": one particle that flies by itself with the round's speed, gravity and drag (deagle_tracers.v<speed>,
-- nearest 25 m/s), aimed from the muzzle at that same point; dies on the first thing it touches.
function Deagle:TracerFx(speed)
	local first, last, step = TRACER_SPEEDS[1], TRACER_SPEEDS[2], TRACER_SPEEDS[3]
	local v = math.max(first, math.min(last, step * math.floor(speed / step + 0.5)))
	return string.format("deagle_tracers.v%d", v)
end

-- at the shot: the flying tracer, or the trail's start (muzzle offset, where it joins the path) for the round to carry
function Deagle:Tracer(from, dir, phys)
	if not self.cfg.tracers then return nil end
	local muzzle = self:VmBone(self.gun.bones.muzzle)
	if not muzzle then return nil end
	local reach = 100
	local hits, p = {}, self:Player()
	local ok, n = pcall(Physics.RayWorldIntersection, from, vscale(dir, reach), 1, ent_all, p and p.id, nil, hits)
	if ok and n and n > 0 and hits[1] then reach = math.max(1, hits[1].dist) end
	if self.cfg.tracer.mode == "flying" then
		local aim = vnorm(vsub(vadd(from, vscale(dir, reach)), muzzle))
		self:SpawnFx(self:TracerFx(phys.velocity), muzzle, aim, 1.0)
		return nil
	end
	return { off = vsub(muzzle, from), reach = reach }
end

-- where the streak is drawn for a point d m along the round's path: the muzzle's offset from the eye, shrinking to
-- nothing at trail.reach
function Deagle:TrailPoint(trail, p, d)
	local w = math.max(0, 1 - d / trail.reach)
	return w > 0 and vadd(p, vscale(trail.off, w)) or p
end

-- The tracer is the round itself glowing: once a frame, a short streak (cfg.tracer.length m) ending where the round is,
-- along the way it's going. Far away it's a dot, close by a short dash; it doesn't draw the whole stretch flown in the
-- frame (that drew a laser line: a filmed tracer is a line only in the frames the camera's motion blur smears it, the
-- user's reference clip). Pieces glow about one frame (fx "auto"), the plugin spawning them so they show at once.
function Deagle:TracerBullet(r, from)
	local trail, c = r.trail, self.cfg.tracer
	if not trail or not self.cfg.tracers or r.dist < c.start then return end
	-- turned off a surface (Deflect, a corner): this frame's streak starts at the turn, not back on the old line, never
	-- reaches back past it, and the muzzle offset is over
	local turned = r.turnDist
	if r.turnPos then from, r.turnPos = r.turnPos, nil end
	local move = vsub(r.pos, from)
	local ml = vlen(move)
	local u = ml > 1e-4 and vscale(move, 1 / ml) or vnorm(r.vel)
	local l = math.min(c.length, r.dist - c.start, turned and r.dist - turned or math.huge)
	if l < 0.05 then return end
	local p1 = turned and r.pos or self:TrailPoint(trail, r.pos, r.dist)
	local p0 = turned and vsub(r.pos, vscale(u, l)) or self:TrailPoint(trail, vsub(r.pos, vscale(u, l)), r.dist - l)
	local dir = vnorm(vsub(p1, p0))
	local fx = c.fx
	if fx == "auto" then   -- glows about a frame: never shorter than the frame, or it's never drawn
		local dt = self.frameDt or 1 / 60
		fx = dt <= 1 / 50 and "deagle_tracers.trail_025" or dt <= 1 / 30 and "deagle_tracers.trail_040" or "deagle_tracers.trail_060"
	end
	local back = c.anchor == "start" and l or c.anchor == "end" and 0 or l / 2   -- where the effect sits; the end is p1
	local at = vsub(p1, vscale(dir, back))
	local restart = c.spawn == "restart" and self.native and self.nativeCaps and self.nativeCaps.fxr
	if c.spawn == "restart" and self.native and not restart and not self.fxrSaid then   -- a plugin from before fxr
		self.fxrSaid = true
		log("$4tracers: the running plugin has no fxr (one emitter per round): restart the game to load the new one")
	end
	if restart then   -- the round's own emitter, restarted here (plugin fxr)
		if not r.traceId then self.traceIds = (self.traceIds or 0) % 1000000 + 1; r.traceId = self.traceIds end
		self:Native(string.format("fxr %d %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", r.traceId, fx, l / c.dash, l, at.x, at.y, at.z, dir.x, dir.y, dir.z))
	elseif (c.spawn == "plugin" or c.spawn == "restart") and self.native then   -- seen from its first frame (plugin fxs)
		self:Native(string.format("fxs %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", fx, l / c.dash, l, at.x, at.y, at.z, dir.x, dir.y, dir.z))
	else self:SpawnFx(fx, at, dir, l / c.dash) end
end

function Deagle:MuzzlePos()
	return self:VmBone(self.gun.bones.muzzle) or vadd(System.GetViewCameraPos(), vscale(System.GetViewCameraDir(), 0.5))
end

-- casing out of the viewmodel's ejection port: random cone, speed and spin so no two take the same path; port smoke
-- Casings come from a pool: CASE_POOL of them are made once, then the oldest is picked up again (its motion stopped
-- with ResetPhysics and thrown anew). Spawning a physics entity every shot made ~10 a second at 750 rpm and left up to
-- 200 rigid bodies lying around for 20 s; now at most CASE_POOL exist.
local CASE_POOL = 12
function Deagle:CaseEntity()
	self.casePool = self.casePool or {}
	self.caseNext = (self.caseNext or 0) % CASE_POOL + 1
	local id = self.casePool[self.caseNext]
	local e = id and System.GetEntity(id)
	if e then
		e:ResetPhysics()
		return e
	end
	e = System.SpawnEntity({
		class = "BasicEntity", name = "deagle_case_" .. self.caseNext, position = { x = 0, y = 0, z = -1000 },
		flags = ENTITY_FLAG_NO_SAVE,
		properties = { object_Model = self.cfg.modelDir .. "deagle_case.cgf", Physics = { bPhysicalize = true, bRigidBody = true, bPushableByPlayers = false, Density = -1, Mass = 0.02 } },
	})
	e:SetFlags(ENTITY_FLAG_NO_SAVE, 0)   -- never into a save
	self.casePool[self.caseNext] = e.id
	return e
end

local function inRange(r) return r[1] + math.random() * (r[2] - r[1]) end
function Deagle:EjectCase()
	local port = self:VmBone(self.gun.bones.brass)
	if not port then return end
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local E = c.eject
		local case = self:CaseEntity()
		case:SetWorldPos(port)
		case:SetDirectionVector(f)   -- its long axis (the model's Y) down the bore, as it leaves the chamber
		local up, back = math.rad(inRange(E.up)), math.rad(inRange(E.back))
		local out = vadd(vscale(r, math.cos(back)), vscale(f, -math.sin(back)))
		local v = vscale(vadd(vscale(out, math.cos(up)), vscale(u, math.sin(up))), inRange(E.speed))
		local okP, pv = pcall(function() return self:Player():GetVelocity() end)
		if okP and type(pv) == "table" then v = vadd(v, pv) end   -- the brass keeps the shooter's own motion
		-- end over end: the ejector stops the case head while the extractor pulls the rim, so the mouth swings out to
		-- the right (spin about -up), with some wobble
		local w = vadd(vscale(u, -inRange(E.spin)), vadd(vscale(r, (math.random() - 0.5) * 40), vscale(f, (math.random() - 0.5) * 40)))
		if self.native then self:Native(string.format("casespin %s %.0f", entityHex(case.id), E.spinCap)) end
		case:SetVelocityEx(v, w)
		self.casesFlying = self.casesFlying or {}
		self.casesFlying[case.id] = { t = System.GetCurrTime(), bounces = 0, ev = self.gun.ron and self.gun.ron.shell }
	end)
	self:SpawnFx(c.portFx, port, System.GetViewCameraUpDir(), 1.0)
end

-- A casing's bounces: Ready or Not plays its shell event (9mmShell) on every bounce with Surface = what it landed on
-- (BulletShell, ShellBounceFMODAudio; it skips bounces under 0.5 m/s). KCD2 doesn't report physics contacts to Lua,
-- so each frame a flying casing's velocity is read: a bounce is a frame where it was falling and now isn't (or lost
-- over cfg.caseBounce.drop of its speed). The surface is a short ray down from it.
function Deagle:CaseTick()
	local flying = self.casesFlying
	if not flying or not next(flying) then return end
	local c, now = self.cfg.caseBounce, System.GetCurrTime()
	for id, f in pairs(flying) do
		local e = System.GetEntity(id)
		local ok, v = false, nil
		if e then ok, v = pcall(e.GetVelocity, e) end
		if not ok or not v or now - f.t > c.life or f.bounces >= c.max then flying[id] = nil
		else
			local speed = vlen(v)
			local was = f.v
			if was then
				local wasSpeed = vlen(was)
				local landed = was.z < -c.minSpeed and v.z > -0.25 * math.abs(was.z)
				local lost = wasSpeed > c.minSpeed and speed < wasSpeed * (1 - c.drop)
				if (landed or lost) and wasSpeed >= c.minSpeed then
					f.bounces = f.bounces + 1
					if f.bounces == 1 and self.native then self:Native(string.format("casespin %s 20", entityHex(id))) end   -- the physics' own cap on the ground
					local pos = e:GetWorldPos()
					local hits = {}
					local okR, n = pcall(Physics.RayWorldIntersection, vadd(pos, { x = 0, y = 0, z = 0.05 }), { x = 0, y = 0, z = -0.3 }, 1,
						ent_static + ent_terrain, id, nil, hits)
					local sf = okR and n and n > 0 and hits[1] and RON_SURFACE[surfaceName(hits[1])]
					if sf == "bell" then sf = 44 end   -- a casing on a bell: steel
					if sf and self.native and f.ev then
						self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|Surface=%d", c.volume, pos.x, pos.y, pos.z, f.ev, sf))
					end
				end
			end
			f.v = v
		end
	end
end

-- the gun's Ready or Not events while cfg.sounds is "ron" (nil: play the gun mod's own recordings)
function Deagle:RonSounds()
	return self.cfg.sounds == "ron" and self.gun and self.gun.ron or nil
end

-- where Henry's gun is heard from while the free camera is the listener (his head)
function Deagle:GunSoundPos()
	local p = self:Player()
	local ok, h = pcall(p.GetBonePos, p, "Head")
	if ok and type(h) == "table" then return h end
	local w = p:GetWorldPos()
	return { x = w.x, y = w.y, z = w.z + 1.6 }
end
-- a Ready or Not event at the player (handling, dry fire, aim): 3D events sit on the listener; gain x its volume. With
-- the free camera on, the listener is the camera: then it plays at Henry (it sat on the camera: the user, 2026-10-13)
function Deagle:RonHere(ev, gain)
	if not (self.native and ev) then return end
	local vol = self.cfg.vm.soundVol.ron * (gain or 1)
	if self.fc then
		local at = self:GunSoundPos()
		self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|", vol, at.x, at.y, at.z, ev))
	else self:Native(string.format("ron %.3f %s|", vol, ev)) end
end
-- RoN mixes its dry-fire clicks 37-44 dB under the shot, too quiet to hear in KCD2 (the user, 2026-10-13): each one's
-- measured level (short-term, rendered alone in RoN's engine) is raised to cfg.vm.dryDb
local DRY_DB = { ["event:/Weapons/FN57/FN57 Dry Fire"] = -40.0, ["event:/Weapons/G19/G19 Dryfire Old"] = -46.7 }
function Deagle:DryGain(ev)
	local own = DRY_DB[ev]
	return own and 10 ^ ((self.cfg.vm.dryDb - own) / 20) or 1
end

-- one shot: ray, effects, ammo. Returns true when a round was fired.
function Deagle:Shoot(force)
	local s, c = self.state, self.cfg
	local p = self:Player()
	if not p or s.reloading then return false end
	if not self:Owns() then
		s.drawn = false
		self:SetAim(false)
		self:HideInHand()
		return false
	end
	local now = System.GetCurrTime()
	if not force and now - s.lastShot < c.interval - 0.005 then return false end
	if s.ammo <= 0 then
		local ron = self:RonSounds()
		if ron and ron.dry then self:RonHere(ron.dry, self:DryGain(ron.dry)) else self:PlayTrigger(c.dryTrigger) end
		self:ShowAmmo()
		return false
	end
	s.heat = math.max(0, (s.heat or 0) - (now - s.lastShot) * c.heatDecay)
	s.lastShot = now
	s.ammo = s.ammo - 1

	local pos = System.GetViewCameraPos()
	local w = self.gun.cs2 and self.cs2 and self.cs2[self.gun.cs2]
	local dir
	if w and w.inStand then                     -- CS2's accuracy, the gun's own numbers
		local inacc, spread = self:CsAccuracy(w)
		dir = csDir(System.GetViewCameraDir(), inacc, spread)
		s.accFire = s.accFire + w.inFire
	else
		dir = coneDir(System.GetViewCameraDir(), (s.aiming and c.spreadAim or c.spreadHip) + s.heat)
	end
	-- the bullet leaves along the barrel, which may still be kicked up / sideways from the last shot
	local kp, ky = self:GunKickNow()
	if kp ~= 0 or ky ~= 0 then
		local _, _, right, upv = camBasis()
		local k = c.physics.barrelAim
		dir = vnorm(vadd(dir, vadd(vscale(upv, math.rad(kp) * k), vscale(right, math.rad(ky) * k))))
	end
	s.heat = s.heat + c.heatPerShot

	if c.gunFx then self:PlayTrigger(c.shotTrigger) end
	self:ShotHeard(self:ShotPhysics())
	if c.gunFx then self:MuzzleFlash(System.GetViewCameraDir()) end
	self:Recoil()
	if c.gunFx then self:EjectCase() end

	local phys = self:ShotPhysics()
	if phys then                    -- a round that flies (speed, drop, drag) and its tracer
		self:LaunchRound(pos, dir, phys, c.gunFx and self:Tracer(pos, dir, phys) or nil)
	else                            -- a gun without cartridge data: an instant ray with its CS2 numbers
		local hits = {}
		local ok, n = pcall(Physics.RayWorldIntersection, pos, vscale(dir, c.range), 1, ent_all, p.id, nil, hits)
		if ok and n and n > 0 and hits[1] then
			self:ApplyHit(hits[1], dir)
		elseif not ok then
			log("$4raycast failed: " .. tostring(n))
		end
	end
	if s.ammo == 0 then self:ShowAmmo() end
	return true
end

-- hip shots play the full shoot clip; aimed shots keep the ADS pose and add the recoil on top
function Deagle:Fire(force, burstRound)
	local s = self.state
	if self:VmDrawing() or self.vmHolstering then return end
	if s.reloading and (burstRound or 1) > 1 then return end   -- a burst's later rounds never cancel a reload
	if s.reloading then
		-- the new mag is in: the shot goes off the moment the gun is back up
		if s.magSeated then s.fireQueued = true return end
		-- before that, staged like Insurgency: Sandstorm: while the old mag is still in the gun a shot cancels the
		-- reload and the rounds stay; once it's out, the shot is the round in the chamber and the gun has no mag until
		-- the next reload. An empty gun has nothing to fire: the reload goes on.
		if s.ammo <= 0 then return end
		local now = System.GetCurrTime()
		local spareInView = now >= (s.reloadCancel or 0)    -- the spare is in the hand: the mags would fly in a blend
		if spareInView and not self.gun.mags then return end -- (no mag pieces to hide: only before that)
		local out = now >= (s.reloadOut or math.huge)
		self:CancelReload()
		if out and not self.cfg.cancelKeepsRounds then
			s.ammo, s.magOut = math.min(s.ammo, 1), true
		end
		if spareInView then self:VmMagsHidden(self.cfg.vm.cutBlend) end
	end
	local aimed, wasLast = s.aiming, s.ammo == 1
	if not self:Shoot(force) then return end
	self:VmRounds(self:MagRounds())          -- the next round went into the chamber
	local anims = self.cfg.vm.anims
	if burstRound and anims.shootBurst and not aimed then
		-- the burst clip shows every round: the first round starts it, the others only fire; a round that empties
		-- the gun cuts it with the last shot (slide locks back)
		if wasLast then self:VmPlay("shootEmpty", false)                 -- its cue is the gunshot
		else
			-- the burst clip's slide cycles are gun.burstGap apart: play it at the speed that lands them on the rounds
			if burstRound == 1 then self:VmPlay("shootBurst", false, (self.gun.burstGap or self.cfg.burstShotGap) / self.cfg.burstShotGap) end
			self:PlayCues("shoot")                                       -- every round's gunshot (the clip has none)
		end
	elseif aimed or (self:FireMode() ~= "semi" and anims.shootAdd) then
		-- rapid fire stacks recoil on the rest pose; a clip still playing (inspect, a reload's settle) is cut first
		if self:VmBusy() and not SHOTS[self.vmAnim.key] then self:VmPlay(self:VmRestKey(), true, 1.0) end
		self:VmRecoilAdd(wasLast)
	else
		self:VmPlay(wasLast and "shootEmpty" or "shoot", false)
	end
end

-- ---------------------------------------------------------------------------
-- inventory, ammo, reload, aim
-- ---------------------------------------------------------------------------
function Deagle:CountOf(id)
	local p = self:Player()
	local ok, n = pcall(function() return p.inventory:GetCountOfClass(id) end)
	return ok and (n or 0) or 0
end

function Deagle:Owns()
	return self:CountOf(self.cfg.itemId) > 0
end

-- SendInfoText queues messages, so clear the current one first and only call this on state changes.
function Deagle:Notify(text)
	if self:HudActive() and type(text) == "string" and text:find("^Desert Eagle  reloading") then return end
	pcall(Game.HideInfoText)
	try("info text", Game.SendInfoText, text, false)
end

function Deagle:ShowAmmo()
	local s = self.state
	if self:HudActive() then pcall(Game.HideInfoText) return end   -- the HUD shows it
	if not s.drawn then
		self:Notify("Desert Eagle  holstered")
		return
	end
	local spare = self.cfg.useAmmoItems and string.format("   (%d spare)", self:CountOf(self.cfg.ammoId)) or ""
	local tail = s.ammo == 0 and "   EMPTY - press R" or ""
	self:Notify(string.format("Desert Eagle  %d / %d%s%s", s.ammo, self.cfg.magSize, spare, tail))
end

function Deagle:Give(line)
	local p = self:Player()
	local rounds = tonumber(unquote(line)) or (self.cfg.useAmmoItems and 49 or 0)
	if not self:Owns() then try("give pistol", ItemUtils.CreateInvItem, p, self.cfg.itemId, 1) end
	if rounds > 0 then try("give ammo", ItemUtils.CreateInvItem, p, self.cfg.ammoId, rounds) end
	log("give: pistol " .. self:CountOf(self.cfg.itemId) .. ", cartridges " .. self:CountOf(self.cfg.ammoId))
end

-- A reload's clip says when its work is done (gun.times, tools/clip_times.py): the new mag seats (the ammo goes in),
-- then the gun comes back up (ready: the reload is over and a shot cuts the rest of the clip). Until the old mag leaves
-- the gun, a shot cancels the reload: the rounds you had stay, no new ones.
function Deagle:Reload()
	local s, c = self.state, self.cfg
	-- a round in the chamber stays there: a fresh mag on top of it makes magSize + 1 (Deagle 7+1, Glock 17+1)
	local capacity = c.magSize + (s.ammo > 0 and 1 or 0)
	if s.reloading or s.ammo >= capacity or self:VmDrawing() or self.vmHolstering then return end
	if c.useAmmoItems and self:CountOf(c.ammoId) <= 0 then
		self:Notify("No .50 AE cartridges")
		return
	end
	local key = s.ammo == 0 and "reloadEmpty" or "reload"
	local a = c.vm.anims[key]
	local len = a[2] / (a[3] or c.vm.fps) / self:Rate(key)
	local seat = math.min(self:ClipTime(key, "seat") or len, len)
	local ready = math.max(seat, math.min(self:ClipTime(key, "ready") or len, len))
	local now = System.GetCurrTime()
	s.reloading, s.magSeated = true, false
	s.reloadEnd = now + ready
	s.reloadCancel = now + (self:ClipTime(key, "cancel") or 0)
	s.reloadOut = now + (self:ClipTime(key, "out") or self:ClipTime(key, "cancel") or 0)
	s.reloadTarget = capacity
	local this = s.reloadEnd
	local function mine() return Deagle.state.reloading and Deagle.state.reloadEnd == this end
	self:Notify("Desert Eagle  reloading...")
	self:PlayTrigger(c.reloadTrigger)
	Script.SetTimer(math.floor(seat * 1000), function() if mine() then Deagle:SeatMag() end end)
	Script.SetTimer(math.floor(ready * 1000) + 20, function() self:FinishReload() end)
	self:VmPlay(key, false)
	self:UpdateAim()
	if key == "reloadEmpty" and self.gun.newMagAt then
		Script.SetTimer(math.floor(self.gun.newMagAt / self:Rate(key) * 1000), function()
			if mine() then Deagle:VmMagIn(); Deagle:VmRounds(Deagle.cfg.magSize) end   -- a full magazine in the hand
		end)
	end
end

-- the new magazine is in: its rounds count (taken from the inventory with useAmmoItems)
function Deagle:SeatMag()
	local s, c = self.state, self.cfg
	if s.magSeated then return end
	s.magSeated = true
	self:VmMagIn()
	local take = (s.reloadTarget or c.magSize) - s.ammo
	if c.useAmmoItems then
		take = math.min(take, self:CountOf(c.ammoId))
		if take > 0 then
			try("use ammo", function() self:Player().inventory:DeleteItemOfClass(c.ammoId, take) end)
		end
	end
	s.ammo = s.ammo + take
	self:VmRounds(self:MagRounds())
end

-- Called by the timer and polled from input, so a reload never gets stuck if timers are cleared.
function Deagle:FinishReload()
	local s = self.state
	if not s.reloading then return end
	if System.GetCurrTime() < (s.reloadEnd or 0) then return end
	self:SeatMag()
	s.reloading = false
	self:ShowAmmo()
	if s.fireQueued then                   -- pressed while the new mag went in and the gun came up
		s.fireQueued = false
		if self:FireMode() ~= "auto" or s.trigger then self:TriggerPress() end
	end
end

-- a shot while the old magazine is still in: no reload, the rounds in it stay
function Deagle:CancelReload()
	local s = self.state
	s.reloading, s.reloadEnd, s.fireQueued = false, nil, false
	self:VmRounds(self:MagRounds())
	self:ShowAmmo()
end

-- Aim input. Aiming (zoom + ADS pose) follows the button, except while a reload / draw / holster runs: those finish
-- first and hand over to ADS in their last moments. Hip shots and inspect are cut by it.
function Deagle:SetAim(on)
	self.state.aimHeld = on and self.cfg.vm.ads and self.cfg.vm.anims.ads ~= nil or false
	self:UpdateAim()
end

function Deagle:AimBlocked()
	if self.vmHolstering or not self.state.drawn then return true end
	local a = self.vmAnim
	return a and not a.loop and HOLDS_AIM[a.key] and System.GetCurrTime() < (a.ends or 0) - self.cfg.vm.adsLead or false
end

function Deagle:UpdateAim(blend)
	local s = self.state
	local blocked = self:AimBlocked()
	local want = (s.aimHeld and not blocked) and true or false
	if want ~= s.aiming then
		s.aiming = want
		self:EaseFov(want)
		if self.native then self:Native("aim " .. (want and "1" or "0")) end
		local ron = self:RonSounds()
		if ron and (want and ron.adsIn or not want and ron.adsOut) then self:RonHere(want and ron.adsIn or ron.adsOut) end
	end
	if self.vmShown and not blocked then
		self:VmPlay(self:VmRestKey(), true, 1.0, blend or (not self:VmBusy() and self.cfg.vm.adsBlend or nil))
	end
end

-- zoom in / out over adsBlend (smoothstep) instead of snapping
function Deagle:EaseFov(on)
	local s = self.state
	try("aim fov", function()
		if (on or self.zoom) and not s.baseFov then s.baseFov = System.GetViewCameraFov() end
		if not s.baseFov then return end
		local to = s.baseFov * (on and self.cfg.aimFovScale or 1) / (self.zoom or 1)
		local from, t0 = s.fovNow or s.baseFov, System.GetCurrTime()
		self.fovToken = (self.fovToken or 0) + 1
		local tok = self.fovToken
		local function step()
			if Deagle.fovToken ~= tok then return end
			local k = math.min(1, (System.GetCurrTime() - t0) / math.max(0.01, Deagle.cfg.vm.adsBlend))
			k = k * k * (3 - 2 * k)
			s.fovNow = from + (to - from) * k
			System.SetViewCameraFov(s.fovNow)
			if k < 1 then Script.SetTimer(0, step)
			elseif not on and not Deagle.zoom then s.baseFov, s.fovNow = nil, nil end   -- fully out: read the game's FOV again next time
		end
		step()
	end)
end

-- inspect only from the hip; an empty gun keeps its slide locked and shows an empty magazine (MWII-style)
function Deagle:Inspect()
	local s = self.state
	if s.aiming or s.aimHeld or not s.drawn or s.reloading or self:VmBusy() or self.vmHolstering then return end
	self:VmPlay(s.ammo == 0 and "inspectEmpty" or "inspect", false)
end

-- ---------------------------------------------------------------------------
-- viewmodel entity
-- ---------------------------------------------------------------------------
-- Older builds let the viewmodel (and test probes) into saves; on load they come back without their character, drawn
-- as the engine's default pyramid (the big white triangle). Anything of ours still in the world before the viewmodel
-- exists is one of those.
function Deagle:RemoveStaleEntities()
	if not System.GetEntitiesByClass then return end
	local n = 0
	for _, e in pairs(System.GetEntitiesByClass("BasicEntity") or {}) do
		local ok, name = pcall(e.GetName, e)
		if ok and type(name) == "string" and name:find("^deagle_") and not name:find("^deagle_case_") and e.id ~= self.vmId then
			pcall(System.RemoveEntity, e.id); n = n + 1
		end
	end
	if n > 0 then log("removed " .. n .. " stale entities from the save") end
end

function Deagle:VmEntity()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then return vm end
	local c = self.cfg.vm
	self:RemoveStaleEntities()
	vm = System.SpawnEntity({ class = "BasicEntity", name = "deagle_viewmodel", position = System.GetViewCameraPos(),
		flags = ENTITY_FLAG_NO_SAVE, properties = { object_Model = "" } })
	if not vm then log("$4viewmodel spawn failed") return nil end
	pcall(vm.SetFlags, vm, ENTITY_FLAG_NO_SAVE, 0)
	local ok, slot = pcall(vm.LoadCharacter, vm, 0, c.cdf)
	log("viewmodel character " .. c.cdf .. " -> " .. tostring(ok) .. " " .. tostring(slot))
	if not ok or slot == nil or slot == false or slot == -1 then
		local ok2, slot2 = pcall(vm.LoadCharacter, vm, 0, c.chr)
		local ok3 = pcall(vm.CreateSkinAttachment, vm, 0, "vm_mesh", c.skin)
		log("viewmodel fallback chr " .. tostring(ok2) .. " " .. tostring(slot2) .. " skin " .. tostring(ok3))
	end
	pcall(vm.SetViewDistRatio, vm, 255)
	self.vmId, self.vmRoundsShown, self.flashCardMtl, self.flashLights = vm.id, nil, {}, nil
	self:FlashMeshSetup(vm)
	vm.OnUpdate = function() Deagle:VmPlace() end    -- per-frame follow (entity update); VmTick timer as well
	pcall(vm.Activate, vm, 1)
	return vm
end

-- the rounds in the gun's magazine: one attachment per modelled round (vm_round_1 = the top one), so it shows what it holds
function Deagle:VmRounds(n)
	local vm = self.vmId and System.GetEntity(self.vmId)
	local r = self.gun.rounds
	if not vm or not r or self.vmRoundsShown == n then return end
	self.vmRoundsShown = n
	if r == true then pcall(vm.HideAttachment, vm, 0, "vm_rounds", n <= 0, n <= 0) return end   -- one piece
	for i = 1, r do pcall(vm.HideAttachment, vm, 0, "vm_round_" .. i, i > n, i > n) end
end

-- A reload cancelled mid-swap: the spare is in the hand and the old mag is out, and a blend back to the rest pose would
-- fly both across the view. Both are hidden while the hands blend, then the gun's own mag shows again, seated.
function Deagle:VmMagsHidden(t)
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or not self.gun.mags then return end
	local function show(on)
		pcall(vm.HideAttachment, vm, 0, "vm_mag2", not on, not on)
		local m1 = on and not self.state.magOut      -- a gun whose mag came out stays without one until the next reload
		pcall(vm.HideAttachment, vm, 0, "vm_mag1", not m1, not m1)
		self.vmRoundsShown = nil
		self:VmRounds(on and self:MagRounds() or 0)
	end
	show(false)
	self.magsToken = (self.magsToken or 0) + 1
	local tok = self.magsToken
	Script.SetTimer(math.floor((t + 0.05) * 1000), function() if Deagle.magsToken == tok then show(true) end end)
end

-- the gun's magazine is back (a reload after one was left out by a cancel)
function Deagle:VmMagIn()
	if not self.state.magOut then return end
	self.state.magOut = false
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.HideAttachment, vm, 0, "vm_mag1", false, false) end
end

-- rounds in the magazine: with any ammo, one of them is in the chamber
function Deagle:MagRounds()
	local a = self.state.ammo
	return a > 0 and a - 1 or 0
end

function Deagle:VmBone(name)
	local vm = self.vmShown and self.vmId and System.GetEntity(self.vmId)
	if not vm then return nil end
	local ok, p = pcall(vm.GetBonePos, vm, name)
	return ok and p or nil
end

-- rebuild the viewmodel entity from scratch
function Deagle:VmRespawn()
	if self.vmId then pcall(System.RemoveEntity, self.vmId) end
	self.vmId, self.vmInFp = nil, nil
	self:VmEntity()
	if self.vmShown then self.vmShown = false; self:VmShow() end
	log("viewmodel respawned")
end

-- arm look: "mw" (original sleeves + knuckle gloves) or "medieval" (KCD2 linen sleeves + old leather gloves,
-- tiled from the game's own Textures/cloth set)
function Deagle:VmApplyArms()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or not self.gun.armsLooks then return end
	local mtl = "objects/manmade/weapons/deagle/vm/" .. (self.cfg.vm.arms == "mw" and "deagle_vm" or "deagle_vm_medieval")
	local ok = pcall(vm.ChangeAttachmentMaterial, vm, "vm_mesh", mtl)
	log("arms look " .. self.cfg.vm.arms .. " (" .. tostring(ok) .. ")")
end

-- ---------------------------------------------------------------------------
-- Henry's body: hidden in first person while the gun is up (his shadow stays), with his worn gear; depth of field
-- off meanwhile (it focuses far and blurs the gun at 0.4 m). Dialogue / cutscene cameras get everything back.
-- ---------------------------------------------------------------------------
local GEAR_BONES = { "Hips", "Spine", "Spine1", "Spine2", "Spine3", "Spine4", "LeftUpLeg", "RightUpLeg" }

function Deagle:HideGear(hide)
	self.hiddenGear = self.hiddenGear or {}
	if not hide then
		for id in pairs(self.hiddenGear) do local e = System.GetEntity(id) if e then pcall(e.Hide, e, 0) end end
		self.hiddenGear = {}
		return
	end
	local p = self:Player()
	if not p then return end
	local pts = {}
	for _, b in ipairs(GEAR_BONES) do local ok, v = pcall(p.GetBonePos, p, b) if ok and v then pts[#pts + 1] = v end end
	local wp = p:GetWorldPos()
	for _, e in ipairs(System.GetEntitiesInSphere({ x = wp.x, y = wp.y, z = wp.z + 1.0 }, 1.2) or {}) do
		if e.class == "PickableItem" and not self.hiddenGear[e.id] then
			local ep = e:GetWorldPos()
			for _, v in ipairs(pts) do
				if vlen(vadd(ep, vscale(v, -1))) < self.cfg.vm.gearDist then
					pcall(e.Hide, e, 1); self.hiddenGear[e.id] = true
					log("hid worn item " .. tostring(e:GetName()))
					break
				end
			end
		end
	end
end

function Deagle:SetBodyHidden(hide)
	local p = self:Player()
	if p and self.bodyHidden ~= hide and (self.cfg.vm.hideBody or not hide) then
		pcall(p.HideAllAttachments, p, 0, hide, false)
		pcall(p.HideAttachmentMaster, p, 0, hide)
		self.bodyHidden = hide
		log("body " .. (hide and "hidden" or "shown"))
	end
	self:HideGear(hide and self.cfg.vm.hideBody)
	if hide then
		if self.savedDof == nil then
			local ok, v = pcall(System.GetCVar, "r_DepthOfField")
			self.savedDof = ok and v or 2
		end
		pcall(System.SetCVar, "r_DepthOfField", 0)
	elseif self.savedDof ~= nil then
		pcall(System.SetCVar, "r_DepthOfField", self.savedDof)
		self.savedDof = nil
	end
end

-- ---------------------------------------------------------------------------
-- per frame: first-person guard, placement on the camera (the plugin does it without lag when linked), HUD
-- ---------------------------------------------------------------------------
function Deagle:VmFollow()
	local p = self:Player()
	if not p or not self.vmShown then return end
	local wasFp = self.vmInFp
	local now = System.GetCurrTime()
	local cp, f = System.GetViewCameraPos(), System.GetViewCameraDir()
	local ok, hp = pcall(p.GetBonePos, p, "Head")
	local fp = ok and hp and vlen(vadd(cp, vscale(hp, -1))) < self.cfg.vm.fpMaxDist
	if fp ~= self.vmInFp then
		self.vmInFp = fp
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then pcall(vm.Hide, vm, fp and 0 or 1) end
		self:SetBodyHidden(fp and true or false)
		log("first person " .. tostring(fp))
	end
	if fp and (now - (self.gearScan or 0)) > 1.0 then self.gearScan = now; self:HideGear(true) end
	-- the plugin places the viewmodel on the final camera; Lua only does it without the plugin (or the first frame)
	if self.vmInFp and not (self.native and wasFp) then
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then
			local okU, up = pcall(System.GetViewCameraUpDir)
			if not okU or not up then up = { x = 0, y = 0, z = 1 } end
			local r = vnorm(vcross(f, up))   -- full camera orientation incl. the head-sway roll
			local u = vcross(r, f)
			local o = self.cfg.vm.offset
			vm:SetWorldPos(vadd(cp, vadd(vscale(f, o.fwd), vadd(vscale(r, o.right), vscale(u, o.up)))))
			vm:SetWorldAngles(matToAngles({ { r.x, f.x, u.x }, { r.y, f.y, u.y }, { r.z, f.z, u.z } }))
		end
	end
	if self.vmInFp ~= wasFp then self:NativeSyncVm() end
end

function Deagle:VmPlace()
	self:VmFollow()
	self:CamRecoilTick()
	local now = System.GetCurrTime()
	if now ~= self.hudT then self.hudT = now; self:DrawHud() end
end

-- While the gun is out the mod owns our keys: KCD2's own maps on them ("combat": left click punches;
-- "draw_holster_torch": R takes the torch out) are held off every frame. Switching them off once at H wasn't enough:
-- a gun drawn another way (gun switch, save load, script reload) skipped it, and the game turns its maps back on
-- after some state changes (R still took the torch out, user 2026-10-12).
function Deagle:HoldGameMapsOff()
	if not self.state.drawn then return end
	for _, map in ipairs(GAME_MAPS_OFF) do ActionMapManager.EnableActionMap(map, false) end
end

function Deagle:VmTick(token)
	if token ~= self.vmToken or not self.vmShown then return end
	local now = System.GetCurrTime()
	if self.vmTickT and now > self.vmTickT then self.frameDt = math.min(0.1, now - self.vmTickT) end   -- a round's first step
	self.vmTickT = now
	self.vmFrame = (self.vmFrame or 0) + 1
	self:VmPlace()
	self:FlashArm()
	self:FlashMeshTick()
	self:CueTick()
	self:EnvTick()
	self:CaseTick()
	self:FlashLightTick()
	if self.MotionTick then self:MotionTick() end
	self:HoldGameMapsOff()
	if (self.zoom ~= nil) ~= (self.vmZoomHidden == true) then     -- the gun out of view while the debug zoom is on
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then pcall(vm.Hide, vm, self.zoom and 1 or 0) end
		self.vmZoomHidden = self.zoom ~= nil
	end
	self:FlashFollow()
	local a = self.vmAnim
	if a and not a.loop and System.GetCurrTime() >= a.ends and not self.vmHolstering then self:VmReturn() end
	Script.SetTimer(0, function() Deagle:VmTick(token) end)
end

local function hudText(x, y, text, size, c)
	return pcall(System.DrawText, x, y, text, Deagle.cfg.hud.font, size, 0, c[1], c[2], c[3], c[4])
end

function Deagle:HudActive()
	return self.cfg.hud.enabled and self.hudOk
end

function Deagle:DrawHud()
	local h, s = self.cfg.hud, self.state
	if not (h.enabled and self.hudOk ~= false and s.drawn and self.vmInFp) then return end
	local ok, vx, vy, vw, vh = pcall(System.GetViewport)
	local W = (ok and type(vw) == "number" and vw > 0) and vw or 1920
	local H = (ok and type(vh) == "number" and vh > 0) and vh or 1080
	local x, y = h.x * W, h.y * H
	local col = s.ammo == 0 and h.empty or h.col
	local good = hudText(x, y, tostring(s.ammo), h.size * 1.6, col)
	if not good then
		self.hudOk = false
		log("$4HUD: System.DrawText unavailable, falling back to info text")
		return
	end
	self.hudOk = true
	hudText(x + 0.028 * W, y + 0.012 * H, "/ " .. self.cfg.magSize, h.size * 0.9, h.col)
	local label = (s.reloading and not s.magSeated) and "RELOADING" or string.upper(self.gun.short or self.gun.title or "DESERT EAGLE")
	if #self:FireModes() > 1 then label = label .. "   " .. string.upper(self:FireMode()) end
	hudText(x - 0.004 * W, y + 0.05 * H, label, h.size * 0.55, { h.col[1], h.col[2], h.col[3], 0.7 })
end

-- ---------------------------------------------------------------------------
-- viewmodel animation
-- ---------------------------------------------------------------------------
-- rest loop: aiming wins (a reload that ends while aiming lands in ADS); slide-locked poses only when the mag is
-- empty and no reload is about to refill it
function Deagle:VmRestKey()
	local s = self.state
	local empty = s.ammo == 0 and not s.reloading
	if s.aiming then return empty and "adsEmpty" or "ads" end
	return empty and "idleEmpty" or "idle"
end

-- a one-shot clip is over: back to the rest loop, or into ADS if aim was held through it
-- Instant: every one-shot clip ends exactly on its rest pose (and the reload's seated spare mag swaps for the
-- identical main one without a blend that would fly them past each other). A queued draw / holster goes first.
function Deagle:VmReturn(trimmed)
	local s = self.state
	if self.pendingToggle then self.pendingToggle = false; self:Toggle()
	elseif s.aimHeld ~= s.aiming then self:UpdateAim()
	else
		-- a trimmed clip ends a few mm off its rest pose: a short blend, unless it ends with the spare mag seated (the
		-- rest pose has the main one there and a blend would fly both: switch, the image is the same to 3 mm)
		local cur = self.vmAnim
		local swap = cur and self.gun.swapTail and self.gun.swapTail[cur.key]
		self:VmPlay(self:VmRestKey(), true, 1.0, (trimmed and not swap) and self.cfg.vm.trimBlend or 0)
	end
end

function Deagle:VmBusy()   -- a one-shot clip (shot, reload, inspect, draw, holster) still playing
	local a = self.vmAnim
	return a and not a.loop and System.GetCurrTime() < (a.ends or 0)
end

function Deagle:VmDrawing()   -- a draw until the gun is up (its ready time): no shots, reloads or inspects before
	local a = self.vmAnim
	return a and DRAWING[a.key] and System.GetCurrTime() < (a.ready or a.ends or 0) or false
end

function Deagle:VmBusyWith(keys)
	return self:VmBusy() and keys[self.vmAnim.key] or false
end

-- blend for a clip start when the caller doesn't set one
function Deagle:VmBlendFor(key)
	local c, cur = self.cfg.vm, self.vmAnim
	if not cur then return c.blend end
	if self:VmBusy() then
		if INSPECTS[cur.key] then return c.inspectCutBlend end
		if SHOTS[cur.key] and SHOTS[key] then return c.refireBlend end
		return c.cutBlend
	end
	if (cur.key == "ads" or cur.key == "adsEmpty") and not REST[key] then return c.cutBlend end
	if SHOTS[key] and REST[cur.key] then return 0 end   -- the kick lands on the next frame (idle = the shot's first frame)
	return c.blend
end

-- Plays a clip on the base layer. One-shot clips play their sound cues and return to the rest loop by themselves.
-- The engine refuses a start of the clip already on the layer, so replaying one (inspect -> inspect) goes through idle.
function Deagle:VmPlay(key, loop, speed, blend)
	if not REST[key] then self:VmClearRecoil() end   -- a full-body clip replaces a recoil still playing on top
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return 0 end
	local a = c.anims[key]
	if key == "shoot" then
		self.vmShot = (self.vmShot or 0) + 1
		a = a[(self.vmShot - 1) % #a + 1]
	end
	local name, frames = a[1], a[2]
	local plain = (speed or 1.0) == 1.0     -- measured times only hold at the clip's own pace (not a fast holster)
	speed = (speed or 1.0) * self:Rate(key)
	blend = blend or self:VmBlendFor(key)
	local cur = self.vmAnim
	-- the reload ends with the spare mag seated and the main one parked off screen: switch to idle (identical image)
	-- before any blend, or the blend would fly both magazines across the view
	local tail = cur and self.gun.swapTail and self.gun.swapTail[cur.key] and self.gun.swapTail[cur.key] / self:Rate(cur.key)
	local swapped = tail and System.GetCurrTime() >= (cur.full or cur.ends or 0) - tail
	if (cur and cur.name == name and not REST[key]) or (swapped and blend > 0 and key ~= "idle") then
		pcall(vm.StartAnimation, vm, 0, c.anims.idle[1], c.layer, 0, 1.0, true)
	end
	local ok, started = pcall(vm.StartAnimation, vm, 0, name, c.layer, blend, speed, loop and true or false)
	if not (ok and started) and self.vmLastFail ~= name and not (self.vmAnim and self.vmAnim.name == name) then
		self.vmLastFail = name
		log("$4viewmodel anim did not start: " .. name .. " " .. tostring(started))
	end
	local len = frames / (a[3] or c.fps) / speed
	-- the clip ends where its hands have settled (gun.times trim): the rest of it is them holding still
	local stop = math.min(not loop and plain and self:ClipTime(key, "trim") or len, len)
	local trimmed = stop < len - 0.01
	local now = System.GetCurrTime()
	self.vmAnimToken = (self.vmAnimToken or 0) + 1
	local tok = self.vmAnimToken
	self.vmAnim = { key = key, name = name, loop = loop, ends = now + stop, full = now + len,
		ready = now + math.min(plain and self:ClipTime(key, "ready") or stop, stop) }
	if not loop then
		self:PlayCues(key, blend, speed)
		if HOLDS_AIM[key] and key ~= "reload" and stop > c.adsLead then   -- reload: at its end (mag swap)
			Script.SetTimer(math.floor((stop - c.adsLead) * 1000), function()
				if Deagle.vmAnimToken == tok and Deagle.vmShown and Deagle.state.aimHeld then Deagle:UpdateAim(c.adsLead) end
			end)
		end
		Script.SetTimer(math.max(1, math.floor(stop * 1000) - 30), function()
			if Deagle.vmAnimToken == tok and Deagle.vmShown and not Deagle.vmHolstering then Deagle:VmReturn(trimmed) end
		end)
	end
	return len
end

-- A finished non-looping clip on the recoil layer holds its last frame. The empty-mag recoil ends with the slide
-- locked back, so that offset rode on top of every later clip (reload, idle) until the layer was cleared.
function Deagle:VmClearRecoil()
	self.vmRecoilToken = (self.vmRecoilToken or 0) + 1
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.StopAnimation, vm, 0, self.cfg.vm.recoilLayer) end
end

-- aimed shot: additive recoil on its own layer over the ADS pose, cleared when it ends
function Deagle:VmRecoilAdd(empty)
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return end
	local list = c.anims.shootAdd
	local a = empty and c.anims.shootEmptyAdd or list[((self.vmShot or 0) % #list) + 1]
	local r = self:Rate(empty and "shootEmptyAdd" or "shootAdd")
	local len = a[2] / (a[3] or c.fps) / r
	self.vmShot = (self.vmShot or 0) + 1
	self.vmRecoilToken = (self.vmRecoilToken or 0) + 1
	local tok = self.vmRecoilToken
	local now = System.GetCurrTime()
	local rapid = now < (self.vmRecoilEnds or 0)       -- the last kick is still on: crossfade instead of snapping to zero
	self.vmRecoilEnds = now + len
	pcall(vm.StartAnimation, vm, 0, a[1], c.recoilLayer, rapid and 0.04 or 0.0, r, false)
	self:PlayCues(empty and "shootEmpty" or "shoot")
	Script.SetTimer(math.floor(len * 1000), function()
		if Deagle.vmRecoilToken ~= tok then return end
		-- same frame: rest loop in (adsEmpty after the last round keeps the slide locked), recoil offset out
		if Deagle.vmShown and not Deagle.vmHolstering and not Deagle:VmBusy() then Deagle:VmPlay(Deagle:VmRestKey(), true, 1.0, 0) end
		Deagle:VmClearRecoil()
	end)
end

-- sound cues of a clip, played by the plugin; cues of an interrupted clip are dropped (shots always play).
-- Cues follow the clip's own playhead (GetAnimationTime, 0..1 through the clip; checked every frame by CueTick), not a
-- timer: in F9 slow motion the viewmodel's animation runs ~1.5x faster than game time while timers run on game time, so
-- timed sounds fell behind the later they came (mag-in ~2 s, slide close ~5 s late, the user's recording 2026-10-12).
-- a gun's Ready or Not handling cues (guns/ron_handling.lua) as plain cues: each event starts early by its click time,
-- so the click lands on the clip's moment ("ron:<event>", or "ronfeet:<event>" at the player's feet)
function Deagle:RonCues(key)
	local rc = self.gun.ronCues and self.gun.ronCues[key]
	if not rc then return nil end
	local out, r, latency = {}, self:Rate(key), self.cfg.vm.cueLead or 0
	for _, c in ipairs(rc) do
		-- real seconds early (its click, then the output latency) in clip seconds: the clip runs Rate times faster
		local lead = ((c.start and 0 or (self.ronClick[c[2]] or 0) / 1000) + latency) * r
		out[#out + 1] = { math.max(0, c[1] - lead), (c.feet and "ronfeet:" or "ron:") .. c[2] }
	end
	return out
end
-- blend: s the clip takes to blend in (VmPlay), while the layer may still report the clip it blends out of; speed: its
-- playback rate
function Deagle:PlayCues(key, blend, speed)
	local g = self.gun
	local cues = (self:RonSounds() and g.ronCues and self:RonCues(key)) or g.cues[key] or ((key == "shoot" or key == "shootEmpty") and SHOT_CUE) or nil
	if not cues then return end
	local a = self.cfg.vm.anims[key]
	if key == "shoot" then a = a[((self.vmShot or 1) - 1) % #a + 1] end
	local span = math.max(1, a[2] - 1) / (a[3] or self.cfg.vm.fps)   -- clip-timeline seconds from first to last frame
	local list = {}
	for _, c in ipairs(cues) do
		local at = math.min(1, c[1] / span)
		if at <= 0.001 then self:CueFire(key, c[2]) else list[#list + 1] = { at = at, file = c[2] } end
	end
	table.sort(list, function(x, y) return x.at < y.at end)
	self.cuesPending = #list > 0 and { key = key, name = a[1], tok = self.vmAnimToken, list = list,
		from = System.GetCurrTime(), blend = blend or 0, speed = speed or 1, span = span } or nil
end
function Deagle:CueFire(key, file)
	local v = self.cfg.vm.soundVol
	local ev = file:match("^ron:(.+)$")
	if ev then return self:RonHere(ev) end
	ev = file:match("^ronfeet:(.+)$")
	if ev then
		local p = self:Player()
		local at = p and p:GetWorldPos()
		if at and self.native then self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|", v.ron, at.x, at.y, at.z + 0.05, ev)) end
		return
	end
	if file == "shot" then
		if not (self.cfg.shotSound and self.cfg.gunFx) then return end
		local ron = self:RonSounds()
		if ron and ron.fire then
			if self.native then
				local vol, out = v.shot * self:ShotGain(nil, 1), self:EnvOutside() and 1 or 0
				if self.fc and ron.fire3p then   -- heard from the free camera: RoN's 3D third-person shot at Henry, fading with distance
					local at = self:GunSoundPos()
					self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|IsOutside=%d Occlusion=0 PropagateDistance=@ Falloff=3", vol, at.x, at.y, at.z, ron.fire3p, out))
				else self:Native(string.format("ron %.3f %s|IsOutside=%d", vol, ron.fire, out)) end
			end
			return
		end
		local f = self:ShotFile()
		if f then self:Snd(f, v.shot * self:ShotGain(f)) end
	else self:Snd(file, v.foley) end
end
-- every viewmodel frame: the cues the playhead has passed
function Deagle:CueTick()
	local p = self.cuesPending
	if not p then return end
	if self.vmAnimToken ~= p.tok and p.key ~= "shoot" and p.key ~= "shootEmpty" then self.cuesPending = nil return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	local layer = self.cfg.vm.layer
	-- the layer may report the clip it blends out of until the blend is over: meanwhile the playhead from game time
	-- (game seconds cover the blend: the clip's clock runs at least as fast, ~1.5x in slow motion). (KCD2's
	-- GetCurAnimation would say which clip it is, but returns nil even while one plays: measured 2026-10-13; waiting
	-- for it silenced every clip cue.)
	local since, t = System.GetCurrTime() - p.from, nil
	if since < p.blend then t = since * p.speed / p.span
	else
		local okT, tt = pcall(vm.GetAnimationTime, vm, 0, layer)
		if not okT or type(tt) ~= "number" then return end
		t = tt
	end
	-- a cue due before the next frame goes now: waiting for the frame after it made every sound up to a frame late
	local ahead = (self.frameDt or 1 / 60) * p.speed / p.span
	while p.list[1] and p.list[1].at <= t + ahead + 1e-4 do
		self:CueFire(p.key, table.remove(p.list, 1).file)
	end
	if not p.list[1] then self.cuesPending = nil end
end

-- ---------------------------------------------------------------------------
-- draw / holster
-- ---------------------------------------------------------------------------
function Deagle:VmShow()
	self.vmShownAt = System.GetCurrTime()
	self.vmHolstering = false
	local vm = self:VmEntity()
	if not vm then return end
	self.vmShown = true
	pcall(vm.Hide, vm, 0)
	self:VmPlace()
	self:VmPlay(self.state.ammo == 0 and "drawEmpty" or "draw", false)
	self.vmRoundsShown = nil
	self:VmRounds(self:MagRounds())
	if self.state.magOut and self.gun.mags then pcall(vm.HideAttachment, vm, 0, "vm_mag1", true, true) end
	self.vmToken = (self.vmToken or 0) + 1
	local token = self.vmToken
	Script.SetTimer(0, function() Deagle:VmTick(token) end)
	pcall(vm.SetFlags, vm, ENTITY_FLAG_CASTSHADOW, 2)   -- no floating-arm shadow; Henry's shadow stays
	self:SetBodyHidden(true)
	self:NativeSyncVm()
	local key = tostring(self.vmId) .. self.cfg.vm.arms
	if self.vmArmsApplied ~= key then
		self.vmArmsApplied = key
		self:VmApplyArms()
	end
end

function Deagle:VmHide()
	self.vmShown, self.vmZoomHidden = false, false
	self:FlashDisarm()
	self.vmToken = (self.vmToken or 0) + 1
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.Hide, vm, 1) end
	self:SetBodyHidden(false)
end

function Deagle:ShowInHand()
	self:VmShow()
end

-- holster: play the reversed draw, then hide
function Deagle:HideInHand()
	if self.vmShown and not self.vmHolstering then
		self.vmHolstering = true
		local len = self:VmPlay(self.state.ammo == 0 and "holsterEmpty" or "holster", false, self.cfg.vm.holsterSpeed)
		Script.SetTimer(math.floor(len * 1000), function()
			Deagle.vmHolstering = false
			if not Deagle.state.drawn then Deagle:VmHide(); Deagle:NativeSyncVm() end
			if Deagle.pendingToggle then Deagle.pendingToggle = false; Deagle:Toggle() end   -- drawn again meanwhile
		end)
		return
	end
	self.vmHolstering = false
	self:VmHide()
	self:NativeSyncVm()
end

-- Draw / holster. Asked for during a draw, holster or reload, it waits for that to finish (no mid-way jumps).
function Deagle:Toggle()
	local s = self.state
	if self.vmShown and (self.vmHolstering or self:VmBusyWith(HOLDS_TOGGLE)) then
		self.pendingToggle = not self.pendingToggle
		return
	end
	if not s.drawn and not self:Owns() then
		self:Notify("You don't have a Desert Eagle")
		return
	end
	s.drawn = not s.drawn
	if s.drawn then
		local h = self:Player().human
		try("holster weapon", function() if h:IsWeaponDrawn() then h:HolsterWeapon() end end)
		self:ShowInHand()
	else
		self:HideInHand()
	end
	-- native maps on our keys stay off while the pistol is out: "combat" turns left click into a fist attack,
	-- "draw_holster_torch" takes the torch out on R (reload)
	for _, map in ipairs(GAME_MAPS_OFF) do try(map .. " map", ActionMapManager.EnableActionMap, map, not s.drawn) end
	-- and R: the game's own no_torch filter while the gun is out (plugin; returning true from OnAction for
	-- toggle_torch didn't stop it: the game takes the torch out elsewhere too, in game 2026-10-12)
	self:Native("notorch " .. (s.drawn and "1" or "0"))
	if not s.drawn then self:SetAim(false) end
	log(s.drawn and "drawn" or "holstered")
	self:ShowAmmo()
end

-- ---------------------------------------------------------------------------
-- native companion (deagle_native.asi): System.LogAlways("[DN] ...") lines are swallowed by the plugin as commands;
-- it answers the hello with the deagle_native_ok console command. Without it the mod works, minus sounds and motion.
-- ---------------------------------------------------------------------------
function Deagle:Native(cmd) System.LogAlways("[DN] " .. cmd) end

function Deagle:NativeHello()
	self.native = false
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

function Deagle:Snd(file, vol)
	if self.native then self:Native(string.format("snd %s %.2f", file, vol or 1)) end
end

-- ---------------------------------------------------------------------------
-- input: our action map + a wrapper around the player's action hook
-- ---------------------------------------------------------------------------
function Deagle:OnAction(action, activation, value)
	local s = self.state
	if action == "deagle_look_x" or action == "deagle_look_y" then return self:FreecamLook(action, tonumber(value) or 0) end
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
		if activation == "press" then self:Reload() end
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
	self.vmId, self.casePool, self.flashUntil = nil, nil, nil   -- a load ends the old world: its ids may now name other entities
	self.flashArmed = nil                                       -- the plugin forgets its live flash at the hello
	self.rounds, self.roundsToken = nil, nil                    -- rounds in flight belong to the old world too
	self.paths, self.pathsToken = nil, nil
	self.fc, self.fcAim = nil, nil                          -- the plugin turns the free camera off at the hello
	-- a load can set the game clock back, and times kept from before it then lie ahead: the gun's kicks grew without
	-- bound and every shot left along NaN, hitting nothing for minutes (in game 2026-10-14, a save loaded in a house)
	self.gunKicks, self.camRecoil, self.camRecoilLast, self.motionIn, self.flashLights, self.nearList = nil, nil, nil, nil, nil, nil
	s.heat, s.accFire, s.accT, s.burstReady = 0, 0, nil, nil
	if self.slowOn then self:SlowMo(false) end
	self:RemoveStaleEntities()   -- at load, not just on draw: a holstered gun would leave the pyramid in view
	self:NativeHello()
end

-- ---------------------------------------------------------------------------
-- where the player is (docs/ron_live_spec.md): guns with gun.ron fire Ready or Not's own event through its own engine
-- (the plugin runs it: native/src/ron.cpp). RoN's room sound comes from 8 room and 4 material snapshots whose strength
-- follows global parameters (ReverbSMALL ... ReverbMaterialFABRIC, 0..1; its map boxes set them and fade them across
-- doorways). KCD2 has no such boxes, so rays from the eye classify the place every cfg.env.period s, a class takes over
-- after 2 reads in a row agree, and its weights fade in over cfg.env.fade s.
-- Classes: out_forest out_street out_covered in_<small|mid|hall|corridor>[_<stone|wood|metal|fabric>] in_cave(_stone)
-- ---------------------------------------------------------------------------
local ENV_UP, ENV_RING = {}, {}
local function envSurf(name)
	if name:find("^mat_rock") or name:find("stone") or name:find("brick") or name:find("plaster") or name:find("ceramic") then return "stone" end
	if name:find("wood") or name:find("bark") or name:find("tree") or name:find("wicker") then return "wood" end
	if name:find("metal") or name:find("chain") or name:find("plate") or name:find("bell") or name:find("iron") then return "metal" end
	if name:find("fabric") or name:find("cloth") or name:find("sack") or name:find("carpet") or name:find("leather") then return "fabric" end
	if name:find("vegetation") or name:find("bush") or name:find("canopy") or name:find("leaves") or name:find("thatch") then return "veg" end
	if name:find("soil") or name:find("grass") or name:find("sand") or name:find("gravel") or name:find("mud") then return "terrain" end
	return "other"
end
local ENV_MATS = { "stone", "wood", "metal", "fabric" }
-- one sample: up = { {dist, kind}... } (nil dist = nothing), ring = 16 of { dist, kind } (index i = i x 22.5 deg)
function Deagle:EnvClassify(sample)
	local c = self.cfg.env
	local roofN, roofStone = 0, false
	for _, u in ipairs(sample.up) do
		if u.dist and u.dist <= c.roof then
			roofN = roofN + 1
			roofStone = roofStone or u.kind == "stone" or u.kind == "terrain"
		end
	end
	local roof = roofN >= c.roofHits
	local near, dists, kinds = 0, {}, {}
	for _, r in ipairs(sample.ring) do
		if r.dist then
			kinds[r.kind] = (kinds[r.kind] or 0) + 1
			if r.dist <= c.ringNear then near = near + 1; dists[#dists + 1] = r.dist end
		end
	end
	local hits = 0
	for _, n in pairs(kinds) do hits = hits + n end
	local function share(k) return hits > 0 and (kinds[k] or 0) / hits or 0 end
	local info = { roofHits = roofN, ringHits = hits, ringNear = near, shares = {} }
	for _, m in ipairs(ENV_MATS) do info.shares[m] = share(m) end
	if roof and near >= c.ringInside then
		table.sort(dists)
		local d = dists[math.ceil(#dists / 2)] or 0
		info.median = d
		local function dist(i) local r = sample.ring[(i - 1) % 16 + 1]; return r.dist or math.huge end
		local corridor = false
		for i = 1, 8 do
			if dist(i) < c.corrNarrow and dist(i + 8) < c.corrNarrow and dist(i + 4) > c.corrWide and dist(i + 12) > c.corrWide then corridor = true end
		end
		if share("stone") > c.share and roofStone then return share("stone") > 0.8 and "in_cave_stone" or "in_cave", info end
		local size = corridor and "corridor" or d < c.small and "small" or d <= c.hall and "mid" or "hall"
		local mat, best = nil, c.share   -- the walls' material: the kind most ring rays hit, if it's over half of them
		for _, m in ipairs(ENV_MATS) do if share(m) > best then mat, best = m, share(m) end end
		return "in_" .. size .. (mat and ("_" .. mat) or ""), info
	end
	if roof then return "out_covered", info end
	local trunks = 0
	for _, r in ipairs(sample.ring) do if r.dist and r.kind == "wood" then trunks = trunks + 1 end end
	if share("veg") > c.vegShare or trunks >= c.trunks then return "out_forest", info end
	return "out_street", info
end
-- a class as Ready or Not's globals: the room's snapshot (and material's) at 1, every other at 0; outside = IsOutside
local ENV_ROOMS = { "BATHROOM", "SMALL", "CORRIDOR", "MID", "HALL", "STREET", "FOREST", "CAVE" }
local ENV_MAT_GLOBAL = { stone = "CONCRETE", wood = "WOOD", metal = "METAL", fabric = "FABRIC" }
local ENV_GLOBALS = {}
for _, r in ipairs(ENV_ROOMS) do ENV_GLOBALS[#ENV_GLOBALS + 1] = "Reverb" .. r end
for _, m in ipairs(ENV_MATS) do ENV_GLOBALS[#ENV_GLOBALS + 1] = "ReverbMaterial" .. ENV_MAT_GLOBAL[m] end
function Deagle:EnvMix(cls)
	local mix = {}
	for _, g in ipairs(ENV_GLOBALS) do mix[g] = 0 end
	local room, mat
	if cls == "out_street" then room = "STREET"
	elseif cls == "out_forest" then room = "FOREST"
	elseif cls == "out_covered" then room, mat = "MID", "stone"   -- a roof with open sides: a mid room's ring off it
	else
		local size, m = cls:match("^in_(%a+)_?(%a*)$")
		room, mat = size and size:upper(), m ~= "" and m or nil
	end
	if room then mix["Reverb" .. room] = 1 end
	if mat then mix["ReverbMaterial" .. ENV_MAT_GLOBAL[mat]] = 1 end
	return mix, cls:find("^out_") ~= nil
end
function Deagle:EnvOutside()
	return self.envClass == nil or self.envClass:find("^out_") ~= nil
end
-- rays from the eye: static things and terrain, not the player
function Deagle:EnvSample()
	local c, eye, pl = self.cfg.env, System.GetViewCameraPos(), self:Player()
	local function cast(dir, len)   -- the first thing a sound would bounce off: on through foliage, helpers, see-through hulls
		local from, done = eye, 0
		for _ = 1, 8 do
			local hits = {}
			local ok, n = pcall(Physics.RayWorldIntersection, from, vscale(dir, len - done), 1, ent_static + ent_terrain, pl and pl.id, nil, hits)
			if not (ok and n and n > 0 and hits[1]) then break end
			local name = surfaceName(hits[1])
			local sf = SURFACES[name]
			if not (sf and sf.fx == "pass") then return { dist = done + hits[1].dist, kind = envSurf(name) } end
			done = done + hits[1].dist + 0.01
			if done >= len then break end
			from = vadd(eye, vscale(dir, done))
		end
		return { dist = nil }
	end
	if #ENV_UP == 0 then
		local a = math.rad(c.upCone)
		ENV_UP[1] = { x = 0, y = 0, z = 1 }
		for k = 0, 3 do local h = k * math.pi / 2; ENV_UP[#ENV_UP + 1] = { x = math.sin(a) * math.cos(h), y = math.sin(a) * math.sin(h), z = math.cos(a) } end
		for k = 0, 15 do local h = k * math.pi / 8; ENV_RING[k + 1] = { x = math.cos(h), y = math.sin(h), z = 0 } end
	end
	local sample = { up = {}, ring = {} }
	for i, d in ipairs(ENV_UP) do sample.up[i] = cast(d, c.upLen) end
	for i, d in ipairs(ENV_RING) do sample.ring[i] = cast(d, c.ringLen) end
	return sample
end
-- every viewmodel frame: a new read every period s; the class changes after 2 agreeing reads; the weights fade to it
function Deagle:EnvTick()
	local c = self.cfg.env
	if not (self.gun and self.gun.ron) then return end
	local now = System.GetCurrTime()
	-- now < envT: a save loaded while the game ran set the clock back (322 s once, Claude 4 2026-10-14): check at once
	if c.on and (not self.envT or now < self.envT or now - self.envT >= c.period) then
		self.envT = now
		local cls, info = self:EnvClassify(self:EnvSample())
		self.envInfo = info
		if cls == self.envClass then self.envPending = nil
		elseif cls == self.envPending or not self.envClass then self.envClass, self.envPending = cls, nil
		else self.envPending = cls end
	end
	self:EnvFade(now)
end
-- move every global toward the class's value at 1 / fade per second and send the ones that moved; the first class
-- after a load is set at once (nothing to fade from)
function Deagle:EnvFade(now)
	if not self.envClass or not self.native then return end
	local target = self:EnvMix(self.envClass)
	local cur, first = self.envMixNow, self.envMixNow == nil
	if first then cur = {}; self.envMixNow = cur end
	local dt = math.max(0, now - (self.envFadeT or now))
	self.envFadeT = now
	local step = self.cfg.env.fade > 0 and dt / self.cfg.env.fade or 1
	for _, g in ipairs(ENV_GLOBALS) do
		local was, want = cur[g], target[g]
		local v = (first or was == nil) and want or (was < want and math.min(want, was + step) or math.max(want, was - step))
		if v ~= was then
			cur[g] = v
			self:Native(string.format("rong %s %.3f", g, v))
		end
	end
end
-- the gun's Ready or Not events load at the handshake, so the first shot has no load delay
function Deagle:RonWarm()
	self.envMixNow = nil   -- the plugin's engine may be new: send every weight again
	for _, g in pairs(self.guns) do
		if g.ron then for _, ev in pairs(g.ron) do self:Native("ronload " .. ev) end end
		local seen = {}
		for _, list in pairs(g.ronCues or {}) do
			for _, c in ipairs(list) do if not seen[c[2]] then seen[c[2]] = true; self:Native("ronload " .. c[2]) end end
		end
	end
	for _, ev in ipairs(self.ronHitEvents) do self:Native("ronload " .. ev) end
end
-- what the plugin can do beyond the handshake ("files": play our own sound files, ron3 file:<path>); its files load now
function Deagle:NativeCaps(caps)
	self.nativeCaps = {}
	for w in tostring(Deagle.Arg(caps) or ""):gmatch("%a+") do self.nativeCaps[w] = true end
	if self.nativeCaps.files then
		local seen = {}
		for _, sf in pairs(SURFACES) do
			if type(sf.ron) == "table" and not seen[sf.ron] then
				seen[sf.ron] = true
				for _, f in ipairs(sf.ron.files) do self:Native("ronload file:" .. f) end
			end
		end
	end
end
function Deagle:RonState(ok)
	self.ronOk = Deagle.Arg(ok) == "1"   -- the console hands %1 over in quotes ("1"): a raw compare always failed
	if not self.ronOk then
		local msg = "Ready or Not's sound engine didn't start: Mods/deagle/ron is missing or broken (see deagle_native.log)"
		log("$4" .. msg); self:Notify(msg)
	end
end
-- the gunshot file for a gun that plays its own recordings
function Deagle:ShotFile()
	local shots = self.gun.shotSounds or {}
	return #shots > 0 and shots[math.random(1, #shots)] or nil
end
function Deagle:EnvPrint()
	if not self.envClass then self:EnvTick() end
	local i = self.envInfo or {}
	local mix, outside = self:EnvMix(self.envClass or "out_street")
	local on = {}
	for _, g in ipairs(ENV_GLOBALS) do if mix[g] > 0 then on[#on + 1] = g end end
	local sh = i.shares or {}
	local msg = string.format("place %s = %s%s (roof rays %s, ring hits %s, near %s, median %.1f m; stone %.0f%% wood %.0f%% metal %.0f%% fabric %.0f%%)",
		tostring(self.envClass), table.concat(on, " + "), outside and " outside" or " inside", tostring(i.roofHits), tostring(i.ringHits),
		tostring(i.ringNear), i.median or -1, 100 * (sh.stone or 0), 100 * (sh.wood or 0), 100 * (sh.metal or 0), 100 * (sh.fabric or 0))
	log(msg); self:Notify(msg)
end

-- ---------------------------------------------------------------------------
-- free camera look (the plugin flies the camera: freecam.cpp). While it's on, KCD2's own "no_camera" filter keeps the
-- mouse from turning Henry, and the mouse's raw movement (our deagle_look_x / _y on maxis_x / _y) turns the camera;
-- holding right mouse lifts the filter so the mouse aims Henry (the camera stays). A line from Henry's eye shows where a
-- shot goes. The plugin tells us when it starts and stops (deagle_freecam_state). (Snapping Henry's aim back from Lua
-- every frame lagged a frame and the camera spun, in game 2026-10-12.)
-- The mouse-to-angle scale is learned from Henry's own turning while the free camera is off (radians per unit, with
-- its sign), so the camera turns like the game does.
-- ---------------------------------------------------------------------------
local function viewAngles()
	local d = System.GetViewCameraDir()   -- Henry's real eye, not the free camera
	return math.asin(math.max(-1, math.min(1, d.z))), math.atan2(-d.x, d.y)
end
local function wrap(a) return (a + math.pi) % (2 * math.pi) - math.pi end
Deagle.lookScale = Deagle.lookScale or { x = { sv = 0, sa = 0 }, y = { sv = 0, sa = 0 } }
function Deagle:FreecamLook(action, v)
	local axis = action == "deagle_look_x" and "x" or "y"
	local L = self.lookScale[axis]
	if self.fc and not self.fcAim then
		if not L.k then return false end                     -- not learned yet (move the mouse once without the freecam)
		if axis == "x" then self.fc.camY = wrap(self.fc.camY + v * L.k)
		else self.fc.camP = math.max(-1.55, math.min(1.55, self.fc.camP + v * L.k)) end
		return false
	end
	if not self.fc and v ~= 0 then                          -- learn how far Henry turns per unit of mouse
		local p, y = viewAngles()
		local last = self.lookLast and self.lookLast[axis]
		if last then
			local da = axis == "x" and wrap(y - last.y) or (p - last.p)
			L.sv, L.sa = L.sv + v * v, L.sa + v * da         -- least squares through zero: k = sum(v da) / sum(v^2)
			if L.sv > 50 then L.k = L.sa / L.sv end
		end
		self.lookLast = self.lookLast or {}
		self.lookLast[axis] = { p = p, y = y }
	end
	return false
end
function Deagle:FreecamState(on)
	on = Deagle.Arg(on) ~= "0"
	if on then
		local p, y = viewAngles()
		self.fc = { camP = p, camY = y, token = {} }
		self:Native("nocamera " .. (self.fcAim and "0" or "1"))
		local tok = self.fc.token
		Script.SetTimer(0, function() Deagle:FreecamTick(tok) end)
		if not (self.lookScale.x.k and self.lookScale.y.k) then
			self:Notify("free camera: move the mouse once with it off, so it learns your mouse speed")
		end
	else
		self.fc = nil
		self:Native("nocamera 0")
		self:Native("freecam rotoff")
	end
end
function Deagle:FreecamTick(token)
	local fc = self.fc
	if not fc or fc.token ~= token then return end
	self:Native(string.format("freecam rot %.5f %.5f", fc.camP, fc.camY))
	-- where a shot goes: Henry's eye along his aim to the first thing it meets (or 150 m)
	local eye, d = System.GetViewCameraPos(), System.GetViewCameraDir()
	local hits, pl, reach = {}, self:Player(), 150
	local ok, n = pcall(Physics.RayWorldIntersection, eye, vscale(d, reach), 1, ent_all, pl and pl.id, nil, hits)
	if ok and n and n > 0 and hits[1] then reach = hits[1].dist end
	local tip = vadd(eye, vscale(d, reach))
	pcall(System.DrawLine, vadd(eye, vscale(d, 0.3)), tip, 1, 0.25, 0.1, 1)
	for _, a in ipairs({ { x = 0.15, y = 0, z = 0 }, { x = 0, y = 0.15, z = 0 }, { x = 0, y = 0, z = 0.15 } }) do
		pcall(System.DrawLine, vsub(tip, a), vadd(tip, a), 1, 0.25, 0.1, 1)
	end
	Script.SetTimer(0, function() Deagle:FreecamTick(token) end)
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

-- deagle_flash ron|ours: the flash cards' look (flashMesh.look), on the cards (flashMode mesh)
function Deagle:FlashLookCmd(arg)
	local look = tostring(arg or ""):lower():find("ron") and "ron" or "ours"
	if self.cfg.flashMode ~= "mesh" then self:Set("flashMode mesh") end
	self:Set("flashMesh.look " .. look)
	self:Notify("muzzle flash: " .. (look == "ron" and "Ready or Not's" or "ours"))
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

function Deagle:FxTest(name)
	name = unquote(name)
	local dir = System.GetViewCameraDir()
	self:SpawnFx(name, vadd(System.GetViewCameraPos(), vscale(dir, 3)), dir, 1)
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
System.AddCCommand("deagle_env", "Deagle.envT = nil Deagle:EnvTick() Deagle:EnvPrint()", "deagle_env: the place the gunshot sounds like (Ready or Not's room and material weights) and the ray numbers")
System.AddCCommand("deagle_freecam_state", "Deagle:FreecamState([[%1]])", "internal: the plugin's free camera came on (1) or went off (0)")
System.AddCCommand("deagle_slowmo", "Deagle:SlowMoCmd(Deagle.Arg([[%1]]))", "deagle_slowmo [speed|off]: slow motion at that game speed (0.01-1, kept: F9 uses it too); no value = on / off like F9")
System.AddCCommand("deagle_zoom", "Deagle:SetZoom(tonumber(Deagle.Arg([[%1]])))", "deagle_zoom <factor>: debug zoom (1 / nothing = off); F2 / mouse 4 step x4, x10, off")
System.AddCCommand("deagle_toggle", "Deagle:EnsureInit() Deagle:Toggle()", "Draw / holster the Desert Eagle")
-- deagle_stress: a frame-rate test anyone can run. Infinite ammo, 10 rounds a second while the view looks at the sky
-- (no one gets hit), in phases: fire with tracers, stop with tracers off (the pieces still held age out), fire
-- without. Every 0.5 s of game time the log gets the frame time split into our Lua (the per-frame functions and Fire)
-- and the rest (the engine: rendering, particles, physics), with the rounds in the air and the tracer commands sent.
-- Made after the user's "spammed 3 clips, 140 to 70 fps" (2026-10-15).
local STRESS = {
	{ "idle", 2, false, true }, { "FIRE tracers on", 5, true, true }, { "stop, tracers off", 3, false, false },
	{ "idle again", 3, false, false }, { "FIRE tracers off", 5, true, false }, { "stop", 3, false, false },
}
local STRESS_LUA = { "VmTick", "RoundsTick", "Fire", "EnvTick", "MotionTick", "DrawHud" }
function Deagle:Stress()
	if self.stress then return end
	local clock = System.GetCurrAsyncTime
	local X = { ph = 1, tracersWas = self.cfg.tracers, lua = 0, pieces = 0, orig = {} }
	self.stress = X
	for _, k in ipairs(STRESS_LUA) do
		local f = self[k]
		X.orig[k] = f
		self[k] = function(...)
			local a = clock()
			local r = { f(...) }
			X.lua = X.lua + clock() - a
			return unpack(r)
		end
	end
	X.orig.Native = self.Native
	self.Native = function(s, cmd, ...)
		if type(cmd) == "string" and (cmd:sub(1, 4) == "fxs " or cmd:sub(1, 4) == "fxr ") then X.pieces = X.pieces + 1 end
		return X.orig.Native(s, cmd, ...)
	end
	local vt = self.VmTick   -- the timed one: the frame clock goes around it
	local lastA, lastG, nextShot
	self.VmTick = function(s, ...)
		local a, g = clock(), System.GetCurrTime()
		if lastA and lastG and g > lastG then   -- frames the game ran (not paused in the background)
			local ph = STRESS[X.ph]
			X.start, X.t0 = X.start or g, X.t0 or g
			X.win = X.win or { t = g, n = 0, sum = 0, max = 0 }
			local w, dt = X.win, a - lastA
			w.n, w.sum, w.max = w.n + 1, w.sum + dt, math.max(w.max, dt)
			s.cfg.tracers = ph[4]
			if ph[3] and System.GetViewCameraDir().z > 0.5 and g >= (nextShot or 0) then
				s.state.ammo = 7
				pcall(s.Fire, s, true)
				nextShot = g + 0.1
			end
			if g - w.t >= 0.5 then
				local rounds = 0
				for _ in pairs(s.rounds or {}) do rounds = rounds + 1 end
				local fm, lm = w.sum / w.n * 1000, X.lua / w.n * 1000
				log(string.format("STRESS %4.1fs %-18s %5.1f ms (%3.0f fps, worst %4.1f) = Lua %4.2f + engine %5.1f | rounds %2d, tracer cmds %4d",
					g - X.start, ph[1], fm, 1000 / fm, w.max * 1000, lm, fm - lm, rounds, X.pieces))
				X.lua, X.pieces, X.win = 0, 0, nil
			end
			if g - X.t0 >= ph[2] then
				X.ph, X.t0 = X.ph + 1, g
				if X.ph > #STRESS then
					for k, f in pairs(X.orig) do s[k] = f end
					s.cfg.tracers, s.stress = X.tracersWas, nil
					s:Native("fxrstat")
					log("STRESS done")
					s:Notify("stress test done")
				end
			end
		end
		lastA, lastG = a, g
		return vt(s, ...)
	end
	self:Notify("stress test: look at the sky for 21 s")
end

System.AddCCommand("deagle_fire", "Deagle:EnsureInit() Deagle:Fire()", "Fire one shot")
System.AddCCommand("deagle_stress", "Deagle:EnsureInit() Deagle:Stress()", "Frame-rate test: infinite-ammo fire into the sky in phases, frame times in kcd.log")
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
System.AddCCommand("deagle_native_ok", "Deagle.native = true; System.LogAlways('[Deagle] native plugin linked'); if Deagle.MotionParams then Deagle:MotionParams() end; Deagle:RonWarm()", "internal: native plugin handshake")
-- %line: the plugin sends several words ("files fxr"); with %1 KCD2 refused the command ("Too many arguments for:
-- deagle_native_caps", 2026-10-15), so no capability was set at all
System.AddCCommand("deagle_native_caps", "Deagle:NativeCaps([[%line]])", "internal: what the native plugin can do (files, fxr)")
System.AddCCommand("deagle_ron_state", "Deagle:RonState([[%1]])", "internal: Ready or Not's sound engine started in the plugin (1) or failed (0)")
System.AddCCommand("deagle_native_test", "Deagle:NativeHello() Deagle:Snd('decho/decho-1.wav', 1)", "ping the native plugin and play a shot sound")
System.AddCCommand("deagle_dev_reload", "Deagle:DevReload()", "Re-run the Deagle script from the dev path")
-- deagle_fxreload: the plugin re-reads the dev copy of the impacts library (build.sh --dev mirrors src/Libs/Particles) into
-- the live effects, so impact effects can be tuned without restarting the game. The engine swaps an effect's params
-- without a lock: never while impact emitters may still be alive (3 s after the last shot)
function Deagle:FxReload(lib)
	lib = (lib and lib ~= "") and lib or "deagle_impacts"
	if not self.native then log("$4fxreload needs the plugin") return end
	local since = System.GetCurrTime() - (self.state.lastShot or -100)
	if since < 3 then   -- effects of the last shot may still be alive: try again once they're gone (game time: paused waits)
		log(string.format("fxreload: in %.1f s (effects of the last shot may still be alive)", 3 - since))
		if not self.fxReloadQueued then
			self.fxReloadQueued = true
			Script.SetTimer(math.floor((3 - since) * 1000) + 100, function() self.fxReloadQueued = false; self:FxReload(lib) end)
		end
		return
	end
	self:Native("fxreload " .. self.devRoot .. "Libs/Particles/" .. lib .. ".xml " .. lib)
end
System.AddCCommand("deagle_fxreload", "Deagle:FxReload(Deagle.Arg([[%1]]))", "deagle_fxreload [library]: re-read Libs/Particles/<library>.xml (default deagle_impacts) from the dev folder into the running game")
System.AddCCommand("deagle_lua", "Deagle:DevLua([==[%line]==])", "deagle_lua <code>: run Lua (dev)")

Deagle:HookActions()
if not Deagle.listening then
	Deagle.listening = true
	try("event listener", function()
		UIAction.RegisterEventSystemListener(Deagle, "System", "OnGameplayStarted", "OnGameplayStarted")
	end)
end

-- ---- fire modes: from the gun's CS2 weapon (burst / full auto); B cycles them, the HUD shows the current one
function Deagle:FireModes()
	return self.gun.modes or { "semi" }
end

function Deagle:FireMode()
	local m = self:FireModes()
	return m[self.state.modeIndex or 1] or m[1]
end

function Deagle:CycleFireMode()
	local m = self:FireModes()
	if #m < 2 then return end
	self.state.modeIndex = (self.state.modeIndex or 1) % #m + 1
	log("fire mode " .. self:FireMode())
end

function Deagle:TriggerPress()
	local s, c, mode = self.state, self.cfg, self:FireMode()
	if mode == "auto" then
		s.trigger = true
		self.autoToken = (self.autoToken or 0) + 1
		local tok = self.autoToken
		local function shot()
			if not (Deagle.state.trigger and Deagle.autoToken == tok and Deagle.state.drawn) then return end
			Deagle:Fire()
			if Deagle.state.ammo > 0 then Script.SetTimer(math.max(1, math.floor(Deagle.cfg.interval * 1000)), shot) end
		end
		shot()
	elseif mode == "burst" then
		local now = System.GetCurrTime()
		local gap = c.burstShotGap
		if now < (s.burstReady or 0) or now - s.lastShot < c.interval - 0.005 then return end
		s.burstReady = now + 2 * gap + c.burstInterval
		self:Fire(false, 1)
		for i = 1, 2 do
			Script.SetTimer(math.floor(i * gap * 1000), function() if Deagle.state.drawn then Deagle:Fire(true, i + 1) end end)
		end
	else
		self:Fire()
	end
end

-- handling from the gun's CS2 weapon (Scripts/Deagle/guns/cs2.lua): damage, headshots, range falloff, fire rate,
-- recoil (scaled so the Deagle keeps the kick it was tuned with), fire modes. Without one the defaults above stay.
local BASE = { damage = Deagle.cfg.damage, headshotMult = Deagle.cfg.headshotMult, interval = Deagle.cfg.interval,
	rangeMod = Deagle.cfg.rangeMod, kickPitch = Deagle.cfg.kickPitch, kickPitchVar = Deagle.cfg.kickPitchVar, kickYaw = Deagle.cfg.kickYaw }
-- CS2's accuracy, for guns with a CS2 profile (user: "like CS2", it was too hard to hit; Ready or Not's guns will
-- bring their own): CS:GO / CS2 GetInaccuracy with the gun's own numbers (guns/cs2.lua, radians). Inaccuracy standing
-- or crouched (KCD2 gives Lua no stance: crouched = the eye under cfg.accuracy.crouchEye; measured live 1.54 m
-- standing, 1.02 crouched), + moving (a ramp from 34 % to 95 % of the gun's max speed, so walking slowly costs nothing),
-- + in the air (jumping, falling), + what recent shots left: each shot adds inFire, gone to 10 % over the recovery
-- time (shorter crouched). Returns the inaccuracy and the base spread for this shot.
function Deagle:CsAccuracy(w)
	local s, a, p = self.state, self.cfg.accuracy, self:Player()
	local now = System.GetCurrTime()
	local eye = System.GetViewCameraPos().z - p:GetWorldPos().z
	s.crouched = eye < a.crouchEye + (s.crouched and a.crouchBand or -a.crouchBand)   -- a band so a bob can't flicker it
	local ok, v = pcall(p.GetVelocity, p)
	local speed = ok and v and math.sqrt(v.x * v.x + v.y * v.y) or 0
	local air = false
	pcall(function() air = p.actor:IsFlying() end)
	s.accFire = (s.accFire or 0) * math.exp(-math.log(10) * math.max(0, now - (s.accT or now)) / (s.crouched and w.recCrouch or w.recStand))
	s.accT = now
	local lo, hi = 0.34 * w.maxSpeed, 0.95 * w.maxSpeed
	local move = math.max(0, math.min(1, (speed - lo) / (hi - lo)))
	local k = a.scale
	return k * ((s.crouched and w.inCrouch or w.inStand) + move * w.inMove + (air and w.inJump or 0) + s.accFire), k * w.spread
end

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

function Deagle:LoadGuns()
	local function load(f) return self:LoadModule(f) end
	pcall(load, "Scripts/Deagle/guns/cs2.lua")
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
