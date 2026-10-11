-- Desert Eagle for KCD2: scripted semi-auto pistol (v0: gameplay core, no custom model yet).
-- Everything is pcall-guarded so a missing engine binding logs an error instead of breaking the game.

Deagle = Deagle or {}

Deagle.cfg = {
	itemId = "d81561f0-5635-4197-84c2-7f2b1a92f013",   -- deagle_pistol (Libs/Tables/item/item__deagle.xml)
	ammoId = "214b29af-66a9-42be-882a-d63562010b7b",   -- deagle_ammo
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
	impactFx = nil,
	bloodFx = nil,
	flashRadius = 9,
	flashIntensity = 6,
	flashMs = 45,
	recoil = 1.0,
	kickPitch = 2.6,          -- degrees of muzzle climb per shot
	kickYaw = 0.8,
	lightSlot = 15,
}

Deagle.state = Deagle.state or {
	ammo = 0,
	drawn = false,
	aiming = false,
	reloading = false,
	lastShot = -100,
	logActions = false,
	baseFov = nil,
}

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

function Deagle:ApplyHit(hit, dir)
	local c = self.cfg
	local e = hit.entity
	local p = self:Player()
	if e and e.soul and e ~= p then
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
		self:SpawnFx(c.bloodFx, hit.pos, hit.normal)
		local hp = "?"
		pcall(function() hp = string.format("%.1f/%.1f", e.actor:GetHealth(), e.actor:GetMaxHealth()) end)
		log("hit " .. tostring(e:GetName()) .. " mode " .. c.hitMode .. " part " .. tostring(hit.bodyPart) .. " dist " .. string.format("%.1f", hit.dist or -1) .. " hp " .. hp)
	else
		self:SpawnFx(c.impactFx, hit.pos, hit.normal)
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
		return
	end
	local now = System.GetCurrTime()
	if now - s.lastShot < c.interval then return end
	if s.ammo <= 0 then
		self:PlayTrigger(c.dryTrigger)
		self:Reload()
		return
	end
	s.lastShot = now
	s.ammo = s.ammo - 1

	local pos = System.GetViewCameraPos()
	local dir = coneDir(System.GetViewCameraDir(), s.aiming and c.spreadAim or c.spreadHip)

	self:PlayTrigger(c.shotTrigger)
	self:SpawnFx(c.muzzleFx, vadd(pos, vscale(dir, 0.7)), dir, 0.6)
	self:Flash()
	self:Recoil()

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

function Deagle:Notify(text)
	try("info text", Game.SendInfoText, text, false)
end

function Deagle:Give(line)
	local p = self:Player()
	local rounds = tonumber(line) or 49
	if not self:Owns() then try("give pistol", ItemUtils.CreateInvItem, p, self.cfg.itemId, 1) end
	if rounds > 0 then try("give ammo", ItemUtils.CreateInvItem, p, self.cfg.ammoId, rounds) end
	log("give: pistol " .. self:CountOf(self.cfg.itemId) .. ", cartridges " .. self:CountOf(self.cfg.ammoId))
end

function Deagle:Reload()
	local s, c = self.state, self.cfg
	if s.reloading or s.ammo >= c.magSize then return end
	if self:CountOf(c.ammoId) <= 0 then
		self:Notify("No .50 AE cartridges")
		return
	end
	s.reloading = true
	self:PlayTrigger(c.reloadTrigger)
	Script.SetTimer(math.floor(c.reloadTime * 1000), function()
		s.reloading = false
		local take = math.min(c.magSize - s.ammo, self:CountOf(c.ammoId))
		if take > 0 then
			try("use ammo", function() self:Player().inventory:DeleteItemOfClass(c.ammoId, take) end)
			s.ammo = s.ammo + take
		end
		self:ShowAmmo()
	end)
end

function Deagle:ShowAmmo()
	self:Notify(string.format("Desert Eagle  %d / %d   (%d spare)", self.state.ammo, self.cfg.magSize, self:CountOf(self.cfg.ammoId)))
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
	end
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
	local key, value = string.match(line or "", "^%s*(%S+)%s+(.-)%s*$")
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
System.AddCCommand("deagle_give", "Deagle:EnsureInit() Deagle:Give([[%1]])", "deagle_give [cartridges=49]: add the pistol (if missing) and cartridges")
System.AddCCommand("deagle_cfg", "Deagle:Cfg()", "Print all settings")
System.AddCCommand("deagle_fx", "Deagle:FxTest([[%1]])", "deagle_fx <effect>: spawn a particle effect 3 m ahead")

Deagle:HookActions()
if not Deagle.listening then
	Deagle.listening = true
	try("event listener", function()
		UIAction.RegisterEventSystemListener(Deagle, "System", "OnGameplayStarted", "OnGameplayStarted")
	end)
end

log("loaded v0.5")
