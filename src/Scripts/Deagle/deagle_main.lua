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
	interval = 0.22,          -- seconds between shots (semi-auto cap)
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
	muzzleFx = "deagle.muzzle",         -- Libs/Particles/deagle.xml (preloaded via PreloadList.txt)
	muzzleFxScale = 1.0,
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
	flashRadius = 9,
	flashIntensity = 6,
	flashMs = 45,
	lightSlot = 15,
	recoil = 1.0,
	kickPitch = 2.6,          -- degrees of muzzle climb per shot
	kickYaw = 0.8,
	modelDir = "objects/manmade/weapons/deagle/",
	ejectImpulse = 0.06,
	caseLifeMs = 20000,
}

-- The viewmodel: its own character placed on the camera (its camera bone tag_view is the model origin, +Y forward),
-- so the arms never depend on Henry's body pose, pitch or strafing.
Deagle.cfg.vm = {
	cdf = "objects/manmade/weapons/deagle/vm/deagle_vm.cdf",
	chr = "objects/manmade/weapons/deagle/vm/deagle_vm.chr",
	skin = "objects/manmade/weapons/deagle/vm/deagle_vm_skin.skin",
	offset = { fwd = 0.0, right = 0.0, up = 0.0 },   -- camera-space nudge (m)
	fps = 24, layer = 0, blend = 0.08,
	recoilLayer = 1,          -- aimed shots: additive recoil on top of the ADS pose
	adsBlend = 0.14,          -- seconds to blend hip <-> ADS (pose and zoom)
	adsLead = 0.2,            -- reload / draw hand over to a held aim this long before they end, blending as long
	holsterSpeed = 1.6,
	anims = {   -- clip, frame count (24 fps)
		draw = { "deagle_vm_draw", 37 }, holster = { "deagle_vm_holster", 37 },
		idle = { "deagle_vm_idle1", 2 }, idleEmpty = { "deagle_vm_idle_empty", 2 },
		ads = { "deagle_vm_ads", 2 }, adsEmpty = { "deagle_vm_ads_empty", 2 },
		shoot = { { "deagle_vm_shoot1", 35 }, { "deagle_vm_shoot2", 35 } }, shootEmpty = { "deagle_vm_shoot_empty", 13 },
		shootAdd = { { "deagle_vm_shoot1_add", 35 }, { "deagle_vm_shoot2_add", 35 } }, shootEmptyAdd = { "deagle_vm_shoot_empty_add", 13 },
		reload = { "deagle_vm_reload", 65 }, reloadEmpty = { "deagle_vm_reload_empty", 68 }, inspect = { "deagle_vm_inspect", 153 },
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
local CUE_SEQ = { shoot = "shoot1", shootEmpty = "shoot_empty", reload = "reload", reloadEmpty = "reload_empty", draw = "draw", inspect = "inspect" }
local REST = { idle = true, idleEmpty = true, ads = true, adsEmpty = true }   -- looping rest poses
local HOLDS_AIM = { draw = true, holster = true, reload = true, reloadEmpty = true }   -- aim waits for these; shots and inspect it cuts

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

function Deagle:Flash()
	local p = self:Player()
	local c = self.cfg
	local i = c.flashIntensity
	local lt = {
		style = 0, deferred_light = 1, radius = c.flashRadius,
		diffuse_color = { x = i, y = i * 0.72, z = i * 0.38 },
		specular_color = { x = i, y = i * 0.72, z = i * 0.38 },
		hdrdyn = 0, lifetime = 0, realtime = 1, dot3 = 1, cast_shadow = 0,
		corona_scale = 0, corona_dist_size_factor = 1, corona_dist_intensity_factor = 1,
	}
	local ok = try("flash light", function()
		local slot = p:LoadLight(c.lightSlot, lt)
		p:SetSlotPos(slot or c.lightSlot, { x = 0.15, y = 0.9, z = 1.5 })
	end)
	if ok then
		Script.SetTimer(c.flashMs, function()
			try("flash off", function() p:FreeSlot(c.lightSlot) end)
		end)
	end
end

function Deagle:Recoil()
	local p = self:Player()
	local k = self.cfg.recoil
	try("view kick", function()
		local d = System.GetViewCameraDir()
		local pitch = math.asin(math.max(-1, math.min(1, d.z)))
		local yaw = math.atan2(-d.x, d.y)
		local up = math.rad(self.cfg.kickPitch) * k * (self.state.aiming and 0.7 or 1)
		local side = math.rad(self.cfg.kickYaw) * k * (math.random() * 2 - 1)
		p.actor:PlayerSetViewAngles({ x = pitch + up, y = 0, z = yaw + side })
	end)
	try("recoil", function()
		p.actor:SetViewShake({ x = math.rad(2.2) * k, y = math.rad(0.6) * k, z = math.rad(0.4) * k },
			{ x = 0.004 * k, y = 0.02 * k, z = 0.006 * k }, 0.16, 1 / 30, 0.2)
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
			local dmg = c.damage * (part == "head" and c.headshotMult or 1)
			hit.bodyPart = part
			try("DealDamage", function() e.soul:DealDamage(c.stamina, dmg, p.id, false, part) end)
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
		try("impulse", function() e:AddImpulse(hit.partid or -1, hit.pos, dir, c.impulse, 1) end)
	end
end

-- ---------------------------------------------------------------------------
-- shooting, casings
-- ---------------------------------------------------------------------------
function Deagle:MuzzlePos()
	return self:VmBone("tag_flash") or vadd(System.GetViewCameraPos(), vscale(System.GetViewCameraDir(), 0.5))
end

-- casing out of the viewmodel's ejection port: random cone, speed and spin so no two take the same path; port smoke
function Deagle:EjectCase()
	local port = self:VmBone("tag_brass")
	if not port then return end
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local case = System.SpawnEntity({
			class = "BasicEntity", name = "deagle_case_" .. tostring(math.random(1, 1e9)), position = { x = 0, y = 0, z = -1000 },
			flags = ENTITY_FLAG_NO_SAVE,
			properties = { object_Model = c.modelDir .. "deagle_case.cgf", Physics = { bPhysicalize = true, bRigidBody = true, bPushableByPlayers = false, Density = -1, Mass = 0.02 } },
		})
		pcall(case.SetFlags, case, ENTITY_FLAG_NO_SAVE, 0)   -- casings live for seconds; never into a save
		case:SetWorldPos(port)
		local jr, ju, jf = 0.75 + math.random() * 0.5, 0.5 + math.random() * 0.7, -0.45 + math.random() * 0.5
		local dir = vnorm(vadd(vscale(r, jr), vadd(vscale(u, ju), vscale(f, jf))))
		local spinAt = vadd(port, { x = (math.random() - 0.5) * 0.02, y = (math.random() - 0.5) * 0.02, z = (math.random() - 0.5) * 0.02 })
		case:AddImpulse(-1, spinAt, dir, c.ejectImpulse * (0.8 + math.random() * 0.45), 1)
		local id = case.id
		Script.SetTimer(c.caseLifeMs, function() pcall(System.RemoveEntity, id) end)
	end)
	self:SpawnFx(c.portFx, port, System.GetViewCameraUpDir(), 1.0)
end

-- one shot: ray, effects, ammo. Returns true when a round was fired.
function Deagle:Shoot()
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
	if now - s.lastShot < c.interval then return false end
	if s.ammo <= 0 then
		self:PlayTrigger(c.dryTrigger)
		self:ShowAmmo()
		return false
	end
	s.lastShot = now
	s.ammo = s.ammo - 1

	local pos = System.GetViewCameraPos()
	local dir = coneDir(System.GetViewCameraDir(), s.aiming and c.spreadAim or c.spreadHip)

	self:PlayTrigger(c.shotTrigger)
	self:SpawnFx(c.muzzleFx, self:MuzzlePos(), dir, 0.6)
	self:Flash()
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
function Deagle:Fire()
	local s = self.state
	local aimed, wasLast = s.aiming, s.ammo == 1
	if not self:Shoot() then return end
	if aimed then
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

-- the reload lasts exactly as long as its clip (full or empty-mag version)
function Deagle:Reload()
	local s, c = self.state, self.cfg
	if s.reloading or s.ammo >= c.magSize then return end
	if c.useAmmoItems and self:CountOf(c.ammoId) <= 0 then
		self:Notify("No .50 AE cartridges")
		return
	end
	local key = s.ammo == 0 and "reloadEmpty" or "reload"
	local len = c.vm.anims[key][2] / c.vm.fps
	s.reloading = true
	s.reloadEnd = System.GetCurrTime() + len
	self:Notify("Desert Eagle  reloading...")
	self:PlayTrigger(c.reloadTrigger)
	Script.SetTimer(math.floor(len * 1000) + 20, function() self:FinishReload() end)
	self:VmPlay(key, false, nil, s.aiming and c.vm.adsBlend or nil)
	self:UpdateAim()
end

-- Called by the timer and polled from input, so a reload never gets stuck if timers are cleared.
function Deagle:FinishReload()
	local s, c = self.state, self.cfg
	if not s.reloading then return end
	if System.GetCurrTime() < (s.reloadEnd or 0) then return end
	s.reloading = false
	local take = c.magSize - s.ammo
	if c.useAmmoItems then
		take = math.min(take, self:CountOf(c.ammoId))
		if take > 0 then
			try("use ammo", function() self:Player().inventory:DeleteItemOfClass(c.ammoId, take) end)
		end
	end
	s.ammo = s.ammo + take
	self:ShowAmmo()
end

-- Aim input. Aiming (zoom + ADS pose) follows the button, except while a reload / draw / holster runs: those finish
-- first and hand over to ADS in their last moments. Hip shots and inspect are cut by it.
function Deagle:SetAim(on)
	self.state.aimHeld = on
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
		self:VmPlay(self:VmRestKey(), true, 1.0, blend or self.cfg.vm.adsBlend)
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

-- inspect only from the hip
function Deagle:Inspect()
	local s = self.state
	if s.aiming or s.aimHeld or not s.drawn or s.reloading then return end
	self:VmPlay("inspect", false)
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
		if ok and type(name) == "string" and name:find("^deagle_") and e.id ~= self.vmId then
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
	self.vmId = vm.id
	vm.OnUpdate = function() Deagle:VmPlace() end    -- per-frame follow (entity update); VmTick timer as well
	pcall(vm.Activate, vm, 1)
	return vm
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
	if not vm then return end
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
	local now = System.GetCurrTime()
	if now ~= self.hudT then self.hudT = now; self:DrawHud() end
end

function Deagle:VmTick(token)
	if token ~= self.vmToken or not self.vmShown then return end
	self:VmPlace()
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
	local label = s.reloading and "RELOADING" or "DESERT EAGLE"
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
function Deagle:VmReturn()
	local s = self.state
	if s.aimHeld ~= s.aiming then self:UpdateAim() else self:VmPlay(self:VmRestKey(), true) end
end

function Deagle:VmBusy()   -- a one-shot clip (shot, reload, inspect, draw, holster) still playing
	local a = self.vmAnim
	return a and not a.loop and System.GetCurrTime() < (a.ends or 0)
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
	speed = speed or 1.0
	if self.vmAnim and self.vmAnim.name == name and not REST[key] then
		pcall(vm.StartAnimation, vm, 0, c.anims.idle[1], c.layer, 0, 1.0, true)
	end
	local ok, started = pcall(vm.StartAnimation, vm, 0, name, c.layer, blend or c.blend, speed, loop and true or false)
	if not (ok and started) and self.vmLastFail ~= name and not (self.vmAnim and self.vmAnim.name == name) then
		self.vmLastFail = name
		log("$4viewmodel anim did not start: " .. name .. " " .. tostring(started))
	end
	local len = frames / c.fps / speed
	self.vmAnimToken = (self.vmAnimToken or 0) + 1
	local tok = self.vmAnimToken
	self.vmAnim = { key = key, name = name, loop = loop, ends = System.GetCurrTime() + len }
	if not loop then
		self:PlayCues(key)
		if HOLDS_AIM[key] and len > c.adsLead then
			Script.SetTimer(math.floor((len - c.adsLead) * 1000), function()
				if Deagle.vmAnimToken == tok and Deagle.vmShown and Deagle.state.aimHeld then Deagle:UpdateAim(c.adsLead) end
			end)
		end
		Script.SetTimer(math.max(1, math.floor(len * 1000) - 30), function()
			if Deagle.vmAnimToken == tok and Deagle.vmShown and not Deagle.vmHolstering then Deagle:VmReturn() end
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
	local a = empty and c.anims.shootEmptyAdd or c.anims.shootAdd[((self.vmShot or 0) % 2) + 1]
	self.vmShot = (self.vmShot or 0) + 1
	self.vmRecoilToken = (self.vmRecoilToken or 0) + 1
	local tok = self.vmRecoilToken
	pcall(vm.StartAnimation, vm, 0, a[1], c.recoilLayer, 0.0, 1.0, false)
	self:PlayCues(empty and "shootEmpty" or "shoot")
	Script.SetTimer(math.floor(a[2] / c.fps * 1000), function()
		if Deagle.vmRecoilToken ~= tok then return end
		-- same frame: rest loop in (adsEmpty after the last round keeps the slide locked), recoil offset out
		if Deagle.vmShown and not Deagle.vmHolstering and not Deagle:VmBusy() then Deagle:VmPlay(Deagle:VmRestKey(), true, 1.0, 0) end
		Deagle:VmClearRecoil()
	end)
end

-- sound cues of a clip, played by the plugin; cues of an interrupted clip are dropped (shots always play)
function Deagle:PlayCues(key)
	local seq = CUE_SEQ[key]
	local cues = seq and Deagle.vmSoundCues[seq]
	if not cues then return end
	local tok = self.vmAnimToken
	local v = self.cfg.vm.soundVol
	for _, c in ipairs(cues) do
		local t, file = c[1], c[2]
		local function fire()
			if Deagle.vmAnimToken ~= tok and key ~= "shoot" and key ~= "shootEmpty" then return end
			if file == "shot" then Deagle:Snd("decho/decho-" .. math.random(1, 8) .. ".wav", v.shot)
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
	self:VmPlay("draw", false)
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
		local len = self:VmPlay("holster", false, self.cfg.vm.holsterSpeed)
		Script.SetTimer(math.floor(len * 1000), function()
			Deagle.vmHolstering = false
			if not Deagle.state.drawn then Deagle:VmHide(); Deagle:NativeSyncVm() end
		end)
		return
	end
	self.vmHolstering = false
	self:VmHide()
	self:NativeSyncVm()
end

function Deagle:Toggle()
	local s = self.state
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
	-- the native "combat" map turns left click into a fist attack; keep it off while the pistol is out
	try("combat map", ActionMapManager.EnableActionMap, "combat", not s.drawn)
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
	if self.vmId then self:Native("vm 0x" .. (string.match(tostring(self.vmId), "(%x+)%s*$") or "0")) end
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
	if not s.drawn then return false end
	if action == "deagle_fire" then
		if activation == "press" then self:Fire() end
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
	s.baseFov, s.fovNow = nil, nil
	self.inputLoaded = false
	self:EnsureInit()
	self.vmId = nil
	self:NativeHello()
end

-- ---------------------------------------------------------------------------
-- console
-- ---------------------------------------------------------------------------
function Deagle:Set(line)
	local key, value = string.match(unquote(line), "^%s*(%S+)%s+(.-)%s*$")
	if not key then
		log("usage: deagle_set <key> <value>")
		return
	end
	local num = tonumber(value)
	if value == "nil" then
		self.cfg[key] = nil
	elseif num then
		self.cfg[key] = num
	else
		self.cfg[key] = value
	end
	log("cfg." .. key .. " = " .. tostring(self.cfg[key]))
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

log("loaded v2.0")
