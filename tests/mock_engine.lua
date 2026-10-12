-- Minimal stand-in for KCD2's Lua environment: enough to run the Deagle script's flows and catch Lua errors.
local M = { t = 100.0, timers = {}, logs = {}, errors = {}, anims = {}, native = {}, drawn_text = 0, cmds = {} }
_G.MOCK = M
M.trace = {}
math.randomseed(42)
-- every effect the script has on the engine (getters excluded), for before/after comparisons (tests/trace_flows.lua)
local function fmt(v)
	local t = type(v)
	if t == "number" then return string.format("%.4g", v) end
	if t == "string" then return string.format("%q", v) end
	if t == "function" then return "fn" end
	if t == "table" then
		local mt = getmetatable(v)
		if mt and mt.__tostring then return tostring(v) end
		if v.x and v.y and v.z then return string.format("(%.4g,%.4g,%.4g)", v.x, v.y, v.z) end
		if v.name then return "<" .. tostring(v.name) .. ">" end
		local keys = {}
		for k in pairs(v) do keys[#keys + 1] = tostring(k) end
		table.sort(keys)
		return "{" .. table.concat(keys, ",") .. "}"
	end
	return tostring(v)
end
local function effectful(name) return not (name:find("^Get") or name:find("^Is") or name:find("^Has")) end
function M.record(who, name, ...)
	if not effectful(name) then return end
	local args = {}
	for i = 1, select("#", ...) do args[#args + 1] = fmt((select(i, ...))) end
	M.trace[#M.trace + 1] = string.format("%.3f %s.%s(%s)", M.t, who, name, table.concat(args, ", "))
end
function M.traced(who, name, fn)
	return function(...) M.record(who, name, select(2, ...)) return fn(...) end
end
function M.tracedStatic(who, name, fn)
	return function(...) M.record(who, name, ...) return fn(...) end
end
local function vec(x, y, z) return { x = x or 0, y = y or 0, z = z or 0 } end
local idc = 0
local function newEntity(name, props)
	idc = idc + 1
	local n = idc
	local e = { id = setmetatable({}, { __tostring = function() return string.format("userdata: %016X", 0x100000 + n) end }),
		class = "BasicEntity", pos = vec(0, 0, 0), hidden = false, name = name }
	local mt = { __index = function(t, k) return function(self, ...) return nil end end }   -- unknown methods: no-op
	setmetatable(e, mt)
	function e:GetName() return self.name end
	function e:GetWorldPos() return vec(self.pos.x, self.pos.y, self.pos.z) end
	function e:SetWorldPos(p) self.pos = vec(p.x, p.y, p.z) end
	function e:GetWorldAngles() return vec(0, 0, 0) end
	function e:SetWorldAngles(a) self.ang = a end
	function e:LoadCharacter(slot, f) return 0 end
	function e:Hide(h) self.hidden = (h == 1) end
	-- like the engine: a layer keeps its clip (a finished one-shot holds its last frame) until replaced or stopped;
	-- asking for the clip that is already on the layer is refused
	e.layers = {}
	function e:StartAnimation(slot, name, layer, blend, speed, loop)
		if self.layers[layer] == name then return false end
		self.layers[layer] = name
		table.insert(M.anims, { t = M.t, name = name, layer = layer, loop = loop }) return true
	end
	function e:StopAnimation(slot, layer) self.layers[layer] = nil end
	e.flags = 0
	function e:GetFlags() return self.flags end
	function e:SetFlags(f, mode) if mode == 0 then self.flags = bit.bor(self.flags, f) elseif mode == 2 then self.flags = bit.band(self.flags, bit.bnot(f)) else self.flags = f end end
	function e:IsAnimationRunning() return true end
	function e:GetBonePos(b) return vec(self.pos.x + 0.3, self.pos.y + 0.4, self.pos.z - 0.05) end
	function e:GetBoneDir(b) return vec(0, 1, 0) end
	function e:GetBoneLocal(b, t) return vec(0, 0, 0) end
	function e:AddImpulse() return true end
	for k, v in pairs(e) do if type(v) == "function" then e[k] = M.traced(name or "?", k, v) end end
	setmetatable(e, { __index = function(t, k) return M.traced(name or "?", k, function() return nil end) end })
	return e
end
M.newEntity = newEntity
local entities = {}
M.entities = function() return entities end
local player = newEntity("Dude")
player.class = "Player"
player.pos = vec(100, 200, 30)
function player:GetBonePos(b)
	if b == "Head" or b == "camera" then return vec(100, 200, 31.6) end
	return vec(100, 200, 30.9)
end
player.inventory = { GetCountOfClass = function() return 1 end, DeleteItemOfClass = function() end, CreateItem = function() return 1 end, AddItem = function() return true end }
player.human = { IsWeaponDrawn = function() return false end, HolsterWeapon = function() end }
player.actor = { PlayerSetViewAngles = function() end, SetViewShake = function() end, GetViewAngles = function() return vec(0, 0, 0) end }
player.soul = {}
_G.g_localActor = player
_G.Player = { OnAction = function() end }
_G.ent_all = 0xFFFF
_G.ENTITY_FLAG_CASTSHADOW = 2
_G.ENTITY_FLAG_NO_SAVE = 32768
_G.System = {
	LogAlways = function(s)
		s = tostring(s)
		if s:sub(1, 4) == "[DN]" then table.insert(M.native, s)            -- what the plugin would receive
		else table.insert(M.logs, s); if s:find("%$4") then table.insert(M.errors, s) end end
	end,
	Log = function(s) end, Warning = function(s) table.insert(M.errors, "WARN " .. tostring(s)) end,
	AddCCommand = function(name, code, help) M.cmds[name] = code end,
	GetCurrTime = function() return M.t end,
	GetViewCameraPos = function() return vec(100, 200, 31.75) end,
	GetViewCameraDir = function() return vec(0, 1, 0) end,
	GetViewCameraUpDir = function() return vec(0, 0, 1) end,
	GetViewport = function() return 0, 0, 1920, 1080 end,
	GetViewCameraFov = function() return 1.1011 end, SetViewCameraFov = function(f) M.fov = f end,
	SpawnEntity = function(p) local e = newEntity(p.name, p.properties); entities[tostring(e.id)] = e; entities[e.id] = e; e.pos = p.position or e.pos; e.flags = p.flags or 0; return e end,
	GetEntitiesByClass = function(cls) local out = {} for k, e in pairs(entities) do if type(k) ~= "string" and e.class == cls then out[#out + 1] = e end end return out end,
	GetEntity = function(id) return entities[id] or entities[tostring(id)] end,
	RemoveEntity = function(id) local e = entities[id] or entities[tostring(id)]; if e then entities[e.id] = nil; entities[tostring(e.id)] = nil end end,
	GetEntitiesInSphere = function() return {} end,
	GetCVar = function(n) return 2 end, SetCVar = function(n, v) M.cvars = M.cvars or {}; M.cvars[n] = v end,
	DrawText = function(...) M.drawn_text = M.drawn_text + 1 end,
	ExecuteCommand = function(c) end,
}
_G.Script = {
	SetTimer = function(ms, fn) table.insert(M.timers, { at = M.t + (ms or 0) / 1000, fn = fn }) return #M.timers end,
	LoadScript = function() return true end, ReloadScript = function() end,
}
_G.Game = { SendInfoText = function(t) M.info = t end, HideInfoText = function() end }
_G.Particle = { SpawnEffect = function(n) M.fx = (M.fx or 0) + 1 M.lastFx = n end, CreateMatDecal = function() end }
_G.Physics = { RayWorldIntersection = function() return 0 end }
_G.ActionMapManager = { LoadFromXML = function() return true end, EnableActionMap = function() end }
_G.UIAction = { RegisterEventSystemListener = function() end }
_G.Sound = {}
_G.AudioUtils = { LookupTriggerID = function() return 1 end, ExecuteAudioTrigger = function() M.gameShot = (M.gameShot or 0) + 1 end }
_G.CryAction = {}
-- run due timers, frame by frame (each frame also fires 0 ms timers once)
function M.advance(sec, fps)
	fps = fps or 60
	local steps = math.floor(sec * fps + 0.5)
	for _ = 1, steps do
		M.t = M.t + 1 / fps
		local due, keep = {}, {}
		for _, tm in ipairs(M.timers) do if tm.at <= M.t then due[#due + 1] = tm else keep[#keep + 1] = tm end end
		M.timers = keep
		table.sort(due, function(a, b) return a.at < b.at end)
		for _, tm in ipairs(due) do
			local ok, err = pcall(tm.fn)
			if not ok then table.insert(M.errors, "TIMER " .. tostring(err)) end
		end
	end
end
function M.console(name)
	local code = M.cmds[name]
	if not code then table.insert(M.errors, "no console command " .. name) return end
	local f, err = loadstring(code)
	if not f then table.insert(M.errors, "compile " .. name .. ": " .. err) return end
	local ok, e = pcall(f); if not ok then table.insert(M.errors, "cmd " .. name .. ": " .. tostring(e)) end
end
for _, lib in ipairs({ "System", "Script", "Game", "Particle", "Physics", "ActionMapManager", "UIAction", "AudioUtils" }) do
	for k, v in pairs(_G[lib]) do
		if type(v) == "function" and not (lib == "System" and k == "LogAlways") then _G[lib][k] = M.tracedStatic(lib, k, v) end
	end
end
local log = System.LogAlways
System.LogAlways = function(msg) M.record("System", "LogAlways", msg) return log(msg) end
return M
