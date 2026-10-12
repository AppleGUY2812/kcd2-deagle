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
		table.insert(M.anims, { t = M.t, name = name, layer = layer, loop = loop, blend = blend }) return true
	end
	function e:StopAnimation(slot, layer) self.layers[layer] = nil end
	e.flags = 0
	function e:GetFlags() return self.flags end
	function e:SetFlags(f, mode) if mode == 0 then self.flags = bit.bor(self.flags, f) elseif mode == 2 then self.flags = bit.band(self.flags, bit.bnot(f)) else self.flags = f end end
	function e:IsAnimationRunning() return true end
	function e:GetBonePos(b) return vec(self.pos.x + 0.3, self.pos.y + 0.4, self.pos.z - 0.05) end
	function e:GetBoneDir(b) return vec(0, 1, 0) end
	function e:GetBoneLocal(b, t) return vec(t.x, t.y, t.z) end   -- a world direction in the bone's axes (bone = world axes here)
	function e:AddImpulse() return true end
	-- particle effects in entity slots. Like KCD2 (checked in game 2026-10-12): CryEngine's table form
	-- (slot, effect, { Scale, bPrime, ... }) returns the slot; plain arguments create nothing and return nil
	e.nslots = 1
	e.slots = {}
	function e:LoadParticleEffect(slot, fx, params)
		if type(params) ~= "table" then M.badFxLoads = (M.badFxLoads or 0) + 1 return nil end
		if slot < 0 then slot = self.nslots; self.nslots = self.nslots + 1 end
		if self.slots[slot] then M.fxLoadsOnBusySlot = (M.fxLoadsOnBusySlot or 0) + 1 end
		self.slots[slot] = fx
		M.slotFx = (M.slotFx or 0) + 1; M.lastSlotFx = fx; M.lastSlotScale = params.Scale; M.lastSlotPrime = params.bPrime
		M.lastSlotEntity = self.name
		return slot
	end
	function e:FreeSlot(slot) self.slots[slot] = nil end
	function e:IsSlotValid(slot) return self.slots[slot] ~= nil end
	function e:SetDirectionVector(d) self.dir = vec(d.x, d.y, d.z) end
	function e:SetSlotWorldTM(slot, pos, dir) M.slotMoves = (M.slotMoves or 0) + 1 end
	function e:HideAttachment(slot, name, hide) M.hidden = M.hidden or {}; M.hidden[name] = hide end
	function e:CreateBoneAttachment(slot, bone, name) M.boneAttach = (M.boneAttach or 0) + 1 end
	function e:SetAttachmentEffect(slot, name, fx, offset, dir, scale, flags)
		if type(dir) ~= "table" or type(scale) ~= "number" then error("SetAttachmentEffect takes plain arguments") end
		M.attachFx = (M.attachFx or 0) + 1; M.lastAttachScale = scale; M.lastAttachDir = dir; M.lastAttachName = fx
	end
	function e:GetDirectionVector(axis) return ({ vec(1, 0, 0), vec(0, 1, 0), vec(0, 0, 1) })[(axis or 1) + 1] end
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
M.view = { pitch = 0, yaw = 0 }   -- the camera follows PlayerSetViewAngles (recoil climbs and returns are measurable)
player.actor = { PlayerSetViewAngles = function(self, a) M.view.pitch, M.view.yaw = a.x or 0, a.z or 0 end,
	SetViewShake = function() end, GetViewAngles = function() return vec(M.view.pitch, 0, M.view.yaw) end }
player.soul = {}
player.this = { id = player.id }
_G.g_localActor = player
_G.Player = { OnAction = function() end }
_G.ent_all = 0xFFFF
_G.ent_static, _G.ent_terrain = 1, 2
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
	GetViewCameraDir = function() local p, y = M.view.pitch, M.view.yaw return vec(-math.sin(y) * math.cos(p), math.cos(y) * math.cos(p), math.sin(p)) end,
	GetViewCameraUpDir = function() return vec(0, 0, 1) end,
	GetViewport = function() return 0, 0, 1920, 1080 end,
	GetViewCameraFov = function() return 1.1011 end, SetViewCameraFov = function(f) M.fov = f end,
	SpawnEntity = function(p) local e = newEntity(p.name, p.properties); entities[tostring(e.id)] = e; entities[e.id] = e; e.pos = p.position or e.pos; e.flags = p.flags or 0; return e end,
	GetEntitiesByClass = function(cls) local out = {} for k, e in pairs(entities) do if type(k) ~= "string" and e.class == cls then out[#out + 1] = e end end return out end,
	GetEntity = function(id) return entities[id] or entities[tostring(id)] end,
	RemoveEntity = function(id) local e = entities[id] or entities[tostring(id)]; if e then entities[e.id] = nil; entities[tostring(e.id)] = nil end end,
	GetEntitiesInSphere = function() return {} end,
	GetSurfaceTypeNameById = function(id) return M.surfaceNames[id] end,
	GetCVar = function(n) return 2 end, SetCVar = function(n, v) M.cvars = M.cvars or {}; M.cvars[n] = v end,
	DrawText = function(...) M.drawn_text = M.drawn_text + 1 end,
	DrawLine = function(a, b, r, g, bl, al) M.lines = (M.lines or 0) + 1 end,
	ExecuteCommand = function(c) end,
}
_G.Script = {
	SetTimer = function(ms, fn) table.insert(M.timers, { at = M.t + (ms or 0) / 1000, fn = fn }) return #M.timers end,
	-- like KCD2 (checked in game 2026-10-12): LoadScript(path, true, ...) always runs the file; without bReload, and
	-- ReloadScript, skip a file that already ran
	LoadScript = function(path, reload) if not reload and M.loaded[path] then return 1 end return M.loadScript(path) end,
	ReloadScript = function(path) if M.loaded[path] then return 1 end return M.loadScript(path) end,
}
_G.Game = { SendInfoText = function(t) M.info = t end, HideInfoText = function() end }
_G.Particle = { SpawnEffect = function(n, pos, dir, scale) M.fx = (M.fx or 0) + 1 M.lastFx = n; M.lastFxScale = scale; M.fxNames = M.fxNames or {}; M.fxNames[n] = (M.fxNames[n] or 0) + 1; M.fxScale = M.fxScale or {}; M.fxScale[n] = scale end,
	CreateMatDecal = function(pos, normal, size, life, mat) M.decals = M.decals or {}; table.insert(M.decals, { pos = pos, size = size, life = life, mat = mat }) end }
_G.Physics = { RayWorldIntersection = function() return 0 end }
M.maps = {}
_G.ActionMapManager = { LoadFromXML = function() return true end, EnableActionMap = function(name, on) M.maps[name] = on end }
_G.UIAction = { RegisterEventSystemListener = function() end }
_G.Sound = {}
_G.AudioUtils = { LookupTriggerID = function() return 1 end, ExecuteAudioTrigger = function() M.gameShot = (M.gameShot or 0) + 1 end }
_G.CryAction = {}
-- KCD2's AI hearing (XGenAIModule.ProduceSound*): every call recorded
M.aiSounds = {}
_G.enum_sound = { gun_fired = 14, bullet_hit = 16 }
_G.XGenAIModule = {
	ProduceSound = function(kind, pos, mult) table.insert(M.aiSounds, { kind = kind, pos = pos, mult = mult }) end,
	ProduceSoundWUID = function(kind, wuid, mult) table.insert(M.aiSounds, { kind = kind, wuid = wuid, mult = mult }) end,
}
-- a standing human's bones (m from its feet; facing -y, so its left is +x), for test NPCs: e.GetBonePos = M.skeleton
M.bones = {
	Hips = { 0, 0, 0.95 }, Spine = { 0, 0, 1.05 }, Spine3 = { 0, 0, 1.25 }, Neck = { 0, 0, 1.5 }, Head = { 0, 0, 1.6 },
	HeadEnd = { 0, 0, 1.95 }, LeftShoulder = { 0.06, 0, 1.43 }, LeftArm = { 0.19, 0, 1.43 }, LeftForeArm = { 0.24, 0, 1.16 },
	LeftHand = { 0.27, 0, 0.9 }, LeftHandSlot = { 0.28, 0, 0.83 }, LeftUpLeg = { 0.1, 0, 0.95 }, LeftLeg = { 0.1, 0, 0.53 },
	LeftFoot = { 0.1, 0, 0.1 }, LeftToeBase = { 0.1, -0.13, 0.02 },
}
for k, v in pairs(M.bones) do if k:find("^Left") then M.bones["Right" .. k:sub(5)] = { -v[1], v[2], v[3] } end end
function M.skeleton(e, name)
	local b, base = M.bones[name], e:GetWorldPos()
	return b and { x = base.x + b[1], y = base.y + b[2], z = base.z + b[3] } or nil
end
-- game paths load from the repo's src/ (gun files etc.); missing files fail like the engine (0)
M.loaded = {}
M.surfaceNames = { [1] = "mat_rock", [2] = "mat_window", [3] = "mat_wood", [4] = "mat_level_barrier" }   -- ray hits' surface ids
function M.loadScript(path)
	M.loaded[path] = true
	local f = io.open("src/" .. path, "r")
	if not f then return 0 end
	f:close()
	local ok, err = pcall(dofile, "src/" .. path)
	if not ok then table.insert(M.errors, "load " .. path .. ": " .. tostring(err)) return 0 end
	return 1
end
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
function M.console(name, arg)
	local code = M.cmds[name]
	if not code then table.insert(M.errors, "no console command " .. name) return end
	-- like KCD2 (checked live): %1 / %line become the argument in quotes ("clear")
	local quoted = '"' .. tostring(arg or "") .. '"'
	code = code:gsub("%%1", function() return quoted end):gsub("%%line", function() return quoted end)
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
