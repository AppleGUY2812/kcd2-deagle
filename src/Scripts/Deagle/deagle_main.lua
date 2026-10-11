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
	handPos = { x = 0, y = 0, z = 0 },     -- frame offset/rotation in the hand, tuned live
	handAng = { x = 0, y = 0, z = 0 },     -- degrees
	slideTravel = 0.035,                    -- metres the slide moves back per shot
	slideMs = { 0, 45, 95 },                -- back, half way, home
	ejectPort = { x = 0.014, y = 0.06, z = 0.06 }, -- in frame space (muzzle +Y, up +Z)
	ejectImpulse = 0.06,
	caseLifeMs = 20000,
	muzzle = { x = 0, y = 0.22, z = 0.055 },
	holdAnim = "deagle_idle",            -- our CAF (Animations/humans/male/deagle), upper-body layer
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

local function toWorld(ent, v)
	local o = ent:GetWorldPos()
	local x, y, z = ent:GetDirectionVector(0), ent:GetDirectionVector(1), ent:GetDirectionVector(2)
	return vadd(o, vadd(vscale(x, v.x), vadd(vscale(y, v.y), vscale(z, v.z))))
end

function Deagle:ShowInHand()
	local p, c = self:Player(), self.cfg
	self:HideInHand()
	local ok, err = pcall(function()
		local frame = spawnModel("deagle_frame", c.modelDir .. "deagle_frame.cgf", false)
		local slide = spawnModel("deagle_slide", c.modelDir .. "deagle_slide.cgf", false)
		local mag = spawnModel("deagle_mag", c.modelDir .. "deagle_mag.cgf", false)
		self.handEnt, self.slideEnt, self.magEnt = frame.id, slide.id, mag.id
		frame:AttachChild(slide.id, 0)
		frame:AttachChild(mag.id, 0)
		for _, child in ipairs({ slide, mag }) do
			child:SetLocalPos({ x = 0, y = 0, z = 0 })
			child:SetLocalAngles({ x = 0, y = 0, z = 0 })
		end
		p.human:AttachEntityToHand(frame.id, 1)
		self:ApplyHandOffset()
	end)
	if not ok then log("$4show in hand failed: " .. tostring(err)) end
	self:PlayHold()
	self:StartWatchdog()
end

function Deagle:ApplyHandOffset()
	local frame = self.handEnt and System.GetEntity(self.handEnt)
	if not frame then return end
	local a = self.cfg.handAng
	try("hand offset", function()
		frame:SetLocalPos(self.cfg.handPos)
		frame:SetLocalAngles({ x = math.rad(a.x), y = math.rad(a.y), z = math.rad(a.z) })
	end)
end

function Deagle:SlideCycle()
	local slide = self.slideEnt and System.GetEntity(self.slideEnt)
	if not slide then return end
	local c = self.cfg
	local steps = { -c.slideTravel, -c.slideTravel * 0.45, 0 }
	for i, ms in ipairs(c.slideMs) do
		local y = steps[i]
		local set = function() pcall(function() slide:SetLocalPos({ x = 0, y = y, z = 0 }) end) end
		if ms <= 0 then set() else Script.SetTimer(ms, set) end
	end
end

function Deagle:EjectCase()
	local frame = self.handEnt and System.GetEntity(self.handEnt)
	if not frame then return end
	local c = self.cfg
	try("eject case", function()
		local pos = toWorld(frame, c.ejectPort)
		local case = spawnModel("deagle_case_" .. tostring(math.random(1, 1e9)), c.modelDir .. "deagle_case.cgf", true, 0.02)
		case:SetWorldPos(pos)
		local right, up, fwd = frame:GetDirectionVector(0), frame:GetDirectionVector(2), frame:GetDirectionVector(1)
		local dir = vnorm(vadd(vscale(right, 1.0), vadd(vscale(up, 0.7), vscale(fwd, -0.25 + math.random() * 0.2))))
		case:AddImpulse(-1, pos, dir, c.ejectImpulse, 1)
		local id = case.id
		Script.SetTimer(c.caseLifeMs, function() pcall(System.RemoveEntity, id) end)
	end)
end

function Deagle:MuzzlePos()
	local frame = self.handEnt and System.GetEntity(self.handEnt)
	if frame then
		local ok, pos = pcall(toWorld, frame, self.cfg.muzzle)
		if ok then return pos end
	end
	return vadd(System.GetViewCameraPos(), vscale(System.GetViewCameraDir(), 0.7))
end

function Deagle:PlayHold()
	local c = self.cfg
	if c.holdAnim and c.holdAnim ~= "" then
		local p = self:Player()
		local ok, started = pcall(p.StartAnimation, p, 0, c.holdAnim, c.animLayer, 0.25, 1.0, true)
		if not (ok and started) then log("$4hold anim did not start: " .. tostring(started)) end
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
		if ok and not running then
			pcall(p.StartAnimation, p, 0, c.holdAnim, c.animLayer, 0.2, 1.0, true)
			self.reapplied = (self.reapplied or 0) + 1
		end
	end
	Script.SetTimer(250, function() Deagle:HoldWatchdog(token) end)
end

function Deagle:StartWatchdog()
	self.watchToken = (self.watchToken or 0) + 1
	local token = self.watchToken
	Script.SetTimer(250, function() Deagle:HoldWatchdog(token) end)
end

function Deagle:HideInHand()
	local p = self:Player()
	if not self.handEnt then return end
	if self.cfg.holdFragment ~= "" then try("stop hold pose", function() p.human:StopAnim() end) end
	pcall(p.StopAnimation, p, 0, self.cfg.animLayer)
	try("detach from hand", function() p.human:DetachFromHand(1) end)
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

log("loaded v1.0")
