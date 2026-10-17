-- Debug tools: zoom, slow motion, the test NPC and test wall, round paths (deagle_paths), the stress test.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local log, vadd, vscale, vsub = U.log, U.vadd, U.vscale, U.vsub

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
-- deagle_stress: a frame-rate test anyone can run. Infinite ammo, 10 rounds a second while the view looks at the sky
-- (no one gets hit), in phases: fire with tracers, stop with tracers off (the pieces still held age out), fire
-- without. Every 0.5 s of game time the log gets the frame time split into our Lua (the per-frame functions and Fire)
-- and the rest (the engine: rendering, particles, physics), with the rounds in the air and the tracer commands sent;
-- after each phase its frame time p50 / p95 / p99 / max and the mod's Lua per frame (average, p99) against the
-- budget in docs/PERFORMANCE.md (1.0 ms average, 2.0 ms p99). Functions called inside each other are counted once
-- (only the outermost). Made after the user's "spammed 3 clips, 140 to 70 fps" (2026-10-15).
-- deagle_stress [rounds a second] [sky | aim]: any fire rate (a CIWS is ~75; several shots a frame when needed), and
-- "aim" fires where you look instead of only at the sky, so impacts are measured too, with no recoil (the view stays
-- put); it holds fire while the view points at a person or an animal (a ray from the eye, 300 m). Lines also count impacts and impact effects.
-- A third word "ab" (deagle_stress 75 aim ab) runs the impact A/B instead: fire with everything, then with the impact
-- effects off, the holes off, the impact sounds off, a rest between, so one run shows which part of a hit costs
-- (the user's 75 aim run fell to 4 fps, 2026-10-15). Phase: name, s, fire, tracers, what is off.
local STRESS = {
	{ "idle", 2, false, true }, { "FIRE tracers on", 5, true, true }, { "stop, tracers off", 3, false, false },
	{ "idle again", 3, false, false }, { "FIRE tracers off", 5, true, false }, { "stop", 3, false, false },
}
local STRESS_AB = {
	{ "idle", 2, false, true }, { "FIRE all", 4, true, true }, { "rest", 3, false, true },
	{ "FIRE no impact fx", 4, true, true, "fx" }, { "rest", 3, false, true },
	{ "FIRE no holes", 4, true, true, "holes" }, { "rest", 3, false, true },
	{ "FIRE no hit sounds", 4, true, true, "sounds" }, { "rest", 3, false, true },
}
-- Spawn mode (Claude 5's plan step 4): deagle_lua Deagle:Stress('spawn <name> <per second>') calls a registered
-- spawner N times a second at one fixed spot ahead of the camera (where the view hits, else 10 m out) for 10 s, with
-- no gun and no ballistics, so one system's cost per event shows on its own. Owners register theirs:
-- Deagle.StressSpawners[name] = function(pos, normal, i) ... end (the table survives a reload).
local STRESS_SPAWN = { { "idle", 2, false, true }, { "SPAWN", 10, true, true }, { "rest", 3, false, true } }
Deagle.StressSpawners = Deagle.StressSpawners or {}
-- bullet holes as the game makes them (no budget): soil
Deagle.StressSpawners.hole = function(pos, normal)
	Particle.CreateMatDecal({ x = pos.x + normal.x * 0.005, y = pos.y + normal.y * 0.005, z = pos.z + normal.z * 0.005 }, normal,
		Deagle.cfg.holeSize, Deagle.cfg.holeLife, "Materials/deagle/decals/bullet_hole_soil", math.random() * 360, normal)
end
local noop = function() end
-- The shot A/B (fourth word "shot": deagle_stress 250 sky shot): fire with everything, then without one part of the
-- shot itself at a time, then the bare rounds. 250 rounds a second fell to 5 fps in 0.7 s with the engine at 200+ ms a
-- frame and only 9 shots a frame (Claude 1's run, 2026-10-17): which part of a shot builds it up? A phase that hits the
-- 150 ms guard ends early ("cliff") instead of the run, and a rest lasts until frames are back near idle (10 s at most).
local STRESS_SHOT = {
	{ "idle", 2, false, true }, { "FIRE all", 3, true, true }, { "rest", 2, false, true },
	{ "FIRE no tracers", 3, true, false }, { "rest", 2, false, true },
	{ "FIRE no flash", 3, true, true, "flash" }, { "rest", 2, false, true },
	{ "FIRE no casings", 3, true, true, "casings" }, { "rest", 2, false, true },
	{ "FIRE no gun sound", 3, true, true, "gunsound" }, { "rest", 2, false, true },
	{ "FIRE no AI hearing", 3, true, true, "hearing" }, { "rest", 2, false, true },
	{ "FIRE no gun anim", 3, true, true, "anim" }, { "rest", 2, false, true },
	{ "FIRE rounds only", 3, true, false, "shot" }, { "rest", 2, false, true },
}
-- what each name switches off: { global table, field, stand-in } ({ wrap = f }: f(the original) makes the stand-in). The gun anim's stand-ins still start the shot's sound cues
-- (they come with the clip), so "no gun anim" keeps the gunshot.
local STRESS_PARTS = {
	fx = { { "Deagle", "SpawnFx", noop } },
	holes = { { "Particle", "CreateMatDecal", noop } },
	sounds = { { "Deagle", "HitSound", noop } },
	flash = { { "Deagle", "MuzzleFlash", noop } },
	casings = { { "Deagle", "EjectCase", noop } },
	gunsound = { { "Deagle", "CueFire", noop }, { "Deagle", "RonHere", noop } },
	hearing = { { "Deagle", "ShotHeard", noop } },
	anim = {
		{ "Deagle", "VmPlay", function(s, key) if key == "shoot" or key == "shootEmpty" then s:PlayCues(key) end return 0 end },
		{ "Deagle", "VmRecoilAdd", function(s, empty) s:PlayCues(empty and "shootEmpty" or "shoot") end },
		{ "Deagle", "VmRounds", noop },
	},
	-- a casing is a physics body (one of 12, thrown again) and a port smoke effect: each on its own
	casebody = { { "Deagle", "EjectCase", function(s)
		local port = s:VmBone(s.gun.bones.brass)
		if port then s:SpawnFx(s.cfg.portFx, port, System.GetViewCameraUpDir(), 1.0) end
	end } },
	portsmoke = { { "Deagle", "SpawnFx", { wrap = function(orig)
		return function(s, name, ...) if name ~= s.cfg.portFx then return orig(s, name, ...) end end
	end } } },
}
STRESS_PARTS.shot = {}
for _, k in ipairs({ "flash", "casings", "gunsound", "hearing", "anim" }) do
	for _, e in ipairs(STRESS_PARTS[k]) do STRESS_PARTS.shot[#STRESS_PARTS.shot + 1] = e end
end
-- The profile (third word "prof": deagle_lua Deagle:Stress('1000 sky prof')): through its fire phase every function of
-- Deagle, and every engine function in PROF_ENGINE called from them, is timed: self time (without the Deagle /
-- engine functions it calls) and total, per frame, and the calls. At 1000 rounds a second our Lua took 24.9 ms a frame,
-- ~0.73 ms a shot (Claude 1's run, 2026-10-17): which functions? KCD2's Lua clock moves in 0.244 ms steps (float32
-- seconds), so a call's time is 0 or a step: the numbers are averages over many calls, and the timing itself adds ~1-2
-- us a call (pcall and a table per call).
local STRESS_PROF = { { "idle", 2, false, true }, { "FIRE profiled", 4, true, true, prof = true }, { "rest", 2, false, true } }
local PROF_ENGINE = { "System", "Physics", "Particle", "XGenAIModule", "AudioUtils", "Game", "Script", "Sound", "ItemUtils" }
local PROF_SKIP = { Stress = true, StressStop = true, VmTick = true, GetCurrAsyncTime = true, GetCurrTime = true }
local function profPack(...) return { n = select("#", ...), ... } end
local function stressProf(X, on, label)
	local P = X.prof
	if P and not on then
		for i = #P.orig, 1, -1 do local o = P.orig[i]; o[1][o[2]] = o[3] end
		X.prof = nil
		local rows = {}
		for name, st in pairs(P.stats) do if st[3] > 0 then rows[#rows + 1] = { name, st[1], st[2], st[3] } end end
		table.sort(rows, function(x, y) if x[2] ~= y[2] then return x[2] > y[2] end if x[3] ~= y[3] then return x[3] > y[3] end return x[4] > y[4] end)
		local f = math.max(1, P.frames)
		log(string.format("PROF %s: %d frames, %d shots: %.2f ms a frame in the profiled functions (KCD2's clock steps 0.244 ms: averages)",
			P.label or "", P.frames, P.shots, P.top / f * 1000))
		for i = 1, math.min(30, #rows) do
			local r = rows[i]
			log(string.format("PROF %2d %-34s self %6.3f ms/frame, total %6.3f, %7.1f calls/frame, %6.1f us self a call", i, r[1],
				r[2] / f * 1000, r[3] / f * 1000, r[4] / f, r[2] / r[4] * 1e6))
		end
		return
	end
	if not on or P then return end
	P = { stats = {}, orig = {}, frames = 0, shots = 0, depth = 0, child = {}, top = 0, label = label }
	X.prof = P
	local clock = System.GetCurrAsyncTime
	local function wrap(t, k, name, engine)
		local f = t[k]
		local st = P.stats[name] or { 0, 0, 0 }
		P.stats[name] = st
		P.orig[#P.orig + 1] = { t, k, f }
		t[k] = function(...)
			if engine and P.depth == 0 then return f(...) end   -- the game's own scripts calling it: not ours
			local d = P.depth + 1
			P.depth, P.child[d] = d, 0
			local a = clock()
			local r = profPack(pcall(f, ...))
			local el = clock() - a
			P.depth = d - 1
			st[1], st[2], st[3] = st[1] + el - P.child[d], st[2] + el, st[3] + 1
			if d > 1 then P.child[d - 1] = P.child[d - 1] + el else P.top = P.top + el end
			if not r[1] then error(r[2], 0) end
			return unpack(r, 2, r.n)
		end
	end
	local names = {}
	for k, v in pairs(Deagle) do if type(v) == "function" and not PROF_SKIP[k] then names[#names + 1] = k end end
	for _, k in ipairs(names) do wrap(Deagle, k, k, false) end
	for _, tn in ipairs(PROF_ENGINE) do
		local t = _G[tn]
		if type(t) == "table" then
			local fns = {}
			for k, v in pairs(t) do if type(v) == "function" and not PROF_SKIP[k] then fns[#fns + 1] = k end end
			for _, k in ipairs(fns) do wrap(t, k, tn .. "." .. k, true) end
		end
	end
	log(string.format("PROF on: %d functions timed", #P.orig))
end
-- switch one part of a hit or a shot off for a phase (X.offOrig keeps what to put back)
local function stressOff(X, what)
	if X.offOrig then
		for i = #X.offOrig, 1, -1 do local e = X.offOrig[i]; _G[e[1]][e[2]] = e[3] end
		X.offOrig = nil
	end
	if not what or not STRESS_PARTS[what] then return end
	local o = {}
	for _, e in ipairs(STRESS_PARTS[what]) do
		local t = _G[e[1]]
		local orig, f = t[e[2]], e[3]
		if type(f) == "table" then f = f.wrap(orig) end
		o[#o + 1] = { e[1], e[2], orig }
		t[e[2]] = f
	end
	X.offOrig = o
end
local STRESS_LUA = { "VmTick", "VmPlace", "RoundsTick", "Fire", "EnvTick", "MotionTick", "DrawHud" }
local MOD_AVG, MOD_P99 = 1.0, 2.0   -- ms of our Lua a frame (docs/PERFORMANCE.md)
local function pct(sorted, q) return sorted[math.max(1, math.min(#sorted, math.ceil(#sorted * q)))] or 0 end
-- "aim" holds fire while anything living is near the line of fire: the view's first hit is a person, an animal or
-- chickens, or a living body is within 8 m of the player or 4 m of the aim point (rounds spread around the crosshair:
-- one centre ray let a bandit at 1.4 m take five rounds, 2026-10-15)
local function aimsAtLife(self)
	local hits, p = {}, self:Player()
	local eye, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
	local ok, n = pcall(Physics.RayWorldIntersection, eye, vscale(dir, 300), 1, ent_all, p and p.id, nil, hits)
	local h = ok and n and n > 0 and hits[1]
	local e = h and h.entity
	if e and (e.actor or e.soul or e.OnBoidHit or (type(e.class) == "string" and e.class:find("Boid"))) then return true end
	local aim = h and h.pos or vadd(eye, vscale(dir, 300))
	for _, q in ipairs({ { eye, 8 }, { aim, 4 } }) do
		local okS, found = pcall(Physics.SamplePhysEnvironment, q[1], q[2], ent_living)
		if okS and type(found) == "table" then
			for i = 1, #found do
				local f = found[i]
				if type(f) == "table" and f ~= p and (f.actor or f.soul) then return true end
			end
		end
	end
	return false
end
-- puts everything the run wrapped back (deagle_stress stop, a frame-time abort, the end of the run)
function Deagle:StressStop(why)
	local X = self.stress
	if not X then return end
	stressProf(X, false)   -- first: it puts back the stress's own wrappers, which X.orig then replaces
	for k, f in pairs(X.orig) do
		if X.mine and self[k] ~= X.mine[k] then log("STRESS: Deagle." .. k .. " was wrapped again during the run; put back to what it was before the run") end
		self[k] = f
	end
	stressOff(X, nil)
	self.cfg.tracers, self.stress, self.shotDirOverride = X.tracersWas, nil, nil
	log("STRESS " .. why)
	self:Notify("stress test " .. why)
end
function Deagle:Stress(arg)
	local words = {}
	for w in tostring(Deagle.Arg(arg) or ""):gmatch("%S+") do words[#words + 1] = w end
	if words[1] == "stop" then return self:StressStop("stopped") end
	if self.stress then return end
	local spawner, spawnName
	if words[1] == "spawn" then
		spawnName = words[2] or ""
		spawner = Deagle.StressSpawners[spawnName]
		if not spawner then
			local names = {}
			for k in pairs(Deagle.StressSpawners) do names[#names + 1] = k end
			table.sort(names)
			log("$4STRESS: no spawner '" .. spawnName .. "' (registered: " .. table.concat(names, ", ") .. ")")
			return
		end
		table.remove(words, 1); table.remove(words, 1)
	end
	-- several rates for the profile (deagle_lua Deagle:Stress('75,250,500 sky prof')): one profiled fire phase each, so
	-- the cost per shot (grows with the rate) and per round in flight (~64 at any rate past ~20 a second: the round
	-- cap) come apart in one run (Claude 5's ask, 2026-10-17)
	local rates = {}
	for r in tostring(words[1] or ""):gmatch("[^,]+") do if tonumber(r) then rates[#rates + 1] = math.max(1, math.min(2000, tonumber(r))) end end
	local rps = rates[1] or (spawner and 30 or 10)
	-- shots a frame at most: 0.1 s worth, so a hitch can't dump a burst (max(8, rps/30) set the rate itself under ~28 fps:
	-- 250 a second at 22 fps fired ~200, Claude 1 / Claude 5 2026-10-17)
	local function frameCap(rate) return math.max(1, math.ceil(rate * 0.1)) end
	local perFrame = frameCap(rps)
	local mode = spawner and "spawn" or (words[2] == "aim" and "aim" or "sky")
	local phases = spawner and STRESS_SPAWN or (words[3] == "ab" and STRESS_AB or words[3] == "shot" and STRESS_SHOT
		or words[3] == "prof" and STRESS_PROF or STRESS)
	if phases == STRESS_PROF and #rates > 1 then
		phases = { STRESS_PROF[1] }
		for _, r in ipairs(rates) do
			phases[#phases + 1] = { "FIRE profiled " .. r, 4, true, true, prof = true, rate = r }
			phases[#phases + 1] = { "rest", 2, false, true }
		end
	end
	-- a fourth word picks the shot A/B's parts: deagle_stress 250 sky shot casebody,portsmoke = everything, then
	-- without each of those
	local shotAB = phases == STRESS_SHOT
	if shotAB and words[4] then
		phases = { STRESS_SHOT[1], STRESS_SHOT[2], STRESS_SHOT[3] }
		for name in words[4]:gmatch("[^,]+") do
			if name == "tracers" or STRESS_PARTS[name] then
				phases[#phases + 1] = { "FIRE no " .. name, 3, true, name ~= "tracers", name ~= "tracers" and name or nil }
				phases[#phases + 1] = { "rest", 2, false, true }
			else log("$4STRESS: no part '" .. name .. "' (tracers, flash, casings, casebody, portsmoke, gunsound, hearing, anim, shot)") end
		end
	end
	local clock = System.GetCurrAsyncTime
	local X = { ph = 1, tracersWas = self.cfg.tracers, lua = 0, frameLua = 0, depth = 0, pieces = 0, impacts = 0, fx = 0, shots = 0, launched = 0, held = 0, phShots = 0, phLaunched = 0, phHeld = 0, settleMs = 40,
		orig = {}, frames = {}, luas = {} }
	self.stress = X
	for _, k in ipairs(STRESS_LUA) do
		local f = self[k]
		X.orig[k] = f
		self[k] = function(...)
			X.depth = X.depth + 1
			local a = clock()
			local r = { f(...) }
			X.depth = X.depth - 1
			if X.depth == 0 then local d = clock() - a; X.lua = X.lua + d; X.frameLua = X.frameLua + d end
			return unpack(r)
		end
	end
	X.orig.Native = self.Native
	self.Native = function(s, cmd, ...)
		if type(cmd) == "string" then
			local k = cmd:sub(1, 4)
			if k == "fxr " then X.pieces = X.pieces + 1 elseif k == "fxs " then X.fx = X.fx + 1 end
		end
		return X.orig.Native(s, cmd, ...)
	end
	if mode == "sky" then
		-- straight up within 5 deg, whatever the view: "sky" used to read the view, so it depended on the scene (Claude
		-- 1, 2026-10-17: ~190 impacts a second from something overhead until the view moved). A cone around +Z.
		local tan5 = math.tan(math.rad(5))
		self.shotDirOverride = function()
			local k, a = tan5 * math.sqrt(math.random()), math.random() * 2 * math.pi
			local x, y = k * math.cos(a), k * math.sin(a)
			local l = math.sqrt(x * x + y * y + 1)
			return { x = x / l, y = y / l, z = 1 / l }
		end
		-- a tree or a roof over Henry breaks straight up: say what is above
		local p, hits = self:Player(), {}
		local okR, nR = pcall(Physics.RayWorldIntersection, System.GetViewCameraPos(), { x = 0, y = 0, z = 1000 }, 1, ent_all, p and p.id, nil, hits)
		local h = okR and nR and nR > 0 and hits[1]
		log(h and string.format("STRESS clear above: no, first hit at %.1f m (%s%s)", h.dist or -1, tostring(U.surfaceName and U.surfaceName(h) or h.surface),
			h.entity and (", " .. tostring(h.entity.class)) or ", static") or "STRESS clear above: yes (nothing within 1000 m straight up)")
	end
	if mode == "aim" then   -- no kick: at 75 rounds a second it climbed the view straight into the sky (the user)
		X.orig.Recoil = self.Recoil
		self.Recoil = function() end
	end
	X.orig.ApplyHit = self.ApplyHit
	self.ApplyHit = function(s, hit, dir, phys, r, ...)   -- (foliage passes too: impacts aren't ends)
		X.impacts = X.impacts + 1
		if r and hit then r.stressSurf = U.surfaceName and U.surfaceName(hit) or tostring(hit.surface) end
		return X.orig.ApplyHit(s, hit, dir, phys, r, ...)
	end
	-- how rounds end, per phase (Claude 5's tools/probe/ends.lua tally, in the harness): HIT by the surface that stopped
	-- it, HIT then stopped (on after a hit, then too slow or too many hops: MoveRound's hit branch), END past the range
	-- or at the end of its life (Deagle.ROUND_LIFE), spent (anything else); dropped by the round cap
	X.ends = { n = 0, hit = {}, hits = 0, stopped = 0, range = 0, life = 0, spent = 0, dropped = 0, alive = 0, frames = 0 }
	X.orig.StepRound = self.StepRound
	self.StepRound = function(s, r, dt)
		local done = X.orig.StepRound(s, r, dt)
		if done then
			local E = X.ends
			E.n = E.n + 1
			if r.landed then E.hits = E.hits + 1; local k = r.stressSurf or "?"; E.hit[k] = (E.hit[k] or 0) + 1
			elseif r.dist > s.cfg.range then E.range = E.range + 1
			elseif r.age > (s.ROUND_LIFE or 6) then E.life = E.life + 1
			elseif r.stressSurf then E.stopped = E.stopped + 1   -- went on after a hit (ricochet, pass, wallbang), then too slow / too many hops
			else E.spent = E.spent + 1 end   -- anything else (FlightHit's early stop)
		end
		return done
	end
	-- shots that became rounds: a "pass" can't hide shots the gun refused (Claude 1: 1000 a second fired ~45)
	X.orig.LaunchRound = self.LaunchRound
	self.LaunchRound = function(s, ...)
		X.launched, X.phLaunched = X.launched + 1, X.phLaunched + 1
		local before = s.rounds and #s.rounds or 0
		local r = X.orig.LaunchRound(s, ...)
		local list = s.rounds
		if list and list[#list] == r then X.ends.dropped = X.ends.dropped + math.max(0, before + 1 - #list) end   -- the cap took the oldest
		return r
	end
	local vt = self.VmTick   -- the timed one: the frame clock goes around it
	-- Shots owed, a small number: rate x the game time of each frame. Not a next-shot time added to the game clock: KCD2's
	-- Lua numbers are float32 and the clock is the save's game time (51602 s in the user's, 2026-10-17), where a float
	-- moves in 3.9 ms steps; 1/500 s rounded to one step (256 a second), 1/1000 s to nothing (only the per-frame cap
	-- stopped it). The frame steps themselves add up to the true time.
	local lastA, lastG, owed = nil, nil, 0
	local function phaseEnd(s, ph, secs)
		table.sort(X.frames); table.sort(X.luas)
		-- a fire phase also says how many of the due shots became rounds: "THROTTLED" under 90 % (a held view, a refused shot)
		local fired = ""
		if ph[3] and mode ~= "spawn" then
			local due = (ph.rate or rps) * secs
			fired = string.format(" | shots %d of %.0f due, %d rounds launched, %d frames held%s", X.phShots, due, X.phLaunched, X.phHeld,
				X.phLaunched < 0.9 * due and " THROTTLED" or "")
		end
		X.phShots, X.phLaunched, X.phHeld = 0, 0, 0
		local E = X.ends
		if ph[3] or E.n > 0 then   -- how this phase's rounds ended (a round fired late in a phase ends in the next one)
			local surf = {}
			for k, v in pairs(E.hit) do surf[#surf + 1] = { k, v } end
			table.sort(surf, function(x, y) return x[2] > y[2] end)
			local top = {}
			for i = 1, math.min(4, #surf) do top[#top + 1] = surf[i][1] .. " " .. surf[i][2] end
			log(string.format("STRESS ends %-18s HIT %d (%s), HIT then stopped %d, END range %d, life %d, spent %d, dropped by the cap %d | rounds alive %.0f on average",
				ph[1], E.hits, table.concat(top, ", "), E.stopped, E.range, E.life, E.spent, E.dropped, E.alive / math.max(1, E.frames)))
		end
		X.ends = { n = 0, hit = {}, hits = 0, stopped = 0, range = 0, life = 0, spent = 0, dropped = 0, alive = 0, frames = 0 }
		local sum = 0
		for _, v in ipairs(X.luas) do sum = sum + v end
		local avg, p99 = sum / math.max(1, #X.luas), pct(X.luas, 0.99)
		log(string.format("STRESS == %-18s frames %d: p50 %.1f p95 %.1f p99 %.1f max %.1f ms | mod Lua avg %.2f p99 %.2f ms (budget %.1f / %.1f): %s",
			ph[1], #X.frames, pct(X.frames, 0.5), pct(X.frames, 0.95), pct(X.frames, 0.99), X.frames[#X.frames] or 0, avg, p99,
			MOD_AVG, MOD_P99, (avg <= MOD_AVG and p99 <= MOD_P99) and "PASS" or "OVER") .. fired)
		X.frames, X.luas = {}, {}
	end
	self.VmTick = function(s, ...)
		local a, g = clock(), System.GetCurrTime()
		if lastA and lastG and g > lastG then   -- frames the game ran (not paused in the background)
			local ph = phases[X.ph]
			if X.offFor ~= X.ph then X.offFor = X.ph; stressOff(X, ph[5]); stressProf(X, ph.prof, ph[1]) end
			if X.prof then X.prof.frames = X.prof.frames + 1 end
			X.ends.alive, X.ends.frames = X.ends.alive + (s.rounds and #s.rounds or 0), X.ends.frames + 1
			X.start, X.t0 = X.start or g, X.t0 or g
			X.win = X.win or { t = g, n = 0, sum = 0, max = 0 }
			local w, dt = X.win, a - lastA
			w.n, w.sum, w.max = w.n + 1, w.sum + dt, math.max(w.max, dt)
			X.frames[#X.frames + 1] = dt * 1000
			X.luas[#X.luas + 1] = X.frameLua * 1000
			X.frameLua = 0
			s.cfg.tracers = ph[4]
			local gdt = g - lastG
			if not ph[3] then owed = 0
			elseif mode == "spawn" then
				if not X.spot then   -- where the view hits now (10 m out if nothing), for the whole run
					local eye, dir, hits = System.GetViewCameraPos(), System.GetViewCameraDir(), {}
					local okR, nR = pcall(Physics.RayWorldIntersection, eye, vscale(dir, 30), 1, ent_all, s:Player().id, nil, hits)
					local h = okR and nR and nR > 0 and hits[1]
					X.spot = h and { pos = h.pos, normal = h.normal } or { pos = vadd(eye, vscale(dir, 10)), normal = vscale(dir, -1) }
				end
				owed = owed + rps * gdt
				local n = math.min(math.floor(owed), perFrame)
				local a0 = clock()
				for _ = 1, n do
					X.spawned = (X.spawned or 0) + 1
					pcall(spawner, X.spot.pos, X.spot.normal, X.spawned)
				end
				owed = owed - n
				owed = owed - math.floor(owed)   -- past the cap: the backlog goes (a long frame doesn't dump a burst)
				local d = clock() - a0
				X.lua, X.frameLua = X.lua + d, X.frameLua + d
			elseif mode == "sky" or (mode == "aim" and not aimsAtLife(s)) then
				local rr = ph.rate or rps
				local pf = ph.rate and frameCap(rr) or perFrame
				owed = owed + rr * gdt
				local n = math.min(math.floor(owed), pf)   -- every shot due this frame
				for _ = 1, n do
					s.state.ammo = 7
					pcall(s.Fire, s, true)
				end
				X.shots, X.phShots = X.shots + n, X.phShots + n
				if X.prof then X.prof.shots = X.prof.shots + n end
				owed = owed - n
				owed = owed - math.floor(owed)   -- past the cap: the backlog goes
			elseif ph[3] and mode ~= "spawn" then owed, X.held, X.phHeld = 0, X.held + 1, X.phHeld + 1   -- a fire phase, the view off the sky / on life
			else owed = 0 end
			if g - w.t >= 0.5 then
				local rounds = 0
				for _ in pairs(s.rounds or {}) do rounds = rounds + 1 end
				local fm, lm = w.sum / w.n * 1000, X.lua / w.n * 1000
				log(string.format("STRESS %4.1fs %-18s %5.1f ms (%3.0f fps, worst %4.1f) = Lua %4.2f + engine %5.1f | rounds %2d, tracer cmds %4d, impacts %3d, impact fx %3d, shots %3d (rounds launched %3d), frames held %2d",
					g - X.start, ph[1], fm, 1000 / fm, w.max * 1000, lm, fm - lm, rounds, X.pieces, X.impacts, X.fx, X.shots, X.launched, X.held))
				X.lua, X.pieces, X.impacts, X.fx, X.shots, X.launched, X.held, X.win = 0, 0, 0, 0, 0, 0, 0, nil
				-- frames over 150 ms for a second: stop (1000 rounds a second sat at 4 fps until the user paused); in the
				-- shot A/B only that fire phase ends
				X.slow = fm > 150 and (X.slow or 0) + 1 or 0
				X.lastFm = fm
				local why = nil
				if shotAB and ph[3] and X.slow >= 2 then
					log(string.format("STRESS cliff: %s at %.1f s into the phase, frames over 150 ms for a second", ph[1], g - X.t0))
					X.slow, X.cutSecs, X.t0 = 0, g - X.t0, g - ph[2]
				elseif shotAB and ph[1] == "idle" then
					X.baseSum, X.baseN = (X.baseSum or 0) + fm, (X.baseN or 0) + 1
				elseif shotAB and ph[1] == "rest" and fm > X.settleMs and g - X.t0 >= 10 then
					why = string.format("aborted: frames still over %.0f ms after 10 s of rest", X.settleMs)
				elseif not shotAB and X.slow >= 2 then why = "aborted: frames over 150 ms for a second" end
				if why then
					local vtOrig = vt
					s:StressStop(why)
					return vtOrig(s, ...)
				end
			end
			-- a rest lasts until frames are back near the idle phase's (1.5 x, 40 ms at least)
			X.settleMs = math.max(40, 1.5 * (X.baseSum or 0) / math.max(1, X.baseN or 0))
			local settling = shotAB and ph[1] == "rest" and (X.lastFm or 0) > X.settleMs   -- (10 s: aborted above)
			if g - X.t0 >= ph[2] and not settling then
				phaseEnd(s, ph, X.cutSecs or g - X.t0)
				X.cutSecs = nil
				X.ph, X.t0 = X.ph + 1, g
				if X.ph > #phases then
					s:Native("fxrstat")
					s:StressStop("done")
				end
			end
		end
		lastA, lastG = a, g
		return vt(s, ...)
	end
	-- what the run installed, to tell at the end whether another tool wrapped the same functions meanwhile (put back
	-- anyway; the other tool's own unwrap may then reinstall a wrapper of this run: Claude 5's probe/ticks.lua)
	X.mine = {}
	for k in pairs(X.orig) do X.mine[k] = self[k] end
	if spawner then log(string.format("STRESS start: spawn %s, %d a second at one spot ahead, no gun", spawnName, rps))
	else
		log(string.format("STRESS start: %d rounds a second, %s%s", rps, mode == "aim" and "where you look (not at people or animals)" or "into the sky",
			phases == STRESS_AB and ", impact A/B" or shotAB and ", shot A/B" or phases == STRESS_PROF and ", profiled" or ""))
	end
	local total = 0
	for _, ph in ipairs(phases) do total = total + ph[2] end
	self:Notify(mode == "spawn" and string.format("stress test: %s x%d a second, %d s", spawnName, rps, total)
		or mode == "aim" and string.format("stress test: %d rounds/s where you look, %d s", rps, total)
		or string.format("stress test: %d rounds/s, look at the sky for %d s", rps, total))
end
