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
	range = 250,
	damage = 60,              -- health damage (game applies body_part coeff: head 1.5, limbs 0.7)
	headshotMult = 3.0,       -- extra on top: .50 AE to the head should drop anyone
	stamina = 40,
	impulse = 220,            -- physics push on props and bodies
	spreadHip = 1.6,          -- degrees
	spreadAim = 0.25,
	aimFovScale = 0.8,
	hitMode = "soul",         -- "soul" = soul:DealDamage with native hit reactions; "hit" = gamerules ServerHit (no effect on souls in KCD2)
	hitType = "bullet",
	shotTrigger = "c_f_firearm_shot",   -- the game's gunshot; skipped while the plugin plays the MWII shots
	dryTrigger = nil,
	reloadTrigger = nil,
	muzzleFx = "deagle.muzzle",         -- Libs/Particles/deagle.xml (preloaded via PreloadList.txt): the flash, rides on the gun
	muzzleFxLarge = "deagle.muzzle_large",   -- the same flash burning longer, for big blasts (cfg.physics.largeFlashBlast)
	muzzleSmokeFx = "deagle.muzzle_smoke",   -- smoke + barrel wisp, left in the world
	-- "gun": the flash rides the muzzle, moved with the gun every frame (like CS2 / CoD / Ready or Not); "world": it stays
	-- where it was fired
	flashMode = "gun",
	slowmo = 0.1,             -- F9 (deagle_slowmo): game speed while slow motion is on, for checking effects
	-- a shot after the old mag has left the gun mid-reload: false = only the chambered round is left (the gun has no mag
	-- until the next reload, like real guns); true = arcade, the rounds you had stay and the mag pops back
	cancelKeepsRounds = false,
	muzzleFxScale = 1.0,      -- multiplier on the flash size the cartridge gives (cfg.physics.flashPerUnit)
	portFx = "deagle.port_smoke",
	bloodFx = "collisions.combat.hit_passed_bullet_fabric",
	armorFx = "collisions.combat.hit_passed_bullet_plate",
	headFx = "collisions.combat.blood_head",
	bloodPaint = 0.45,        -- actor:AddBlood amount per hit on that body part
	bleeding = true,          -- non-lethal hits apply the game's bleeding buff
	bleedingBuff = "0c903899-fcc9-4cf2-9ee3-1130ac08b0fc",
	armorFxThreshold = 0.5,   -- armor ratio above which hits spark instead of splash
	splatMaterials = { "Materials/particles/blood_decalshoot", "Materials/particles/blood_decalgun", "Materials/particles/blood_decal" },
	splatRange = 3.0,         -- how far behind the target blood can land
	splatSize = { 0.45, 0.9 },
	splatLife = 180,
	recoil = 1.0,
	-- cartridge-driven handling (guns/calibers.lua -> ShotPhysics): physics -> screen. PROVISIONAL constants, set so the
	-- .50 AE Deagle kicks ~4.5 deg; to be fitted to Ready or Not's values for the same guns. A gun with no cartridge
	-- data keeps kickPitch / kickYaw below.
	physics = {
		camPerFlip = 0.217,     -- camera kick (deg) per rad/s of muzzle flip (Deagle 20.7 rad/s -> 4.5 deg, Glock 7.1 -> 1.5)
		-- elastic hold (design 1): the grip springs back up to holdRange degrees of camera kick; what goes past it
		-- stays (you pull it down). A calm Deagle shot returns 65 %, fast follow-ups much less, a Glock ~95 %.
		holdRange = 2.9,        -- deg, two hands, hip (Ready or Not's RecoilReturnPercentage calibrates it)
		holdMax = 0.95,         -- most of a kick that ever comes back by itself
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
		gunBackPerVel = 0.007,  -- gun pushed back (m) per m/s of free recoil (Deagle 4.95 -> 3.5 cm)
		gunFlipPerFlip = 0.29,  -- gun model flip (deg) per rad/s, on top of the shot animation
		flashPerUnit = 0.30,    -- flash size per unit of blast scale G (9 mm = 1: Glock 0.30, Deagle 0.62)
		damageRef = 35,         -- HP a 9 mm 124 gr FMJ at 360 m/s takes from the torso (3 hits on a 100 HP NPC)
		staminaRef = 20,        -- stamina a 9 mm's shove takes; others by momentum
		headMult = 2.0,         -- on a head hit, on top of KCD2's own x1.5: a round through the skull kills
		punchPerMomentum = 25.6,-- physics push on bodies / props per kg m/s of bullet momentum (Deagle 8.6 -> 220)
		largeFlashBlast = 1.5,  -- blast scale from which the flash burns longer (3-4 frames instead of 2-3; Deagle 2.08)
		loudnessSpan = 0.5,     -- share of the real loudness difference played (Deagle +6.5 dB over the Glock -> 0.69x)
	},
	kickPitch = 2.6,          -- degrees of muzzle climb per shot
	kickYaw = 0.8,
	modelDir = "objects/manmade/weapons/deagle/",
	ejectImpulse = 0.035,     -- casing (20 g) out at ~1.75 m/s, up and to the right
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
	soundVol = { shot = 1.0, foley = 0.75 },
	motion = 1.0,             -- plugin bob / turn sway / sprint pose / landing strength (deagle_motion)
	arms = Deagle.cfg and Deagle.cfg.vm and Deagle.cfg.vm.arms or "mw",   -- "mw" or "medieval" (deagle_arms)
}

-- ammo HUD, drawn every frame with System.DrawText (bottom right); falls back to info text if DrawText is missing
Deagle.cfg.hud = { enabled = true, x = 0.885, y = 0.86, size = 2.6, font = "default",
	col = { 1.0, 0.95, 0.86, 0.92 }, empty = { 0.95, 0.35, 0.3, 0.95 } }

Deagle.state = Deagle.state or {
	ammo = 7,
	drawn = false,
	aiming = false,
	reloading = false,
	lastShot = -100,
	logActions = false,
	baseFov = nil,
}

-- MWII sound cues per clip (seconds from clip start), from assets/build/vm_sound_cues.json; "shot" = random gunshot
Deagle.vmSoundCues = {
	["shoot1"] = { { 0, "shot" } },
	["shoot2"] = { { 0, "shot" } },
	["shoot_empty"] = { { 0, "shot" } },
	["reload"] = { { 0.000, "decho/p25_pi_decho_reload_raise.wav" }, { 0.667, "decho/p25_pi_decho_reload_magrelease.wav" }, { 0.708, "decho/p25_pi_decho_reload_magout.wav" }, { 1.042, "decho/p25_pi_decho_reload_magfindwell.wav" }, { 1.375, "decho/p25_pi_decho_reload_magin.wav" }, { 1.750, "decho/p25_pi_decho_reload_end.wav" } },
	["draw"] = { { 0.000, "decho/p25_pi_decho_raise_first_raise.wav" }, { 0.417, "decho/p25_pi_decho_raise_first_switch.wav" }, { 0.625, "decho/p25_pi_decho_raise_first_end.wav" } },
	["reload_empty"] = { { 0.000, "decho/p25_pi_decho_reload_empty_raise.wav" }, { 0.125, "decho/p25_pi_decho_reload_empty_pivot.wav" }, { 0.208, "decho/p25_pi_decho_reload_empty_magout.wav" }, { 0.917, "decho/p25_pi_decho_reload_empty_magfindwell.wav" }, { 1.125, "decho/p25_pi_decho_reload_empty_magin.wav" }, { 1.375, "decho/p25_pi_decho_reload_empty_tilt.wav" }, { 1.750, "decho/p25_pi_decho_reload_empty_slideback.wav" }, { 1.833, "decho/p25_pi_decho_reload_empty_slideclose.wav" }, { 2.042, "decho/p25_pi_decho_reload_empty_end.wav" } },
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
	local cues, shots = {}, {}
	for key, seq in pairs(CUE_SEQ) do cues[key] = Deagle.vmSoundCues[seq] end
	for i = 1, 8 do shots[i] = "decho/decho-" .. i .. ".wav" end
	Deagle:RegisterGun("deagle", { title = "Desert Eagle", magSize = Deagle.cfg.magSize, cdf = Deagle.cfg.vm.cdf, anims = Deagle.cfg.vm.anims,
		cues = cues, shotSounds = shots, bones = { muzzle = "tag_flash", brass = "tag_brass" }, rounds = 6, mags = true,
		swapTail = { reload = 0.6 }, newMagAt = 0.69, armsLooks = true, cs2 = "weapon_deagle",
		cycle = 0.08,             -- the mod's weapon script (cycletime): CS2's Deagle is 0.225
		-- the clips were exported frame for frame at 24 fps, but the mod's sequences run at 30 (shots at 40): play them
		-- faster (measured against YabrakProds' own video of the mod: reload back at idle ~1.7 s, as at 30 fps)
		rate = { default = 30 / 24, shoot = 40 / 24, shootAdd = 40 / 24 },
		-- clip-timeline seconds (tools/clip_times.py): clean reload cancel, new mag seated, ready to fire, settled
		times = { reload = { cancel = 0.375, out = 0.833, ready = 2.375, seat = 1.375, trim = 2.375 },
			reloadEmpty = { cancel = 0.333, out = 0.333, ready = 2.292, seat = 1.125, trim = 2.542 },
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
local GAME_MAPS_OFF = { "combat", "draw_holster_torch" }   -- KCD2 action maps on our keys, off while the gun is out

-- ---------------------------------------------------------------------------
-- helpers
-- ---------------------------------------------------------------------------
-- Console %line / %1 substitutions arrive wrapped in double quotes.
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

function Deagle:SpawnFx(name, pos, normal, scale)
	if not name then return end
	if name == self.cfg.muzzleFx then scale = self.cfg.muzzleFxScale end
	try("fx " .. name, Particle.SpawnEffect, name, pos, normal, scale or 1.0)
end

-- The flash rides the gun on a small entity of its own: the plugin moves it onto the muzzle in the same call that
-- places the gun, so it sits on the muzzle in every frame drawn (Lua runs before the camera is final, so whatever Lua
-- alone puts on the gun trails it by a frame). Checked in game 2026-10-12 (slow motion, scripted camera turns, recorded):
-- effects attached to the viewmodel character (bone attachments) showed on some shots only, never on the Deagle, and
-- trailed by a frame; an emitter in a slot of the viewmodel entity never renders; an emitter on a separate entity
-- renders every time. KCD2's LoadParticleEffect takes CryEngine's table (slot, effect, { Scale = ... }): plain
-- arguments create no slot. A slot still holding the last flash doesn't restart: two slots, the older one cleared.
-- flashMode "world" leaves the flash where the muzzle was instead. No fallback: a failure is logged. Smoke stays in
-- the world.
local FLASH_FOLLOW = 0.3   -- s of game time the flash entity keeps following the muzzle (the longest flash part lives 0.11)
local function vsub(a, b) return { x = a.x - b.x, y = a.y - b.y, z = a.z - b.z } end
local function vdot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
local function entityHex(id) return string.match(tostring(id), "(%x+)%s*$") or "0" end

function Deagle:MuzzleFlash(dir)
	local c = self.cfg
	local pos = self:MuzzlePos()
	local phys = self:ShotPhysics()
	local scale = (phys and c.physics.flashPerUnit * phys.blast or 1) * c.muzzleFxScale
	-- a real flash is over in 1-2 ms; games show it 2-4 frames. A bigger blast burns a little longer (the engine can't
	-- stretch a flash per shot, so two versions of it).
	local fx = (phys and phys.blast >= c.physics.largeFlashBlast) and c.muzzleFxLarge or c.muzzleFx
	if c.flashMode == "world" then
		try("fx", Particle.SpawnEffect, fx, pos, dir, scale)
	elseif self.vmInFp and self.vmShown then
		local vm = self.vmId and System.GetEntity(self.vmId)
		local e = vm and self:FlashEntity()
		local okM, lp, ld = false, nil, nil
		if e then okM, lp, ld = pcall(self.MuzzleLocal, self, vm) end
		if not (e and okM) then
			log("$4muzzle flash: " .. (e and ("no muzzle on the gun: " .. tostring(lp)) or "no flash entity"))
		else
			self:FlashPlace(e, vm, lp, ld)
			self.flashSlot = self.flashSlot == 0 and 1 or 0
			e:FreeSlot(self.flashSlot)
			local okL, slot = pcall(e.LoadParticleEffect, e, self.flashSlot, fx, { Scale = scale, bPrime = false })
			if not okL or type(slot) ~= "number" then log("$4muzzle flash: " .. fx .. " didn't load: " .. tostring(slot)) end
			self.flashUntil = System.GetCurrTime() + FLASH_FOLLOW
			self:Native(string.format("flash 0x%s %.4f %.4f %.4f %.4f %.4f %.4f", entityHex(e.id), lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
		end
	end
	try("fx", Particle.SpawnEffect, c.muzzleSmokeFx, pos, dir, scale / 0.6)
end

function Deagle:FlashEntity()
	local e = self.flashId and System.GetEntity(self.flashId)
	if e then return e end
	e = System.SpawnEntity({ class = "BasicEntity", name = "deagle_flash", position = System.GetViewCameraPos(),
		flags = ENTITY_FLAG_NO_SAVE, properties = { object_Model = "" } })
	if not e then return nil end
	e:SetFlags(ENTITY_FLAG_NO_SAVE, 0)   -- never into a save
	self.flashId, self.flashSlot = e.id, nil
	return e
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

-- put the flash entity on the muzzle from Lua (the plugin, when it places the gun, does it again later in the frame)
function Deagle:FlashPlace(e, vm, lp, ld)
	local o, X, Y, Z = vm:GetWorldPos(), vm:GetDirectionVector(0), vm:GetDirectionVector(1), vm:GetDirectionVector(2)
	local function world(v) return vadd(vadd(vscale(X, v.x), vscale(Y, v.y)), vscale(Z, v.z)) end
	e:SetWorldPos(vadd(o, world(lp)))
	e:SetDirectionVector(world(ld))
end

-- every frame while a flash burns: the muzzle moves with the shot animation; afterwards the plugin lets go
function Deagle:FlashFollow()
	if not self.flashUntil then return end
	local e, vm = self.flashId and System.GetEntity(self.flashId), self.vmId and System.GetEntity(self.vmId)
	if System.GetCurrTime() >= self.flashUntil or not (e and vm) then
		self.flashUntil = nil
		self:Native("flashoff")
		return
	end
	local ok, lp, ld = pcall(self.MuzzleLocal, self, vm)
	if not ok then log("$4muzzle flash: lost the muzzle: " .. tostring(lp)) self.flashUntil = nil self:Native("flashoff") return end
	self:FlashPlace(e, vm, lp, ld)
	self:Native(string.format("flashpos %.4f %.4f %.4f %.4f %.4f %.4f", lp.x, lp.y, lp.z, ld.x, ld.y, ld.z))
end

-- slow motion for checking effects (F9): KCD2 has CryEngine's t_Scale
function Deagle:SlowMo(on)
	self.slowOn = on and true or false
	pcall(System.SetCVar, "t_Scale", on and self.cfg.slowmo or 1)
	log("slow motion " .. (on and ("x" .. tostring(self.cfg.slowmo)) or "off"))
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
function Deagle:ShotGain(file)
	local g = self.shotGain and self.shotGain[file]
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
function Deagle:Wound(phys, dist)
	local m = self.cfg.physics
	local eIn = phys.energy * math.exp(-2 * (dist or 0) / phys.dragLength)
	local ref = self.calibers["9x19"]
	local eRef = deposited(ref.bullet, ref.d, 0.5 * ref.bullet * 360 * 360)
	local health = m.damageRef * math.sqrt(deposited(phys.bullet, phys.d, eIn) / eRef)
	local stamina = m.staminaRef * (math.sqrt(2 * eIn / phys.bullet) * phys.bullet) / (ref.bullet * 360)
	return health, stamina, eIn
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
	local up = m.camPerFlip * phys.flip * (1 + m.vary * self:SprayRand(n, 1)) * aim * self.cfg.recoil
	local side = up * ((ph.side or 0) + m.sideSigma * self:SprayRand(n, 2))
	local range = m.holdRange * m.holds[ph.hold] * (s.aiming and m.adsHold or 1)
	local r = self.camRecoil or { pitch = 0, yaw = 0, applied = { pitch = 0, yaw = 0 }, perm = { pitch = 0, yaw = 0 } }
	self.camRecoil = r
	r.rate = m.returnRate * math.sqrt(m.returnInertia / phys.inertia)
	local back = math.min(m.holdMax * up, math.max(0, range - r.pitch))
	r.pitch, r.perm.pitch = r.pitch + back, r.perm.pitch + up - back
	local sideBack = math.min(m.holdMax * math.abs(side), math.max(0, range * m.holdYaw - math.abs(r.yaw)))
	if side < 0 then sideBack = -sideBack end
	r.yaw, r.perm.yaw = r.yaw + sideBack, r.perm.yaw + side - sideBack
	self.camRecoilT = System.GetCurrTime()
	if self.native then
		local flip = m.gunFlipPerFlip * phys.flip * aim
		local yaw = up > 0 and flip * side / up or 0
		self:Native(string.format("kick %.4f %.3f %.3f %.3f", m.gunBackPerVel * phys.recoilVel * aim, flip, yaw, m.gunRollPerYaw * yaw))
		self.gunKicks = self.gunKicks or {}
		table.insert(self.gunKicks, { t = self.camRecoilT, pitch = flip, yaw = yaw })
	end
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
		if t < 0.6 then
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

-- ---------------------------------------------------------------------------
-- hits
-- ---------------------------------------------------------------------------
-- Bullet impact effect for the surface the ray hit (Libs/Particles/collisions.xml, destructibles.bullet_*).
local surfaceFx = {
	{ "water", "mudwater" }, { "mud", "mud" }, { "stone", "stone" }, { "rock", "stone" }, { "brick", "stone" },
	{ "wood", "wood" }, { "metal", "metal" }, { "iron", "metal" }, { "grass", "grass" }, { "plant", "grass" },
	{ "veget", "grass" }, { "plaster", "plaster" }, { "straw", "straw" }, { "hay", "straw" }, { "cloth", "sack" },
	{ "sack", "sack" }, { "fabric", "sack" }, { "ceram", "ceramics" }, { "pottery", "ceramics" }, { "clay", "ceramics" },
}

local function impactFxFor(hit)
	local name = ""
	pcall(function() name = string.lower(System.GetSurfaceTypeNameById(hit.surface) or "") end)
	for _, pair in ipairs(surfaceFx) do
		if string.find(name, pair[1], 1, true) then return "collisions.destructibles.bullet_" .. pair[2], name end
	end
	return "collisions.destructibles.bullet_soil", name
end

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

-- Blood on whatever is behind the target (wall, floor) plus a pool under the hit point.
function Deagle:Splatter(e, hit, dir)
	local c = self.cfg
	local function decal(pos, normal, scale)
		local size = (c.splatSize[1] + math.random() * (c.splatSize[2] - c.splatSize[1])) * (scale or 1)
		for _, mat in ipairs(c.splatMaterials) do
			local ok = pcall(Particle.CreateMatDecal, vadd(pos, vscale(normal, 0.01)), normal, size, c.splatLife, mat)
			if ok then return end
		end
	end
	local from = vadd(hit.pos, vscale(dir, 0.15))
	local hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, from, vscale(dir, c.splatRange), 1, ent_all, e.id, self:Player().id, hits)
	if ok and n and n > 0 and hits[1] then decal(hits[1].pos, hits[1].normal, 1) end
	hits = {}
	ok, n = pcall(Physics.RayWorldIntersection, hit.pos, { x = dir.x * 0.4, y = dir.y * 0.4, z = -2.5 }, 1, ent_static + ent_terrain, e.id, nil, hits)
	if ok and n and n > 0 and hits[1] then decal(hits[1].pos, hits[1].normal, 0.6) end
end

function Deagle:Bleed(e, hit, dir)
	local c = self.cfg
	local part = hit.bodyPart
	local armored = false
	pcall(function() armored = e.actor:GetArmor() / math.max(1, e.actor:GetMaxArmor()) > c.armorFxThreshold end)
	-- spray goes out of the exit side, along the bullet
	self:SpawnFx(armored and c.armorFx or c.bloodFx, hit.pos, dir)
	self:SpawnFx(c.bloodFx, vadd(hit.pos, vscale(dir, 0.25)), dir, 1.3)  -- exit wound
	if part == "head" then self:SpawnFx(c.headFx, hit.pos, dir) end
	self:Splatter(e, hit, dir)
	if part and c.bloodPaint > 0 then
		try("paint blood", function() e.actor:AddBlood(c.bloodPaint, part) end)
	end
	local dead = false
	pcall(function() dead = e.actor:IsDead() or e.actor:GetHealth() <= 0 end)
	if c.bleeding and not dead then
		try("bleeding", function() e.soul:AddBuff(c.bleedingBuff) end)
	end
end

function Deagle:ApplyHit(hit, dir)
	local c = self.cfg
	local e = hit.entity
	local p = self:Player()
	local dead = false
	if e and e.actor then pcall(function() dead = e.actor:IsDead() end) end
	if e and e ~= p and (dead or (e.class and string.find(string.lower(e.class), "dead"))) then
		hit.bodyPart = bodyPartFor(e, hit.pos)
		self:Bleed(e, hit, dir)
		log("corpse hit " .. tostring(e:GetName()))
	elseif e and e.soul and e ~= p then
		if c.hitMode == "hit" then
			try("CreateHit", function()
				g_gameRules:CreateHit(e.id, p.id, p.id, c.damage, 0, nil, hit.partid or -1, c.hitType, hit.pos, dir, hit.normal)
			end)
		else
			local part = bodyPartFor(e, hit.pos)
			local dmg, stam
			local phys = self:ShotPhysics()
			if phys then        -- from the cartridge: energy left in the body, air drag over the distance
				dmg, stam = self:Wound(phys, hit.dist)
				if part == "head" then dmg = dmg * c.physics.headMult end
			else                -- a gun without cartridge data: its CS2 numbers
				dmg = c.damage * (part == "head" and c.headshotMult or 1) * c.rangeMod ^ ((hit.dist or 0) / 12.7)
				stam = c.stamina
			end
			hit.bodyPart = part
			try("DealDamage", function() e.soul:DealDamage(stam, dmg, p.id, false, part) end)
		end
		self:Bleed(e, hit, dir)
		local hp = "?"
		pcall(function() hp = string.format("%.1f/%.1f", e.actor:GetHealth(), e.actor:GetMaxHealth()) end)
		log("hit " .. tostring(e:GetName()) .. " mode " .. c.hitMode .. " part " .. tostring(hit.bodyPart) .. " dist " .. string.format("%.1f", hit.dist or -1) .. " hp " .. hp)
	else
		local fx, surface = impactFxFor(hit)
		self:SpawnFx(fx, hit.pos, hit.normal)
		if self.state.logActions then log("impact " .. tostring(surface) .. " -> " .. fx) end
	end
	if e and e.AddImpulse and (not e.soul or (e.IsDead and e:IsDead())) then
		local phys = self:ShotPhysics()
		local push = phys and c.physics.punchPerMomentum * phys.momentum or c.impulse   -- the shove follows momentum
		try("impulse", function() e:AddImpulse(hit.partid or -1, hit.pos, dir, push, 1) end)
	end
end

-- ---------------------------------------------------------------------------
-- shooting, casings
-- ---------------------------------------------------------------------------
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

function Deagle:EjectCase()
	local port = self:VmBone(self.gun.bones.brass)
	if not port then return end
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local case = self:CaseEntity()
		case:SetWorldPos(port)
		local jr, ju, jf = 0.7 + math.random() * 0.4, 0.9 + math.random() * 0.5, -0.3 + math.random() * 0.4
		local dir = vnorm(vadd(vscale(r, jr), vadd(vscale(u, ju), vscale(f, jf))))
		local spinAt = vadd(port, { x = (math.random() - 0.5) * 0.02, y = (math.random() - 0.5) * 0.02, z = (math.random() - 0.5) * 0.02 })
		case:AddImpulse(-1, spinAt, dir, c.ejectImpulse * (0.8 + math.random() * 0.45), 1)
	end)
	self:SpawnFx(c.portFx, port, System.GetViewCameraUpDir(), 1.0)
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
		self:PlayTrigger(c.dryTrigger)
		self:ShowAmmo()
		return false
	end
	s.heat = math.max(0, (s.heat or 0) - (now - s.lastShot) * c.heatDecay)
	s.lastShot = now
	s.ammo = s.ammo - 1

	local pos = System.GetViewCameraPos()
	local dir = coneDir(System.GetViewCameraDir(), (s.aiming and c.spreadAim or c.spreadHip) + s.heat)
	-- the bullet leaves along the barrel, which may still be kicked up / sideways from the last shot
	local kp, ky = self:GunKickNow()
	if kp ~= 0 or ky ~= 0 then
		local _, _, right, upv = camBasis()
		local k = c.physics.barrelAim
		dir = vnorm(vadd(dir, vadd(vscale(upv, math.rad(kp) * k), vscale(right, math.rad(ky) * k))))
	end
	s.heat = s.heat + c.heatPerShot

	self:PlayTrigger(c.shotTrigger)
	self:MuzzleFlash(System.GetViewCameraDir())
	self:Recoil()
	self:EjectCase()

	local hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, pos, vscale(dir, c.range), 1, ent_all, p.id, nil, hits)
	if ok and n and n > 0 and hits[1] then
		self:ApplyHit(hits[1], dir)
	elseif not ok then
		log("$4raycast failed: " .. tostring(n))
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
	end
	if self.vmShown and not blocked then
		self:VmPlay(self:VmRestKey(), true, 1.0, blend or (not self:VmBusy() and self.cfg.vm.adsBlend or nil))
	end
end

-- zoom in / out over adsBlend (smoothstep) instead of snapping
function Deagle:EaseFov(on)
	local s = self.state
	try("aim fov", function()
		if on and not s.baseFov then s.baseFov = System.GetViewCameraFov() end
		if not s.baseFov then return end
		local from, to, t0 = s.fovNow or s.baseFov, s.baseFov * (on and self.cfg.aimFovScale or 1), System.GetCurrTime()
		self.fovToken = (self.fovToken or 0) + 1
		local tok = self.fovToken
		local function step()
			if Deagle.fovToken ~= tok then return end
			local k = math.min(1, (System.GetCurrTime() - t0) / math.max(0.01, Deagle.cfg.vm.adsBlend))
			k = k * k * (3 - 2 * k)
			s.fovNow = from + (to - from) * k
			System.SetViewCameraFov(s.fovNow)
			if k < 1 then Script.SetTimer(0, step)
			elseif not on then s.baseFov, s.fovNow = nil, nil end   -- fully out: read the game's FOV again next time
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
		if ok and type(name) == "string" and name:find("^deagle_") and not name:find("^deagle_case_") and e.id ~= self.vmId
			and e.id ~= self.flashId then
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
	self.vmId, self.vmRoundsShown = vm.id, nil
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

function Deagle:VmTick(token)
	if token ~= self.vmToken or not self.vmShown then return end
	self:VmPlace()
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
		self:PlayCues(key)
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

-- sound cues of a clip, played by the plugin; cues of an interrupted clip are dropped (shots always play)
function Deagle:PlayCues(key)
	local g = self.gun
	local cues = g.cues[key] or ((key == "shoot" or key == "shootEmpty") and SHOT_CUE) or nil
	if not cues then return end
	local tok = self.vmAnimToken
	local v = self.cfg.vm.soundVol
	local rate = self:Rate(key)
	for _, c in ipairs(cues) do
		local t, file = c[1] / rate, c[2]
		local function fire()
			if Deagle.vmAnimToken ~= tok and key ~= "shoot" and key ~= "shootEmpty" then return end
			if file == "shot" then
				local shots = g.shotSounds or {}
				if #shots > 0 then local f = shots[math.random(1, #shots)] Deagle:Snd(f, v.shot * Deagle:ShotGain(f)) end
			else Deagle:Snd(file, v.foley) end
		end
		if t <= 0.001 then fire() else Script.SetTimer(math.floor(t * 1000), fire) end
	end
end

-- ---------------------------------------------------------------------------
-- draw / holster
-- ---------------------------------------------------------------------------
function Deagle:VmShow()
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
	self.vmShown = false
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
end

function Deagle:Snd(file, vol)
	if self.native then self:Native(string.format("snd %s %.2f", file, vol or 1)) end
end

-- ---------------------------------------------------------------------------
-- input: our action map + a wrapper around the player's action hook
-- ---------------------------------------------------------------------------
function Deagle:OnAction(action, activation)
	local s = self.state
	if action == "deagle_inspect" and s.drawn then
		if activation == "press" then self:Inspect() end
		return true
	end
	if not self.inputLoaded then self:EnsureInit() end
	if s.reloading then self:FinishReload() end
	if s.logActions then log("action " .. tostring(action) .. " " .. tostring(activation)) end
	if action == "deagle_toggle" then
		if activation == "press" then self:Toggle() end
		return true
	end
	if action == "deagle_slowmo" then
		if activation == "press" then self:SlowMo(not self.slowOn) end
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
		local ok, err = pcall(function() consumed = Deagle:OnAction(action, activation) end)
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
	self.vmId, self.casePool, self.flashId, self.flashUntil = nil, nil, nil, nil   -- a load ends the old world: its ids may now name other entities
	if self.slowOn then self:SlowMo(false) end
	self:RemoveStaleEntities()   -- at load, not just on draw: a holstered gun would leave the pyramid in view
	self:NativeHello()
end

-- ---------------------------------------------------------------------------
-- console
-- ---------------------------------------------------------------------------
function Deagle:Set(line)
	local key, value = string.match(unquote(line), "^%s*(%S+)%s+(.-)%s*$")
	if not key then
		log("usage: deagle_set <key> <value>   (nested: deagle_set physics.camPerFlip 0.3)")
		return
	end
	local t, k = self.cfg, key
	for part, rest in string.gmatch(key, "([^.]+)%.(.+)") do t, k = t[part], rest end   -- one level of nesting
	if type(t) ~= "table" then log("no setting " .. key) return end
	local num = tonumber(value)
	if value == "nil" then t[k] = nil elseif num then t[k] = num else t[k] = value end
	log("cfg." .. key .. " = " .. tostring(t[k]))
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

-- dev: re-run this script from a loose file (tools/live.sh reload), run a Lua line
Deagle.devPath = Deagle.devPath or "E:/Tools/deagle-dev/deagle_main.lua"
function Deagle:DevReload()
	local ok, err = pcall(function()
		local chunk, perr = loadfile(self.devPath)
		if not chunk then error(perr) end
		chunk()
	end)
	log(ok and ("dev reload ok: " .. self.devPath) or ("$4dev reload failed: " .. tostring(err)))
end

function Deagle:DevLua(line)
	local chunk, err = loadstring(unquote(line))
	if not chunk then log("$4lua: " .. tostring(err)) return end
	local ok, res = pcall(chunk)
	log("lua -> " .. tostring(ok) .. " " .. tostring(res))
end

System.AddCCommand("deagle_toggle", "Deagle:EnsureInit() Deagle:Toggle()", "Draw / holster the Desert Eagle")
System.AddCCommand("deagle_fire", "Deagle:EnsureInit() Deagle:Fire()", "Fire one shot")
System.AddCCommand("deagle_reload", "Deagle:Reload()", "Reload")
System.AddCCommand("deagle_inspect", "Deagle:Inspect()", "Play the inspect animation")
System.AddCCommand("deagle_give", "Deagle:EnsureInit() Deagle:Give()", "Add the Desert Eagle (if missing) and 49 cartridges")
System.AddCCommand("deagle_give_ammo", "Deagle:EnsureInit() Deagle:Give([[%1]])", "deagle_give_ammo <n>: add n cartridges")
System.AddCCommand("deagle_arms", "Deagle.cfg.vm.arms = (([[%1]]):find('mw') and 'mw' or 'medieval') Deagle.vmArmsApplied = nil Deagle:VmApplyArms()", "deagle_arms medieval|mw: viewmodel sleeve/glove look")
System.AddCCommand("deagle_motion", "Deagle.cfg.vm.motion = tonumber((([[%1]]):gsub('\"',''))) or 1 Deagle:NativeSyncVm()", "deagle_motion <0..2>: viewmodel bob/sway strength (0 = off)")
System.AddCCommand("deagle_ads", "Deagle.cfg.vm.ads = (([[%1]]):find('0') == nil) Deagle:SetAim(false)", "deagle_ads 1|0: aim down sights on right click")
System.AddCCommand("deagle_hud", "Deagle.cfg.hud.enabled = (([[%1]]):find('0') == nil) Deagle.hudOk = nil", "deagle_hud 1|0: ammo HUD on/off")
System.AddCCommand("deagle_set", "Deagle:Set([[%line]])", "deagle_set <key> <value>: change a setting live")
System.AddCCommand("deagle_cfg", "Deagle:Cfg()", "Print all settings")
System.AddCCommand("deagle_status", "Deagle:EnsureInit() Deagle:Status()", "Print hook / input / ammo state")
System.AddCCommand("deagle_input", "Deagle:LoadInput()", "Reload the Deagle key bindings")
System.AddCCommand("deagle_log_actions", "Deagle.state.logActions = not Deagle.state.logActions", "Toggle logging of every input action")
System.AddCCommand("deagle_fx", "Deagle:FxTest([[%1]])", "deagle_fx <effect>: spawn a particle effect 3 m ahead")
System.AddCCommand("deagle_vm_respawn", "Deagle:VmRespawn()", "Rebuild the viewmodel entity")
System.AddCCommand("deagle_native_ok", "Deagle.native = true; System.LogAlways('[Deagle] native plugin linked')", "internal: native plugin handshake")
System.AddCCommand("deagle_native_test", "Deagle:NativeHello() Deagle:Snd('decho/decho-1.wav', 1)", "ping the native plugin and play a shot sound")
System.AddCCommand("deagle_dev_reload", "Deagle:DevReload()", "Re-run the Deagle script from the dev path")
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
	local load = Script.ReloadScript or Script.LoadScript
	pcall(load, "Scripts/Deagle/guns/cs2.lua")
	pcall(load, "Scripts/Deagle/guns/calibers.lua")
	pcall(load, "Scripts/Deagle/guns/shot_gain.lua")
	pcall(load, "Scripts/Deagle/guns/index.lua")
	for _, id in ipairs(self.gunIds or {}) do pcall(load, "Scripts/Deagle/guns/" .. id .. ".lua") end
end
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
		hp, hp50, hp * m.headMult * 1.5, st))
end
System.AddCCommand("deagle_physics", "Deagle:PrintPhysics()", "deagle_physics: what the current gun's cartridge does (recoil, flip, flash, energy)")
System.AddCCommand("deagle_gun", "Deagle:UseGun((([[%1]]):gsub('\"','')))", "deagle_gun <id>: which gun the pistol looks / sounds like (no id: list)")

log("loaded v2.1")
