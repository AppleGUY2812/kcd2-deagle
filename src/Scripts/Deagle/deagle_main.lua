-- Desert Eagle for KCD2: scripted semi-auto pistol (v0: gameplay core, no custom model yet).
-- Everything is pcall-guarded so a missing engine binding logs an error instead of breaking the game.

Deagle = Deagle or {}

Deagle.cfg = {
	magSize = 7,
	interval = 0.22,          -- seconds between shots (semi-auto cap)
	reloadTime = 2.1,
	range = 250,
	damage = 120,
	stamina = 40,
	impulse = 220,            -- physics push on props and bodies
	spreadHip = 1.6,          -- degrees
	spreadAim = 0.25,
	aimFovScale = 0.8,
	hitMode = "hit",          -- "hit" = game hit pipeline (reactions), "soul" = direct soul damage
	hitType = "normal",
	shotTrigger = "c_f_firearm_shot",
	dryTrigger = nil,
	reloadTrigger = nil,
	muzzleFx = nil,           -- particle effect names, filled in once verified
	impactFx = nil,
	bloodFx = nil,
	flashRadius = 9,
	flashIntensity = 6,
	flashMs = 45,
	recoil = 1.0,
	lightSlot = 15,
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
	try("recoil", function()
		p.actor:SetViewShake({ x = math.rad(2.2) * k, y = math.rad(0.6) * k, z = math.rad(0.4) * k },
			{ x = 0.004 * k, y = 0.02 * k, z = 0.006 * k }, 0.16, 1 / 30, 0.2)
	end)
end

-- ---------------------------------------------------------------------------
-- shooting
-- ---------------------------------------------------------------------------
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
			try("DealDamage", function() e.soul:DealDamage(c.damage, c.stamina, __null, true) end)
		end
		self:SpawnFx(c.bloodFx, hit.pos, hit.normal)
		log("hit " .. tostring(e:GetName()) .. " part " .. tostring(hit.partid) .. " dist " .. string.format("%.1f", hit.dist or -1))
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

function Deagle:Reload()
	local s, c = self.state, self.cfg
	if s.reloading or s.ammo >= c.magSize then return end
	s.reloading = true
	self:PlayTrigger(c.reloadTrigger)
	Script.SetTimer(math.floor(c.reloadTime * 1000), function()
		s.ammo = c.magSize
		s.reloading = false
		self:ShowAmmo()
	end)
end

function Deagle:ShowAmmo()
	try("ammo text", Game.SendInfoText, "Desert Eagle  " .. self.state.ammo .. " / " .. self.cfg.magSize, false, nil, 1.5)
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
	s.drawn = not s.drawn
	if not s.drawn then self:SetAim(false) end
	log(s.drawn and "drawn" or "holstered")
	self:ShowAmmo()
end

-- ---------------------------------------------------------------------------
-- input: our action map + a wrapper around gamerules' action hook
-- ---------------------------------------------------------------------------
function Deagle:OnAction(action, activation)
	local s = self.state
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

function Deagle:HookActions()
	if not (g_gameRules and g_gameRules.Client) then return false end
	local client = g_gameRules.Client
	if client.OnActorAction and client.OnActorAction == self.actionWrapper then return true end
	local orig = client.OnActorAction
	self.actionWrapper = function(gr, actor, action, activation, value)
		local consumed = false
		local ok, err = pcall(function() consumed = Deagle:OnAction(action, activation) end)
		if not ok then log("$4OnAction error: " .. tostring(err)) end
		if consumed then return false end
		if orig then return orig(gr, actor, action, activation, value) end
		return true
	end
	client.OnActorAction = self.actionWrapper
	log("action hook installed")
	return true
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

-- keeps the hook alive across level loads / gamerules re-creation
function Deagle:Tick()
	if self:HookActions() and not self.inputLoaded and self:Player() then
		self:LoadInput()
	end
	Script.SetTimer(1000, function() Deagle:Tick() end)
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

function Deagle:Cfg()
	local keys = {}
	for k in pairs(self.cfg) do keys[#keys + 1] = k end
	table.sort(keys)
	for _, k in ipairs(keys) do log("cfg." .. k .. " = " .. tostring(self.cfg[k])) end
end

System.AddCCommand("deagle_probe", "Deagle:Probe()", "Dump engine APIs used by the Deagle mod to the log")
System.AddCCommand("deagle_toggle", "Deagle:Toggle()", "Draw / holster the Desert Eagle")
System.AddCCommand("deagle_fire", "Deagle:Fire()", "Fire one shot")
System.AddCCommand("deagle_reload", "Deagle:Reload()", "Reload")
System.AddCCommand("deagle_input", "Deagle:LoadInput()", "Reload the Deagle key bindings")
System.AddCCommand("deagle_log_actions", "Deagle.state.logActions = not Deagle.state.logActions", "Toggle logging of every input action")
System.AddCCommand("deagle_set", "Deagle:Set([[%line]])", "deagle_set <key> <value>: change a setting live")
System.AddCCommand("deagle_cfg", "Deagle:Cfg()", "Print all settings")
System.AddCCommand("deagle_fx", "Deagle:FxTest([[%1]])", "deagle_fx <effect>: spawn a particle effect 3 m ahead")

if not Deagle.ticking then
	Deagle.ticking = true
	Deagle:Tick()
end

log("loaded v0.1")
