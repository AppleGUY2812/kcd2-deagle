-- Desert Eagle for KCD2: scripted semi-auto pistol (v0: gameplay core, no custom model yet).
-- Everything is pcall-guarded so a missing engine binding logs an error instead of breaking the game.

Deagle = Deagle or {}

Deagle.cfg = {
	itemId = "d81561f0-5635-4197-84c2-7f2b1a92f013",   -- deagle_pistol (Libs/Tables/item/item__deagle.xml)
	ammoId = "214b29af-66a9-42be-882a-d63562010b7b",   -- deagle_ammo
	useAmmoItems = false,     -- cartridge items stack as one count in KCD2; unlimited reserve until that is solved
	magSize = 7,
	interval = 0.22,          -- seconds between shots (semi-auto cap)
	reloadTime = 2.1,
	range = 250,
	damage = 60,              -- health damage (game applies body_part coeff: head 1.5, limbs 0.7)
	headshotMult = 3.0,       -- extra on top: .50 AE to the head should drop anyone
	stamina = 40,
	impulse = 220,            -- physics push on props and bodies
	spreadHip = 1.6,          -- degrees
	spreadAim = 0.25,
	aimFovScale = 0.8,
	hitMode = "soul",         -- "soul" = soul:DealDamage with native hit reactions; "hit" = gamerules ServerHit (no effect on souls in KCD2)
	hitType = "bullet",       -- probe: bullet=10, normal=8
	shotTrigger = "c_f_firearm_shot",
	dryTrigger = nil,
	reloadTrigger = nil,
	muzzleFx = "firearms.hand_cannon.frontParticleEmit", -- has its own LightSource
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
	recoil = 1.0,
	kickPitch = 2.6,          -- degrees of muzzle climb per shot
	kickYaw = 0.8,
	lightSlot = 15,
	modelDir = "objects/manmade/weapons/deagle/",
	-- pistol relative to RightHandSlot, computed in Blender against the deagle_idle grip (tools/retarget.py HANDOFFSET)
	handPos = { x = 0.0206, y = -0.009, z = -0.0017 },
	handAng = { x = 110.0, y = -52.1, z = 60.8 },     -- degrees, XYZ
	slideTravel = 0.035,                    -- metres the slide moves back per shot
	slideMs = { 0, 45, 95 },                -- back, half way, home
	ejectPort = { x = 0.014, y = 0.06, z = 0.06 }, -- in frame space (muzzle +Y, up +Z)
	ejectImpulse = 0.06,
	caseLifeMs = 20000,
	muzzle = { x = 0, y = 0.22, z = 0.055 },
	holdAnim = "",                       -- off while the arm animations are rebuilt on the native aim system
	pitchStep = 10, pitchMax = 70,
	gunCam = { fwd = 0.36, right = 0.05, up = -0.11 },  -- where the hold animations put the hand slot, camera space
	animLayer = 9,
	holdFragment = "",        -- e.g. HoldItemRight / r_light; PlayAnim currently breaks crouch, off until our own anims exist
	holdTags = "",
}

Deagle.state = Deagle.state or {
	ammo = 7,
	drawn = false,
	aiming = false,
	reloading = false,
	lastShot = -100,
	logActions = false,
	baseFov = nil,
}

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

-- ---------------------------------------------------------------------------
-- vector helpers
-- ---------------------------------------------------------------------------
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

local function coneDir(dir, degrees)
	if degrees <= 0 then return dir end
	local up = { x = 0, y = 0, z = 1 }
	local right = vnorm(vcross(dir, up))
	local realUp = vnorm(vcross(right, dir))
	local r = math.tan(math.rad(degrees)) * math.sqrt(math.random())
	local a = math.random() * 2 * math.pi
	return vnorm(vadd(dir, vadd(vscale(right, r * math.cos(a)), vscale(realUp, r * math.sin(a)))))
end

function Deagle:Player()
	return g_localActor or player
end

-- ---------------------------------------------------------------------------
-- feedback: sound, particles, light, recoil
-- ---------------------------------------------------------------------------
function Deagle:PlayTrigger(name)
	if not name then return end
	local p = self:Player()
	try("audio " .. name, function()
		local id = AudioUtils.LookupTriggerID(name)
		if id then p:ExecuteAudioTrigger(id, p:GetDefaultAuxAudioProxyID()) end
	end)
end

function Deagle:SpawnFx(name, pos, normal, scale)
	if not name then return end
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
-- shooting
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

function Deagle:Fire()
	local s, c = self.state, self.cfg
	local p = self:Player()
	if not p or s.reloading then return end
	if not self:Owns() then
		s.drawn = false
		self:SetAim(false)
		self:HideInHand()
		return
	end
	local now = System.GetCurrTime()
	if now - s.lastShot < c.interval then return end
	if s.ammo <= 0 then
		self:PlayTrigger(c.dryTrigger)
		self:ShowAmmo()
		return
	end
	s.lastShot = now
	s.ammo = s.ammo - 1

	local pos = System.GetViewCameraPos()
	local dir = coneDir(System.GetViewCameraDir(), s.aiming and c.spreadAim or c.spreadHip)

	self:PlayTrigger(c.shotTrigger)
	self:SpawnFx(c.muzzleFx, self:MuzzlePos(), dir, 0.6)
	self:Flash()
	self:Recoil()
	self:SlideCycle()
	self:EjectCase()

	local hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, pos, vscale(dir, c.range), 1, ent_all, p.id, nil, hits)
	if ok and n and n > 0 and hits[1] then
		self:ApplyHit(hits[1], dir)
	elseif not ok then
		log("$4raycast failed: " .. tostring(n))
	end
	if s.ammo == 0 then self:ShowAmmo() end
end

-- ---------------------------------------------------------------------------
-- inventory: the pistol and its cartridges are real items
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
	pcall(Game.HideInfoText)
	try("info text", Game.SendInfoText, text, false)
end

function Deagle:Give(line)
	local p = self:Player()
	local rounds = tonumber(unquote(line)) or (self.cfg.useAmmoItems and 49 or 0)
	if not self:Owns() then try("give pistol", ItemUtils.CreateInvItem, p, self.cfg.itemId, 1) end
	if rounds > 0 then try("give ammo", ItemUtils.CreateInvItem, p, self.cfg.ammoId, rounds) end
	log("give: pistol " .. self:CountOf(self.cfg.itemId) .. ", cartridges " .. self:CountOf(self.cfg.ammoId))
end

function Deagle:Reload()
	local s, c = self.state, self.cfg
	if s.reloading or s.ammo >= c.magSize then return end
	if c.useAmmoItems and self:CountOf(c.ammoId) <= 0 then
		self:Notify("No .50 AE cartridges")
		return
	end
	s.reloading = true
	s.reloadEnd = System.GetCurrTime() + c.reloadTime
	self:Notify("Desert Eagle  reloading...")
	self:PlayTrigger(c.reloadTrigger)
	Script.SetTimer(math.floor(c.reloadTime * 1000) + 20, function() self:FinishReload() end)
end

-- Called by the timer and polled from input, so a reload never gets stuck if timers are cleared.
function Deagle:FinishReload()
	local s, c = self.state, self.cfg
	if not s.reloading then return end
	if System.GetCurrTime() < (s.reloadEnd or 0) then return end
	do
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
end

function Deagle:ShowAmmo()
	local s = self.state
	if not s.drawn then
		self:Notify("Desert Eagle  holstered")
		return
	end
	local spare = self.cfg.useAmmoItems and string.format("   (%d spare)", self:CountOf(self.cfg.ammoId)) or ""
	local tail = s.ammo == 0 and "   EMPTY - press R" or ""
	self:Notify(string.format("Desert Eagle  %d / %d%s%s", s.ammo, self.cfg.magSize, spare, tail))
end

function Deagle:SetAim(on)
	local s = self.state
	if s.aiming == on then return end
	s.aiming = on
	try("aim fov", function()
		if on then
			s.baseFov = System.GetViewCameraFov()
			System.SetViewCameraFov(s.baseFov * self.cfg.aimFovScale)
		elseif s.baseFov then
			System.SetViewCameraFov(s.baseFov)
		end
	end)
end

-- ---------------------------------------------------------------------------
-- the pistol in Henry's right hand: a spawned entity attached with Human.AttachEntityToHand
-- ---------------------------------------------------------------------------
local function spawnModel(name, file, physical, mass)
	return System.SpawnEntity({
		class = "BasicEntity", name = name, position = { x = 0, y = 0, z = -1000 },
		properties = { object_Model = file, Physics = { bPhysicalize = physical, bRigidBody = physical, bPushableByPlayers = false, Density = -1, Mass = mass or -1 } },
	})
end

-- Cry Ang3 (XYZ, radians) -> rotation applied to a vector
local function rotate(ang, v)
	local cx, sx, cy, sy, cz, sz = math.cos(ang.x), math.sin(ang.x), math.cos(ang.y), math.sin(ang.y), math.cos(ang.z), math.sin(ang.z)
	return {
		x = cy * cz * v.x + (sx * sy * cz - cx * sz) * v.y + (cx * sy * cz + sx * sz) * v.z,
		y = cy * sz * v.x + (sx * sy * sz + cx * cz) * v.y + (cx * sy * sz - sx * cz) * v.z,
		z = -sy * v.x + sx * cy * v.y + cx * cy * v.z,
	}
end

-- Camera basis (the hold animations lock the pistol to the camera, so effects are placed in camera space)
local function camBasis()
	local pos, f = System.GetViewCameraPos(), System.GetViewCameraDir()
	local r = vnorm(vcross(f, { x = 0, y = 0, z = 1 }))
	return pos, f, r, vcross(r, f)
end

local function camPoint(c)
	local pos, f, r, u = camBasis()
	return vadd(pos, vadd(vscale(f, c.fwd), vadd(vscale(r, c.right), vscale(u, c.up))))
end

local PARTS = { "frame", "slide", "mag" }

function Deagle:AttachName(part) return "deagle_" .. part .. "_att" end

function Deagle:ShowInHand()
	local p, c = self:Player(), self.cfg
	self:HideInHand()
	local a = c.handAng
	self.handRad = { x = math.rad(a.x), y = math.rad(a.y), z = math.rad(a.z) }
	local ok, err = pcall(function()
		for _, part in ipairs(PARTS) do
			local name = self:AttachName(part)
			p:CreateBoneAttachment(0, "RightHandSlot", name)
			p:SetAttachmentCGF(0, name, c.modelDir .. "deagle_" .. part .. ".cgf")
			p:SetAttachmentAngles(0, name, self.handRad)
			p:SetAttachmentPos(0, name, c.handPos)
		end
		self.attached = true
	end)
	if not ok then log("$4show in hand failed: " .. tostring(err)) end
	self.holdBucket = nil
	self:PlayHold()
	self:StartWatchdog()
end

function Deagle:ApplyHandOffset()
	if self.attached then self:ShowInHand() end
end

-- slide offset along the pistol's own -Y, expressed in the hand slot's space
function Deagle:SetSlide(back)
	local p, c = self:Player(), self.cfg
	if not self.attached then return end
	local off = rotate(self.handRad, { x = 0, y = -back, z = 0 })
	pcall(p.SetAttachmentPos, p, 0, self:AttachName("slide"), vadd(c.handPos, off))
end

function Deagle:SlideCycle()
	local c = self.cfg
	local steps = { c.slideTravel, c.slideTravel * 0.45, 0 }
	for i, ms in ipairs(c.slideMs) do
		local back = steps[i]
		if ms <= 0 then self:SetSlide(back) else Script.SetTimer(ms, function() Deagle:SetSlide(back) end) end
	end
end

function Deagle:EjectCase()
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local g = c.gunCam
		local pos = camPoint({ fwd = g.fwd + c.ejectPort.y, right = g.right + c.ejectPort.x + 0.02, up = g.up + c.ejectPort.z })
		local case = spawnModel("deagle_case_" .. tostring(math.random(1, 1e9)), c.modelDir .. "deagle_case.cgf", true, 0.02)
		case:SetWorldPos(pos)
		local dir = vnorm(vadd(vscale(r, 1.0), vadd(vscale(u, 0.8), vscale(f, -0.3 + math.random() * 0.2))))
		case:AddImpulse(-1, pos, dir, c.ejectImpulse, 1)
		local id = case.id
		Script.SetTimer(c.caseLifeMs, function() pcall(System.RemoveEntity, id) end)
	end)
end

function Deagle:MuzzlePos()
	local g, m = self.cfg.gunCam, self.cfg.muzzle
	return camPoint({ fwd = g.fwd + m.y, right = g.right + m.x, up = g.up + m.z })
end

function Deagle:HoldName()
	local c = self.cfg
	local d = System.GetViewCameraDir()
	local pitch = math.deg(math.asin(math.max(-1, math.min(1, d.z))))
	local b = math.floor(pitch / c.pitchStep + 0.5) * c.pitchStep
	b = math.max(-c.pitchMax, math.min(c.pitchMax, b))
	return c.holdAnim .. (b < 0 and ("_m" .. -b) or ("_p" .. b)), b
end

function Deagle:PlayHold(blend)
	local c = self.cfg
	if c.holdAnim and c.holdAnim ~= "" then
		local p = self:Player()
		local name, bucket = self:HoldName()
		local ok, started = pcall(p.StartAnimation, p, 0, name, c.animLayer, blend or 0.25, 1.0, true)
		if ok and started then
			self.holdBucket = bucket
		elseif self.lastHoldFail ~= name then
			self.lastHoldFail = name  -- log each missing clip once; the watchdog retries every tick
			log("$4hold anim did not start: " .. name)
		end
	end
	if c.holdFragment and c.holdFragment ~= "" then
		try("hold pose", function() self:Player().human:PlayAnim(c.holdFragment, c.holdTags or "") end)
	end
end

-- Stance changes (crouch, jump, sprint...) restart the character's animation and drop our layer,
-- so while the pistol is out a watchdog re-applies the hold as soon as it stops running.
function Deagle:HoldWatchdog(token)
	if token ~= self.watchToken or not self.state.drawn then return end
	local p, c = self:Player(), self.cfg
	if p and c.holdAnim and c.holdAnim ~= "" then
		local ok, running = pcall(p.IsAnimationRunning, p, 0, c.animLayer)
		local _, bucket = self:HoldName()
		if (ok and not running) or bucket ~= self.holdBucket then
			self:PlayHold(0.12)
			self.reapplied = (self.reapplied or 0) + 1
		end
	end
	Script.SetTimer(80, function() Deagle:HoldWatchdog(token) end)
end

function Deagle:StartWatchdog()
	self.watchToken = (self.watchToken or 0) + 1
	local token = self.watchToken
	Script.SetTimer(80, function() Deagle:HoldWatchdog(token) end)
end

function Deagle:HideInHand()
	local p = self:Player()
	if not self.attached and not self.handEnt then return end
	if self.cfg.holdFragment ~= "" then try("stop hold pose", function() p.human:StopAnim() end) end
	pcall(p.StopAnimation, p, 0, self.cfg.animLayer)
	for _, part in ipairs(PARTS) do pcall(p.DestroyAttachment, p, 0, self:AttachName(part)) end
	self.attached = false
	for _, id in ipairs({ self.slideEnt, self.magEnt, self.handEnt }) do
		if id then pcall(System.RemoveEntity, id) end
	end
	self.handEnt, self.slideEnt, self.magEnt = nil, nil, nil
end

function Deagle:Pose(line)
	local frag, tags = string.match(unquote(line), "^%s*(%S+)%s*(%S*)")
	self.cfg.holdFragment, self.cfg.holdTags = frag, tags
	log("pose " .. tostring(frag) .. " [" .. tostring(tags) .. "]")
	self:PlayHold()
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
-- input: our action map + a wrapper around gamerules' action hook
-- ---------------------------------------------------------------------------
function Deagle:OnAction(action, activation)
	local s = self.state
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
end

-- Script timers are cleared when a save loads, so init is driven by gameplay events,
-- plus the first player action as a fallback.
function Deagle:OnGameplayStarted()
	log("gameplay started")
	local s = self.state
	s.reloading, s.drawn, s.aiming, s.lastShot, s.ammo = false, false, false, -100, self.cfg.magSize
	self.handEnt, self.slideEnt, self.magEnt = nil, nil, nil
	self.attached = false
	self.inputLoaded = false
	self:EnsureInit()
end

-- ---------------------------------------------------------------------------
-- probe: dumps the engine API surface we depend on into kcd.log
-- ---------------------------------------------------------------------------
local function dumpTable(label, t)
	if type(t) ~= "table" then
		log("probe " .. label .. ": " .. type(t))
		return
	end
	local names = {}
	local seen = {}
	local function collect(tbl)
		for k, v in pairs(tbl) do
			if not seen[k] then
				seen[k] = true
				names[#names + 1] = tostring(k) .. (type(v) == "function" and "()" or "")
			end
		end
	end
	collect(t)
	local mt = getmetatable(t)
	if mt and type(mt.__index) == "table" then collect(mt.__index) end
	table.sort(names)
	log("probe " .. label .. " (" .. #names .. "): " .. table.concat(names, " "))
end

function Deagle:Probe()
	local p = self:Player()
	dumpTable("ActionMapManager", ActionMapManager)
	dumpTable("Physics", Physics)
	dumpTable("Particle", Particle)
	dumpTable("PlayerStateHandler", PlayerStateHandler)
	dumpTable("Sound", Sound)
	dumpTable("AudioUtils", AudioUtils)
	dumpTable("Game", Game)
	if p then
		dumpTable("player", p)
		dumpTable("player.actor", p.actor)
		dumpTable("player.soul", p.soul)
		dumpTable("player.human", p.human)
		dumpTable("player.player", p.player)
	end
	if g_gameRules and g_gameRules.game then
		for _, name in ipairs({ "normal", "bullet", "arrow", "missile", "melee", "pierce", "stab", "explosion", "fire", "fall", "gun" }) do
			local ok, id = pcall(g_gameRules.game.GetHitTypeId, g_gameRules.game, name)
			log("probe hitType " .. name .. " = " .. tostring(ok and id or ("err " .. tostring(id))))
		end
	end
	log("probe camera fov " .. tostring(System.GetViewCameraFov()))
end

-- ---------------------------------------------------------------------------
-- console commands
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

function Deagle:Cfg()
	local keys = {}
	for k in pairs(self.cfg) do keys[#keys + 1] = k end
	table.sort(keys)
	for _, k in ipairs(keys) do log("cfg." .. k .. " = " .. tostring(self.cfg[k])) end
end

System.AddCCommand("deagle_probe", "Deagle:Probe()", "Dump engine APIs used by the Deagle mod to the log")
System.AddCCommand("deagle_toggle", "Deagle:EnsureInit() Deagle:Toggle()", "Draw / holster the Desert Eagle")
System.AddCCommand("deagle_fire", "Deagle:EnsureInit() Deagle:Fire()", "Fire one shot")
System.AddCCommand("deagle_reload", "Deagle:Reload()", "Reload")
System.AddCCommand("deagle_input", "Deagle:LoadInput()", "Reload the Deagle key bindings")
System.AddCCommand("deagle_log_actions", "Deagle.state.logActions = not Deagle.state.logActions", "Toggle logging of every input action")
System.AddCCommand("deagle_set", "Deagle:Set([[%line]])", "deagle_set <key> <value>: change a setting live")
System.AddCCommand("deagle_status", "Deagle:EnsureInit() Deagle:Status()", "Print hook / input / ammo state")
System.AddCCommand("deagle_give", "Deagle:EnsureInit() Deagle:Give()", "Add the Desert Eagle (if missing) and 49 cartridges")
System.AddCCommand("deagle_give_ammo", "Deagle:EnsureInit() Deagle:Give([[%1]])", "deagle_give_ammo <n>: add n cartridges")
System.AddCCommand("deagle_pose", "Deagle:Pose([[%line]])", "deagle_pose <fragment> [tags]: try a hold animation, e.g. deagle_pose Pointing r_noweapon")
-- Dev hot reload: re-runs this script from a loose file so changes apply without restarting the game.
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

System.AddCCommand("deagle_dev_reload", "Deagle:DevReload()", "Re-run the Deagle script from the dev path")
System.AddCCommand("deagle_lua", "Deagle:DevLua([==[%line]==])", "deagle_lua <code>: run Lua (dev)")
function Deagle:Hand(line)
	local n = {}
	for v in string.gmatch(unquote(line), "%S+") do n[#n + 1] = tonumber(v) or 0 end
	local c = self.cfg
	c.handPos = { x = n[1] or 0, y = n[2] or 0, z = n[3] or 0 }
	c.handAng = { x = n[4] or 0, y = n[5] or 0, z = n[6] or 0 }
	log(string.format("hand pos %.3f %.3f %.3f ang %.0f %.0f %.0f", c.handPos.x, c.handPos.y, c.handPos.z, c.handAng.x, c.handAng.y, c.handAng.z))
	self:ApplyHandOffset()
end
System.AddCCommand("deagle_hand", "Deagle:Hand([[%line]])", "deagle_hand x y z rx ry rz: offset (m) and rotation (deg) of the pistol in the hand")
System.AddCCommand("deagle_cfg", "Deagle:Cfg()", "Print all settings")
System.AddCCommand("deagle_fx", "Deagle:FxTest([[%1]])", "deagle_fx <effect>: spawn a particle effect 3 m ahead")

Deagle:HookActions()
if not Deagle.listening then
	Deagle.listening = true
	try("event listener", function()
		UIAction.RegisterEventSystemListener(Deagle, "System", "OnGameplayStarted", "OnGameplayStarted")
	end)
end

log("loaded v1.3")

-- ---------------------------------------------------------------------------
-- first-person viewmodel: arms + Deagle + animations converted from the MWII Deagle (GameBanana 532396, private use).
-- A separate character placed on the camera every frame (its camera bone tag_view is the model origin, +Y forward),
-- so the arms never depend on Henry's body pose, pitch or strafing.
-- ---------------------------------------------------------------------------
Deagle.cfg.vm = {
	enabled = true,
	cdf = "objects/manmade/weapons/deagle/vm/deagle_vm.cdf",
	chr = "objects/manmade/weapons/deagle/vm/deagle_vm.chr",
	skin = "objects/manmade/weapons/deagle/vm/deagle_vm_skin.skin",
	offset = { fwd = 0.0, right = 0.0, up = 0.0 },   -- camera-space nudge (m)
	fps = 24, layer = 0, blend = 0.08,
	anims = {   -- name, frame count (24 fps)
		draw = { "deagle_vm_draw", 37 }, idle = { "deagle_vm_idle1", 2 },
		shoot = { { "deagle_vm_shoot1", 35 }, { "deagle_vm_shoot2", 35 } }, shootEmpty = { "deagle_vm_shoot_empty", 13 },
		reload = { "deagle_vm_reload", 65 }, reloadEmpty = { "deagle_vm_reload_empty", 68 }, inspect = { "deagle_vm_inspect", 153 },
	},
}

function Deagle:VmEntity()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then return vm end
	local c = self.cfg.vm
	vm = System.SpawnEntity({ class = "BasicEntity", name = "deagle_viewmodel", position = System.GetViewCameraPos(),
		properties = { object_Model = "" } })
	if not vm then log("$4viewmodel spawn failed") return nil end
	local ok, slot = pcall(vm.LoadCharacter, vm, 0, c.cdf)
	log("viewmodel character " .. c.cdf .. " -> " .. tostring(ok) .. " " .. tostring(slot))
	if not ok or slot == nil or slot == false or slot == -1 then
		local ok2, slot2 = pcall(vm.LoadCharacter, vm, 0, c.chr)
		local ok3 = pcall(vm.CreateSkinAttachment, vm, 0, "vm_mesh", c.skin)
		log("viewmodel fallback chr " .. tostring(ok2) .. " " .. tostring(slot2) .. " skin " .. tostring(ok3))
	end
	pcall(vm.SetViewDistRatio, vm, 255)
	self.vmId = vm.id
	vm.OnUpdate = function() Deagle:VmPlace() end    -- per-frame follow (entity update); timer fallback below
	pcall(vm.Activate, vm, 1)
	return vm
end

function Deagle:VmPlace()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm or not self.vmShown then return end
	local o = self.cfg.vm.offset
	local pos, f = System.GetViewCameraPos(), System.GetViewCameraDir()
	local r = vnorm(vcross(f, { x = 0, y = 0, z = 1 }))
	local u = vcross(r, f)
	vm:SetWorldPos(vadd(pos, vadd(vscale(f, o.fwd), vadd(vscale(r, o.right), vscale(u, o.up)))))
	pcall(vm.SetDirectionVector, vm, f, u)
end

function Deagle:VmTick(token)
	if token ~= self.vmToken or not self.vmShown then return end
	self:VmPlace()
	local a = self.vmAnim
	if a and not a.loop and System.GetCurrTime() >= a.ends then self:VmPlay("idle", true) end
	Script.SetTimer(0, function() Deagle:VmTick(token) end)
end

function Deagle:VmPlay(key, loop)
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return 0 end
	local a = c.anims[key]
	if key == "shoot" then
		self.vmShot = (self.vmShot or 0) + 1
		a = a[(self.vmShot - 1) % #a + 1]
	end
	local name, frames = a[1], a[2]
	local ok, started = pcall(vm.StartAnimation, vm, 0, name, c.layer, c.blend, 1.0, loop and true or false)
	if not (ok and started) and self.vmLastFail ~= name then
		self.vmLastFail = name
		log("$4viewmodel anim did not start: " .. name .. " " .. tostring(started))
	end
	local len = frames / c.fps
	self.vmAnim = { key = key, loop = loop, ends = System.GetCurrTime() + len }
	return len
end

function Deagle:VmShow()
	local vm = self:VmEntity()
	if not vm then return end
	self.vmShown = true
	pcall(vm.Hide, vm, 0)
	self:VmPlace()
	self:VmPlay("draw", false)
	self.vmToken = (self.vmToken or 0) + 1
	local token = self.vmToken
	Script.SetTimer(0, function() Deagle:VmTick(token) end)
end

function Deagle:VmHide()
	self.vmShown = false
	self.vmToken = (self.vmToken or 0) + 1
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.Hide, vm, 1) end
end

function Deagle:VmBone(name)
	local vm = self.vmShown and self.vmId and System.GetEntity(self.vmId)
	if not vm then return nil end
	local ok, p = pcall(vm.GetBonePos, vm, name)
	return ok and p or nil
end

function Deagle:Inspect()
	local s = self.state
	if not s.drawn or s.reloading or not self.cfg.vm.enabled then return end
	self:VmPlay("inspect", false)
end

-- hooks into the existing draw / fire / reload flow
local baseShowInHand, baseHideInHand = Deagle.ShowInHand, Deagle.HideInHand
function Deagle:ShowInHand()
	if self.cfg.vm.enabled then self:VmShow() return end
	baseShowInHand(self)
end
function Deagle:HideInHand()
	self:VmHide()
	baseHideInHand(self)
end
local baseMuzzlePos = Deagle.MuzzlePos
function Deagle:MuzzlePos()
	return self:VmBone("tag_flash") or baseMuzzlePos(self)
end
local baseSlideCycle = Deagle.SlideCycle
function Deagle:SlideCycle()
	if self.cfg.vm.enabled then return end      -- the viewmodel animation moves its own slide
	baseSlideCycle(self)
end
local baseFire = Deagle.Fire
function Deagle:Fire()
	local before, last = self.state.ammo, self.state.lastShot
	baseFire(self)
	if self.cfg.vm.enabled and self.state.lastShot ~= last then
		self:VmPlay(before == 1 and "shootEmpty" or "shoot", false)
	end
end
local baseReload = Deagle.Reload
function Deagle:Reload()
	local s, c = self.state, self.cfg
	if c.vm.enabled and not s.reloading and s.ammo < c.magSize then
		local a = c.vm.anims[s.ammo == 0 and "reloadEmpty" or "reload"]
		c.reloadTime = a[2] / c.vm.fps
		baseReload(self)
		if s.reloading then self:VmPlay(s.ammo == 0 and "reloadEmpty" or "reload", false) end
		return
	end
	baseReload(self)
end
local baseOnAction = Deagle.OnAction
function Deagle:OnAction(action, activation)
	if action == "deagle_inspect" and self.state.drawn then
		if activation == "press" then self:Inspect() end
		return true
	end
	return baseOnAction(self, action, activation)
end

System.AddCCommand("deagle_inspect", "Deagle:Inspect()", "Play the inspect animation")
System.AddCCommand("deagle_vm", "Deagle:DevLua([==[%line]==])", "deagle_vm <lua>: alias of deagle_lua for viewmodel tweaks")
local baseEjectCase = Deagle.EjectCase
function Deagle:EjectCase()
	local port = self:VmBone("tag_brass")
	if not port then return baseEjectCase(self) end
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local case = spawnModel("deagle_case_" .. tostring(math.random(1, 1e9)), c.modelDir .. "deagle_case.cgf", true, 0.02)
		case:SetWorldPos(port)
		local dir = vnorm(vadd(vscale(r, 1.0), vadd(vscale(u, 0.8), vscale(f, -0.3 + math.random() * 0.2))))
		case:AddImpulse(-1, port, dir, c.ejectImpulse, 1)
		local id = case.id
		Script.SetTimer(c.caseLifeMs, function() pcall(System.RemoveEntity, id) end)
	end)
end

-- ---- head-locked follow (fixes the 1-frame lag of per-frame camera placement): the viewmodel entity rides a
-- bone attachment on Henry's Head, which updates in the same frame as the first-person camera.
-- VmAttachHead() attaches with zero offset; VmCalibrate() (a frame later) solves the offset that puts the
-- viewmodel's origin on the camera, looking along it.
local function anglesToMat(a)   -- CryEngine Matrix33::CreateRotationXYZ = Rz * Ry * Rx (rows)
	local cx, sx, cy, sy, cz, sz = math.cos(a.x), math.sin(a.x), math.cos(a.y), math.sin(a.y), math.cos(a.z), math.sin(a.z)
	return { { cy * cz, sx * sy * cz - cx * sz, cx * sy * cz + sx * sz },
	         { cy * sz, sx * sy * sz + cx * cz, cx * sy * sz - sx * cz },
	         { -sy, sx * cy, cx * cy } }
end
local function matToAngles(m)
	return { x = math.atan2(m[3][2], m[3][3]), y = math.asin(math.max(-1, math.min(1, -m[3][1]))), z = math.atan2(m[2][1], m[1][1]) }
end
local function mtm(a, b)        -- a^T * b
	local r = {}
	for i = 1, 3 do r[i] = {} for j = 1, 3 do r[i][j] = a[1][i] * b[1][j] + a[2][i] * b[2][j] + a[3][i] * b[3][j] end end
	return r
end
local function mtv(a, v)        -- a^T * v
	return { x = a[1][1] * v.x + a[2][1] * v.y + a[3][1] * v.z, y = a[1][2] * v.x + a[2][2] * v.y + a[3][2] * v.z,
	         z = a[1][3] * v.x + a[2][3] * v.y + a[3][3] * v.z }
end
local function camMat()
	local f = System.GetViewCameraDir()
	local r = vnorm(vcross(f, { x = 0, y = 0, z = 1 }))
	local u = vcross(r, f)
	return { { r.x, f.x, u.x }, { r.y, f.y, u.y }, { r.z, f.z, u.z } }  -- columns: right, forward, up
end

Deagle.cfg.vm.follow = Deagle.cfg.vm.follow or "camera"   -- "camera" (per-frame) or "head" (bone attachment)
Deagle.cfg.vm.attBone = "Head"

function Deagle:VmAttachHead()
	local p, vm = self:Player(), self:VmEntity()
	if not (p and vm) then return end
	pcall(p.DestroyAttachment, p, 0, "deagle_vm_att")
	p:CreateBoneAttachment(0, self.cfg.vm.attBone, "deagle_vm_att")
	p:SetAttachmentObject(0, "deagle_vm_att", vm.id, -1, 0)
	local c = self.cfg.vm
	p:SetAttachmentPos(0, "deagle_vm_att", c.attPos or { x = 0, y = 0, z = 0 })
	p:SetAttachmentAngles(0, "deagle_vm_att", c.attAng or { x = 0, y = 0, z = 0 })
	self.cfg.vm.follow = "head"
	log("viewmodel attached to " .. c.attBone)
end

function Deagle:VmCalibrate()
	local vm = System.GetEntity(self.vmId)
	local P0, R0 = vm:GetWorldPos(), anglesToMat(vm:GetWorldAngles())
	-- current attachment offset (T_old) is part of the measured pose: world = A * T_old, want A * T_new = camera
	local c = self.cfg.vm
	local To = anglesToMat(c.attAng or { x = 0, y = 0, z = 0 })
	local po = c.attPos or { x = 0, y = 0, z = 0 }
	-- attachment frame A: rotation RA = R0 * To^T, origin PA = P0 - RA * po
	local RA = {}
	for i = 1, 3 do RA[i] = {} for j = 1, 3 do RA[i][j] = R0[i][1] * To[j][1] + R0[i][2] * To[j][2] + R0[i][3] * To[j][3] end end
	local PA = { x = P0.x - (RA[1][1] * po.x + RA[1][2] * po.y + RA[1][3] * po.z),
	             y = P0.y - (RA[2][1] * po.x + RA[2][2] * po.y + RA[2][3] * po.z),
	             z = P0.z - (RA[3][1] * po.x + RA[3][2] * po.y + RA[3][3] * po.z) }
	local Pc, Rc = System.GetViewCameraPos(), camMat()
	c.attPos = mtv(RA, vadd(Pc, vscale(PA, -1)))
	c.attAng = matToAngles(mtm(RA, Rc))
	local p = self:Player()
	p:SetAttachmentPos(0, "deagle_vm_att", c.attPos)
	p:SetAttachmentAngles(0, "deagle_vm_att", c.attAng)
	log(string.format("viewmodel calibrated: pos %.4f %.4f %.4f ang %.2f %.2f %.2f (deg)", c.attPos.x, c.attPos.y, c.attPos.z,
		math.deg(c.attAng.x), math.deg(c.attAng.y), math.deg(c.attAng.z)))
end

-- error of the viewmodel origin vs the camera (mm, deg): logged to judge drift while moving
function Deagle:VmError()
	local vm = System.GetEntity(self.vmId)
	local d = vadd(vm:GetWorldPos(), vscale(System.GetViewCameraPos(), -1))
	local R = anglesToMat(vm:GetWorldAngles())
	local f = System.GetViewCameraDir()
	local dot = R[1][2] * f.x + R[2][2] * f.y + R[3][2] * f.z
	return vlen(d) * 1000, math.deg(math.acos(math.max(-1, math.min(1, dot))))
end

local baseVmPlace = Deagle.VmPlace
function Deagle:VmPlace()
	if self.cfg.vm.follow == "head" then return end
	baseVmPlace(self)
end

-- ---- v2 viewmodel playback: every one-shot clip schedules its own return to idle (the 0 ms tick timer is not a
-- reliable per-frame loop), and replaying the clip that is already queued goes through idle first (CryEngine ignores
-- a start of the clip it is already playing, which blocked inspect -> inspect).
function Deagle:VmPlay(key, loop)
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return 0 end
	local a = c.anims[key]
	if key == "shoot" then
		self.vmShot = (self.vmShot or 0) + 1
		a = a[(self.vmShot - 1) % #a + 1]
	end
	local name, frames = a[1], a[2]
	if self.vmAnim and self.vmAnim.name == name and key ~= "idle" then
		pcall(vm.StartAnimation, vm, 0, c.anims.idle[1], c.layer, 0, 1.0, true)
	end
	local ok, started = pcall(vm.StartAnimation, vm, 0, name, c.layer, c.blend, 1.0, loop and true or false)
	if not (ok and started) and self.vmLastFail ~= name then
		self.vmLastFail = name
		log("$4viewmodel anim did not start: " .. name .. " " .. tostring(started))
	end
	local len = frames / c.fps
	self.vmAnimToken = (self.vmAnimToken or 0) + 1
	local tok = self.vmAnimToken
	self.vmAnim = { key = key, name = name, loop = loop, ends = System.GetCurrTime() + len }
	if not loop then
		Script.SetTimer(math.max(1, math.floor(len * 1000) - 30), function()
			if Deagle.vmAnimToken == tok and Deagle.vmShown then Deagle:VmPlay("idle", true) end
		end)
	end
	return len
end

-- casings: random cone, speed and spin so no two take the same path
function Deagle:EjectCase()
	local port = self:VmBone("tag_brass")
	if not port then return baseEjectCase(self) end
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local case = spawnModel("deagle_case_" .. tostring(math.random(1, 1e9)), c.modelDir .. "deagle_case.cgf", true, 0.02)
		case:SetWorldPos(port)
		local jr, ju, jf = 0.75 + math.random() * 0.5, 0.5 + math.random() * 0.7, -0.45 + math.random() * 0.5
		local dir = vnorm(vadd(vscale(r, jr), vadd(vscale(u, ju), vscale(f, jf))))
		local spinAt = vadd(port, { x = (math.random() - 0.5) * 0.02, y = (math.random() - 0.5) * 0.02, z = (math.random() - 0.5) * 0.02 })
		case:AddImpulse(-1, spinAt, dir, c.ejectImpulse * (0.8 + math.random() * 0.45), 1)
		local id = case.id
		Script.SetTimer(c.caseLifeMs, function() pcall(System.RemoveEntity, id) end)
	end)
end

-- Henry's own body is hidden in first person while the viewmodel is out (his shadow stays)
Deagle.cfg.vm.hideBody = true
function Deagle:SetBodyHidden(hide)
	if not self.cfg.vm.hideBody and hide then return end
	local p = self:Player()
	if not p or self.bodyHidden == hide then return end
	pcall(p.HideAllAttachments, p, 0, hide, false)
	pcall(p.HideAttachmentMaster, p, 0, hide)
	self.bodyHidden = hide
	log("body " .. (hide and "hidden" or "shown"))
end

-- viewmodel scale about the camera: identical image, but everything sits closer, so it clips into walls far less
Deagle.cfg.vm.scale = Deagle.cfg.vm.scale or 1.0
function Deagle:VmApplyScale()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then local s = self.cfg.vm.scale pcall(vm.SetScale, vm, { x = s, y = s, z = s }) end
end

local baseVmShow, baseVmHide = Deagle.VmShow, Deagle.VmHide
function Deagle:VmShow()
	baseVmShow(self)
	self:VmApplyScale()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if vm then pcall(vm.SetFlags, vm, ENTITY_FLAG_CASTSHADOW, 2) end   -- no floating-arm shadow; Henry's shadow stays
	self:SetBodyHidden(true)
end
function Deagle:VmHide()
	baseVmHide(self)
	self:SetBodyHidden(false)
end

-- ---- first-person guard: in dialogue / cutscene cameras the viewmodel hides and Henry's body comes back
Deagle.cfg.vm.fpMaxDist = 0.35   -- camera within this of the Head bone = first person
local placeWithFollow = Deagle.VmPlace
function Deagle:VmPlace()
	local p = self:Player()
	if not p or not self.vmShown then return end
	local ok, hp = pcall(p.GetBonePos, p, "Head")
	local cp = System.GetViewCameraPos()
	local fp = ok and hp and vlen(vadd(cp, vscale(hp, -1))) < self.cfg.vm.fpMaxDist
	if fp ~= self.vmInFp then
		self.vmInFp = fp
		local vm = self.vmId and System.GetEntity(self.vmId)
		if vm then pcall(vm.Hide, vm, fp and 0 or 1) end
		self:SetBodyHidden(fp and true or false)
		log("first person " .. tostring(fp))
	end
	if fp then placeWithFollow(self) end
end

-- ---- v3 placement: one-frame prediction (camera reads during entity update are a frame old), first-person guard,
-- worn gear (sword, dagger, shield...) hidden with the body. Head-bone follow removed: KCD2 ignores attachment
-- offsets on entity attachments and the head turns up to 15 deg against the camera.
Deagle.cfg.vm.predict = 0        -- one-frame extrapolation made it worse (amplifies camera sway); off
Deagle.cfg.vm.follow = "camera"
Deagle.cfg.vm.gearDist = 0.35    -- worn items: within this of the hips / spine
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

local baseSetBodyHidden = Deagle.SetBodyHidden
function Deagle:SetBodyHidden(hide)
	baseSetBodyHidden(self, hide)
	self:HideGear(hide and self.cfg.vm.hideBody)
end

function Deagle:VmPlace()
	local p = self:Player()
	if not p or not self.vmShown then return end
	local now = System.GetCurrTime()
	local pr = self.vmPred
	if not pr or now ~= pr.t then
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
		local k, of, op = self.cfg.vm.predict, f, cp
		if pr and pr.f and (now - pr.t) < 0.1 and k > 0 then
			of = vnorm(vadd(f, vscale(vadd(f, vscale(pr.f, -1)), k)))
			op = vadd(cp, vscale(vadd(cp, vscale(pr.p, -1)), k))
		end
		self.vmPred = { t = now, f = f, p = cp, outF = of, outP = op, fp = fp }
		pr = self.vmPred
	end
	if not pr.fp then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	local o, f = self.cfg.vm.offset, pr.outF
	local r = vnorm(vcross(f, { x = 0, y = 0, z = 1 }))
	local u = vcross(r, f)
	vm:SetWorldPos(vadd(pr.outP, vadd(vscale(f, o.fwd), vadd(vscale(r, o.right), vscale(u, o.up)))))
	pcall(vm.SetDirectionVector, vm, f, u)
end
System.AddCCommand("deagle_predict", "Deagle.cfg.vm.predict = tonumber(([[%1]]):gsub('\"','')) or 1 Deagle.vmPred = nil System.LogAlways('[Deagle] predict ' .. Deagle.cfg.vm.predict)", "deagle_predict <0..1.5>: viewmodel lag compensation")

-- ---- v4 placement: full camera orientation including roll (KCD2's head sway rolls the camera; ignoring it made
-- the arms wobble against the view), no prediction.
function Deagle:VmPlace()
	local p = self:Player()
	if not p or not self.vmShown then return end
	local now = System.GetCurrTime()
	local cp, f = System.GetViewCameraPos(), System.GetViewCameraDir()
	if now ~= self.vmGuardT then
		self.vmGuardT = now
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
	end
	if not self.vmInFp then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	local okU, up = pcall(System.GetViewCameraUpDir)
	if not okU or not up then up = { x = 0, y = 0, z = 1 } end
	local r = vnorm(vcross(f, up))
	local u = vcross(r, f)
	local o = self.cfg.vm.offset
	vm:SetWorldPos(vadd(cp, vadd(vscale(f, o.fwd), vadd(vscale(r, o.right), vscale(u, o.up)))))
	vm:SetWorldAngles(matToAngles({ { r.x, f.x, u.x }, { r.y, f.y, u.y }, { r.z, f.z, u.z } }))
end

-- ---- depth of field off while the viewmodel is up in first person (it focuses far and blurs the gun at 0.4 m);
-- restored on holster and in dialogue/cutscene cameras, which use DOF on purpose
local baseSetBodyHidden2 = Deagle.SetBodyHidden
function Deagle:SetBodyHidden(hide)
	baseSetBodyHidden2(self, hide)
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

-- ---- camera-bone follow: the viewmodel entity rides an attachment on Henry's "camera" bone (updated with his skeleton,
-- in step with the first-person camera). KCD2 places attached entities at a constant offset from the bone (measured:
-- 1.624 m down, 0.123 m back), so the character inside the entity is shifted by the measured camera pose (slot 0 local
-- transform), self-calibrated over 15 frames.
Deagle.cfg.vm.follow = "camera"   -- "bone" tested: KCD2 places attached entities at the character origin, not on the bone
function Deagle:VmBoneFollow(on)
	local p, vm = self:Player(), self:VmEntity()
	if not (p and vm) then return end
	pcall(p.DestroyAttachment, p, 0, "deagle_vm_att")
	if not on then
		pcall(vm.SetSlotPos, vm, 0, { x = 0, y = 0, z = 0 }); pcall(vm.SetSlotAngles, vm, 0, { x = 0, y = 0, z = 0 })
		self.cfg.vm.follow = "camera"; self.vmBoneOk = false
		log("viewmodel: camera follow")
		return
	end
	p:CreateBoneAttachment(0, "camera", "deagle_vm_att")
	p:SetAttachmentObject(0, "deagle_vm_att", vm.id, -1, 0)
	pcall(vm.SetSlotPos, vm, 0, { x = 0, y = 0, z = 0 }); pcall(vm.SetSlotAngles, vm, 0, { x = 0, y = 0, z = 0 })
	self.cfg.vm.follow = "bone"; self.vmBoneOk = false
	self.vmCal = { n = 0, d = { 0, 0, 0 }, f = { 0, 0, 0 }, u = { 0, 0, 0 } }
	log("viewmodel: camera bone follow, calibrating")
end

-- one calibration sample: the camera pose expressed in the viewmodel entity's frame
function Deagle:VmBoneSample()
	local vm = System.GetEntity(self.vmId)
	local cal = self.vmCal
	if not (vm and cal) then return end
	local R = anglesToMat(vm:GetWorldAngles())
	local P = vm:GetWorldPos()
	local cp, f, u = System.GetViewCameraPos(), System.GetViewCameraDir(), System.GetViewCameraUpDir()
	local d = mtv(R, vadd(cp, vscale(P, -1)))
	local fl, ul = mtv(R, f), mtv(R, u)
	cal.n = cal.n + 1
	cal.d = { cal.d[1] + d.x, cal.d[2] + d.y, cal.d[3] + d.z }
	cal.f = { cal.f[1] + fl.x, cal.f[2] + fl.y, cal.f[3] + fl.z }
	cal.u = { cal.u[1] + ul.x, cal.u[2] + ul.y, cal.u[3] + ul.z }
	if cal.n >= 15 then
		local n = cal.n
		local d0 = { x = cal.d[1] / n, y = cal.d[2] / n, z = cal.d[3] / n }
		local f0 = vnorm({ x = cal.f[1], y = cal.f[2], z = cal.f[3] })
		local u0 = vnorm({ x = cal.u[1], y = cal.u[2], z = cal.u[3] })
		local r0 = vnorm(vcross(f0, u0)); u0 = vcross(r0, f0)
		pcall(vm.SetSlotPos, vm, 0, d0)
		pcall(vm.SetSlotAngles, vm, 0, matToAngles({ { r0.x, f0.x, u0.x }, { r0.y, f0.y, u0.y }, { r0.z, f0.z, u0.z } }))
		self.cfg.vm.boneSlotPos, self.vmCal, self.vmBoneOk = d0, nil, true
		log(string.format("viewmodel bone offset %.1f %.1f %.1f mm, forward %.3f %.3f %.3f", d0.x * 1000, d0.y * 1000, d0.z * 1000, f0.x, f0.y, f0.z))
	end
end

-- per-frame: first-person guard always; placement only in camera-follow mode; calibration samples in bone mode
local placeCamera = Deagle.VmPlace
function Deagle:VmPlace()
	if self.cfg.vm.follow == "bone" then
		local p = self:Player()
		if not p or not self.vmShown then return end
		local cp = System.GetViewCameraPos()
		local ok, hp = pcall(p.GetBonePos, p, "Head")
		local fp = ok and hp and vlen(vadd(cp, vscale(hp, -1))) < self.cfg.vm.fpMaxDist
		if fp ~= self.vmInFp then
			self.vmInFp = fp
			local vm = self.vmId and System.GetEntity(self.vmId)
			if vm then pcall(vm.Hide, vm, fp and 0 or 1) end
			self:SetBodyHidden(fp and true or false)
			log("first person " .. tostring(fp))
		end
		local now = System.GetCurrTime()
		if fp and (now - (self.gearScan or 0)) > 1.0 then self.gearScan = now; self:HideGear(true) end
		if fp and self.vmCal then self:VmBoneSample() end
		return
	end
	placeCamera(self)
end

local baseVmShow3 = Deagle.VmShow
function Deagle:VmShow()
	baseVmShow3(self)
	if self.cfg.vm.follow == "bone" and not self.vmBoneOk then self:VmBoneFollow(true) end
end

System.AddCCommand("deagle_follow", "Deagle:VmBoneFollow(([[%1]]):find('bone') ~= nil)", "deagle_follow bone|camera: how the viewmodel follows the view")

-- rebuild the viewmodel entity from scratch (undo experiments on its render flags)
function Deagle:VmRespawn(hud3d)
	if self.vmId then pcall(System.RemoveEntity, self.vmId) end
	self.vmId, self.vmInFp = nil, nil
	local vm = self:VmEntity()
	if vm and hud3d then pcall(vm.SetSlotHud3D, vm, 0) end
	if self.vmShown then self.vmShown = false; self:VmShow() end
	log("viewmodel respawned" .. (hud3d and " (HUD 3D)" or ""))
end
System.AddCCommand("deagle_vm_respawn", "Deagle:VmRespawn(([[%1]]):find('hud') ~= nil)", "deagle_vm_respawn [hud]: rebuild the viewmodel (optionally as 3D HUD)")

-- ===========================================================================
-- v5: native companion (deagle_native.asi), MWII sounds, ADS, empty idle, holster, additive recoil
-- ===========================================================================
-- generated by hand from assets/build/vm_sound_cues.json (MWII Deagle sound cues, seconds from clip start)
Deagle.vmSoundCues = {
	["shoot1"] = { { 0, "shot" } },
	["shoot2"] = { { 0, "shot" } },
	["shoot_empty"] = { { 0, "shot" } },
	["reload"] = { { 0.000, "decho/p25_pi_decho_reload_raise.wav" }, { 0.667, "decho/p25_pi_decho_reload_magrelease.wav" }, { 0.708, "decho/p25_pi_decho_reload_magout.wav" }, { 1.042, "decho/p25_pi_decho_reload_magfindwell.wav" }, { 1.375, "decho/p25_pi_decho_reload_magin.wav" }, { 1.750, "decho/p25_pi_decho_reload_end.wav" } },
	["draw"] = { { 0.000, "decho/p25_pi_decho_raise_first_raise.wav" }, { 0.417, "decho/p25_pi_decho_raise_first_switch.wav" }, { 0.625, "decho/p25_pi_decho_raise_first_end.wav" } },
	["reload_empty"] = { { 0.000, "decho/p25_pi_decho_reload_empty_raise.wav" }, { 0.125, "decho/p25_pi_decho_reload_empty_pivot.wav" }, { 0.208, "decho/p25_pi_decho_reload_empty_magout.wav" }, { 0.917, "decho/p25_pi_decho_reload_empty_magfindwell.wav" }, { 1.125, "decho/p25_pi_decho_reload_empty_magin.wav" }, { 1.375, "decho/p25_pi_decho_reload_empty_tilt.wav" }, { 1.750, "decho/p25_pi_decho_reload_empty_slideback.wav" }, { 1.833, "decho/p25_pi_decho_reload_empty_slideclose.wav" }, { 2.042, "decho/p25_pi_decho_reload_empty_end.wav" } },
	["inspect"] = { { 0.000, "decho/p25_pi_decho_inspect_raise.wav" }, { 0.083, "decho/p25_pi_decho_inspect_pivot.wav" }, { 0.208, "decho/p25_pi_decho_inspect_magrelease.wav" }, { 0.208, "decho/p25_pi_decho_inspect_magout.wav" }, { 0.750, "decho/p25_pi_decho_inspect_magcatch.wav" }, { 2.500, "decho/p25_pi_decho_inspect_magfindwell.wav" }, { 2.917, "decho/p25_pi_decho_inspect_magin.wav" }, { 3.375, "decho/p25_pi_decho_inspect_rotate.wav" }, { 3.792, "decho/p25_pi_decho_inspect_slideback.wav" }, { 4.542, "decho/p25_pi_decho_inspect_slideclose.wav" }, { 4.667, "decho/p25_pi_decho_inspect_end.wav" } },
}

local A = Deagle.cfg.vm.anims
A.idleEmpty = { "deagle_vm_idle_empty", 2 }
A.holster = { "deagle_vm_holster", 37 }
A.ads = { "deagle_vm_ads", 2 }
A.shootAdd = { { "deagle_vm_shoot1_add", 35 }, { "deagle_vm_shoot2_add", 35 } }
A.shootEmptyAdd = { "deagle_vm_shoot_empty_add", 13 }
Deagle.cfg.vm.adsBlend = 0.14       -- seconds to blend hip <-> ADS
Deagle.cfg.vm.holsterSpeed = 1.6
Deagle.cfg.vm.recoilLayer = 1
Deagle.cfg.vm.soundVol = { shot = 1.0, foley = 0.75 }
local CUE_SEQ = { shoot = "shoot1", shootEmpty = "shoot_empty", reload = "reload", reloadEmpty = "reload_empty", draw = "draw", inspect = "inspect" }

-- ---- native link: System.LogAlways("[DN] ...") is swallowed by the plugin; it answers with the console command below
function Deagle:Native(cmd) System.LogAlways("[DN] " .. cmd) end
System.AddCCommand("deagle_native_ok", "Deagle.native = true; System.LogAlways('[Deagle] native plugin linked')", "internal: native plugin handshake")
function Deagle:NativeHello()
	self.native = false
	self:Native("hello")
end
function Deagle:NativeSyncVm()
	if not self.native then return end
	local c = self.cfg.vm
	if self.vmId then self:Native("vm 0x" .. (string.match(tostring(self.vmId), "(%x+)%s*$") or "0")) end
	self:Native(string.format("off %.4f %.4f %.4f", c.offset.right, c.offset.fwd, c.offset.up))
	self:Native("follow " .. ((self.vmShown and self.vmInFp) and "1" or "0"))
end

-- ---- sounds: cue lists per clip, played by the plugin (no plugin = silent, gameplay unaffected)
function Deagle:Snd(file, vol)
	if self.native then self:Native(string.format("snd %s %.2f", file, vol or 1)) end
end
function Deagle:PlayCues(key)
	local seq = CUE_SEQ[key]
	local cues = seq and Deagle.vmSoundCues[seq]
	if not cues then return end
	local tok = self.vmAnimToken
	local v = self.cfg.vm.soundVol
	for _, c in ipairs(cues) do
		local t, file = c[1], c[2]
		local function fire()
			if Deagle.vmAnimToken ~= tok and key ~= "shoot" and key ~= "shootEmpty" then return end   -- clip was interrupted
			if file == "shot" then Deagle:Snd("decho/decho-" .. math.random(1, 8) .. ".wav", v.shot)
			else Deagle:Snd(file, v.foley) end
		end
		if t <= 0.001 then fire() else Script.SetTimer(math.floor(t * 1000), fire) end
	end
end

-- ---- which loop to rest in: ADS when aiming, slide-locked idle when empty, else idle
function Deagle:VmRestKey()
	local s = self.state
	if s.reloading then return "idle" end            -- reload ends with the slide forward (ammo refills a moment later)
	if s.aiming then return "ads" end
	if s.ammo == 0 then return "idleEmpty" end
	return "idle"
end

-- v5 player: speed + blend overrides, rest-loop aware return, sound cues, native sync
function Deagle:VmPlay(key, loop, speed, blend)
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return 0 end
	local a = c.anims[key]
	if key == "shoot" then
		self.vmShot = (self.vmShot or 0) + 1
		a = a[(self.vmShot - 1) % #a + 1]
	end
	local name, frames = a[1], a[2]
	speed = speed or 1.0
	local rest = { idle = true, idleEmpty = true, ads = true }
	if self.vmAnim and self.vmAnim.name == name and not rest[key] then
		pcall(vm.StartAnimation, vm, 0, c.anims.idle[1], c.layer, 0, 1.0, true)
	end
	local ok, started = pcall(vm.StartAnimation, vm, 0, name, c.layer, blend or c.blend, speed, loop and true or false)
	if not (ok and started) and self.vmLastFail ~= name then
		self.vmLastFail = name
		log("$4viewmodel anim did not start: " .. name .. " " .. tostring(started))
	end
	local len = frames / c.fps / speed
	self.vmAnimToken = (self.vmAnimToken or 0) + 1
	local tok = self.vmAnimToken
	self.vmAnim = { key = key, name = name, loop = loop, ends = System.GetCurrTime() + len }
	if not loop then
		self:PlayCues(key)
		Script.SetTimer(math.max(1, math.floor(len * 1000) - 30), function()
			if Deagle.vmAnimToken == tok and Deagle.vmShown and not Deagle.vmHolstering then Deagle:VmPlay(Deagle:VmRestKey(), true) end
		end)
	end
	return len
end

-- recoil while aiming: additive clip on its own layer, the ADS pose underneath stays put
function Deagle:VmRecoilAdd(empty)
	local vm, c = self:VmEntity(), self.cfg.vm
	if not vm then return end
	local a = empty and c.anims.shootEmptyAdd or c.anims.shootAdd[((self.vmShot or 0) % 2) + 1]
	self.vmShot = (self.vmShot or 0) + 1
	pcall(vm.StartAnimation, vm, 0, a[1], c.recoilLayer, 0.0, 1.0, false)
	self:PlayCues(empty and "shootEmpty" or "shoot")
end

local baseFire5 = Deagle.Fire
function Deagle:Fire()
	local s = self.state
	if not (self.cfg.vm.enabled and s.aiming) then return baseFire5(self) end
	-- aimed shot: run the base shot without its hip clip, then layer the additive recoil
	local before, last = s.ammo, s.lastShot
	local savedPlay = self.VmPlay
	self.VmPlay = function() return 0 end
	local ok, err = pcall(baseFire5, self)
	self.VmPlay = savedPlay
	if not ok then log("$4fire: " .. tostring(err)) end
	if s.lastShot ~= last then
		self:VmRecoilAdd(before == 1)
		if s.ammo == 0 then self:VmPlay("idleEmpty", true) end
	end
end

-- ADS in/out blends the rest pose (FOV zoom is the existing SetAim)
local baseSetAim5 = Deagle.SetAim
function Deagle:SetAim(on)
	baseSetAim5(self, on)
	local busy = self.vmAnim and not self.vmAnim.loop and System.GetCurrTime() < (self.vmAnim.ends or 0)
	if self.vmShown and not busy and not self.vmHolstering then
		self:VmPlay(self:VmRestKey(), true, 1.0, self.cfg.vm.adsBlend)
	end
end

-- inspect only from the hip
local baseInspect5 = Deagle.Inspect
function Deagle:Inspect()
	if self.state.aiming then return end
	baseInspect5(self)
end

-- holster: play the reversed draw, then hide
local baseHideInHand5 = Deagle.HideInHand
function Deagle:HideInHand()
	if self.vmShown and not self.vmHolstering and self.cfg.vm.enabled then
		self.vmHolstering = true
		local len = self:VmPlay("holster", false, self.cfg.vm.holsterSpeed)
		Script.SetTimer(math.floor(len * 1000), function()
			Deagle.vmHolstering = false
			if not Deagle.state.drawn then baseHideInHand5(Deagle); Deagle:NativeSyncVm() end
		end)
		return
	end
	self.vmHolstering = false
	baseHideInHand5(self)
	self:NativeSyncVm()
end

-- show: tell the plugin which entity to place; first-person changes re-sync follow
local baseVmShow5 = Deagle.VmShow
function Deagle:VmShow()
	self.vmHolstering = false
	baseVmShow5(self)
	self:NativeSyncVm()
end
local baseVmPlace5 = Deagle.VmPlace
function Deagle:VmPlace()
	local wasFp = self.vmInFp
	if self.native and self.vmInFp and self.cfg.vm.follow ~= "bone" then
		-- the plugin places the viewmodel on the final camera; Lua only runs the first-person guard (once per frame)
		local saved = self.cfg.vm.follow
		self.cfg.vm.follow = "bone"          -- reuse the guard-only branch of the bone-follow code path
		baseVmPlace5(self)
		self.cfg.vm.follow = saved
	else
		baseVmPlace5(self)
	end
	if self.vmInFp ~= wasFp then self:NativeSyncVm() end
end

-- say hello on every gameplay start (save loads clear the plugin's view of Lua state too)
local baseOnGameplayStarted5 = Deagle.OnGameplayStarted
function Deagle:OnGameplayStarted()
	baseOnGameplayStarted5(self)
	self.vmId = nil
	self:NativeHello()
end
local baseEnsureInit5 = Deagle.EnsureInit
function Deagle:EnsureInit()
	baseEnsureInit5(self)
	if self.native == nil then self:NativeHello() end
end
System.AddCCommand("deagle_native_test", "Deagle:NativeHello() Deagle:Snd('decho/decho-1.wav', 1)", "ping the native plugin and play a shot sound")

-- ---- own muzzle effects (Libs/Particles/deagle.xml, preloaded via PreloadList.txt); port smoke at the ejection port
Deagle.cfg.muzzleFx = "deagle.muzzle"
Deagle.cfg.portFx = "deagle.port_smoke"
Deagle.cfg.muzzleFxScale = 1.0
local baseEjectCase5 = Deagle.EjectCase
function Deagle:EjectCase()
	baseEjectCase5(self)
	local port = self:VmBone("tag_brass")
	if port then self:SpawnFx(self.cfg.portFx, port, System.GetViewCameraUpDir(), 1.0) end
end
local baseSpawnFx5 = Deagle.SpawnFx
function Deagle:SpawnFx(name, pos, normal, scale)
	if name == self.cfg.muzzleFx then scale = self.cfg.muzzleFxScale end
	return baseSpawnFx5(self, name, pos, normal, scale)
end
