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
-- switch one part of a hit off for a phase (X.offOrig keeps what to put back)
local function stressOff(X, what)
	if X.offOrig then
		local o = X.offOrig
		X.offOrig = nil
		if o.fx then Deagle.SpawnFx = o.fx end
		if o.decal then Particle.CreateMatDecal = o.decal end
		if o.sound then Deagle.HitSound = o.sound end
	end
	if not what then return end
	local o = {}
	if what == "fx" then o.fx = Deagle.SpawnFx; Deagle.SpawnFx = noop
	elseif what == "holes" then o.decal = Particle.CreateMatDecal; Particle.CreateMatDecal = noop
	elseif what == "sounds" then o.sound = Deagle.HitSound; Deagle.HitSound = noop end
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
	for k, f in pairs(X.orig) do self[k] = f end
	stressOff(X, nil)
	self.cfg.tracers, self.stress = X.tracersWas, nil
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
	local rps = math.max(1, math.min(2000, tonumber(words[1]) or (spawner and 30 or 10)))
	local perFrame = math.max(8, math.ceil(rps / 30))   -- shots a frame at most (a long frame doesn't dump a burst)
	local mode = spawner and "spawn" or (words[2] == "aim" and "aim" or "sky")
	local phases = spawner and STRESS_SPAWN or (words[3] == "ab" and STRESS_AB or STRESS)
	local clock = System.GetCurrAsyncTime
	local X = { ph = 1, tracersWas = self.cfg.tracers, lua = 0, frameLua = 0, depth = 0, pieces = 0, impacts = 0, fx = 0,
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
	if mode == "aim" then   -- no kick: at 75 rounds a second it climbed the view straight into the sky (the user)
		X.orig.Recoil = self.Recoil
		self.Recoil = function() end
	end
	X.orig.ApplyHit = self.ApplyHit
	self.ApplyHit = function(...) X.impacts = X.impacts + 1 return X.orig.ApplyHit(...) end
	local vt = self.VmTick   -- the timed one: the frame clock goes around it
	local lastA, lastG, nextShot
	local function phaseEnd(s, ph)
		table.sort(X.frames); table.sort(X.luas)
		local sum = 0
		for _, v in ipairs(X.luas) do sum = sum + v end
		local avg, p99 = sum / math.max(1, #X.luas), pct(X.luas, 0.99)
		log(string.format("STRESS == %-18s frames %d: p50 %.1f p95 %.1f p99 %.1f max %.1f ms | mod Lua avg %.2f p99 %.2f ms (budget %.1f / %.1f): %s",
			ph[1], #X.frames, pct(X.frames, 0.5), pct(X.frames, 0.95), pct(X.frames, 0.99), X.frames[#X.frames] or 0, avg, p99,
			MOD_AVG, MOD_P99, (avg <= MOD_AVG and p99 <= MOD_P99) and "PASS" or "OVER"))
		X.frames, X.luas = {}, {}
	end
	self.VmTick = function(s, ...)
		local a, g = clock(), System.GetCurrTime()
		if lastA and lastG and g > lastG then   -- frames the game ran (not paused in the background)
			local ph = phases[X.ph]
			if X.offFor ~= X.ph then X.offFor = X.ph; stressOff(X, ph[5]) end
			X.start, X.t0 = X.start or g, X.t0 or g
			X.win = X.win or { t = g, n = 0, sum = 0, max = 0 }
			local w, dt = X.win, a - lastA
			w.n, w.sum, w.max = w.n + 1, w.sum + dt, math.max(w.max, dt)
			X.frames[#X.frames + 1] = dt * 1000
			X.luas[#X.luas + 1] = X.frameLua * 1000
			X.frameLua = 0
			s.cfg.tracers = ph[4]
			nextShot = nextShot or g
			if not ph[3] then nextShot = g
			elseif mode == "spawn" then
				if not X.spot then   -- where the view hits now (10 m out if nothing), for the whole run
					local eye, dir, hits = System.GetViewCameraPos(), System.GetViewCameraDir(), {}
					local okR, nR = pcall(Physics.RayWorldIntersection, eye, vscale(dir, 30), 1, ent_all, s:Player().id, nil, hits)
					local h = okR and nR and nR > 0 and hits[1]
					X.spot = h and { pos = h.pos, normal = h.normal } or { pos = vadd(eye, vscale(dir, 10)), normal = vscale(dir, -1) }
				end
				local n = 0
				local a0 = clock()
				while g >= nextShot and n < perFrame do
					X.spawned = (X.spawned or 0) + 1
					pcall(spawner, X.spot.pos, X.spot.normal, X.spawned)
					nextShot, n = nextShot + 1 / rps, n + 1
				end
				local d = clock() - a0
				X.lua, X.frameLua = X.lua + d, X.frameLua + d
				if g >= nextShot then nextShot = g end
			elseif (mode == "sky" and System.GetViewCameraDir().z > 0.5) or (mode == "aim" and not aimsAtLife(s)) then
				local n = 0
				while g >= nextShot and n < perFrame do   -- every shot due this frame
					s.state.ammo = 7
					pcall(s.Fire, s, true)
					nextShot, n = nextShot + 1 / rps, n + 1
				end
				if g >= nextShot then nextShot = g end
			else nextShot = g end
			if g - w.t >= 0.5 then
				local rounds = 0
				for _ in pairs(s.rounds or {}) do rounds = rounds + 1 end
				local fm, lm = w.sum / w.n * 1000, X.lua / w.n * 1000
				log(string.format("STRESS %4.1fs %-18s %5.1f ms (%3.0f fps, worst %4.1f) = Lua %4.2f + engine %5.1f | rounds %2d, tracer cmds %4d, impacts %3d, impact fx %3d",
					g - X.start, ph[1], fm, 1000 / fm, w.max * 1000, lm, fm - lm, rounds, X.pieces, X.impacts, X.fx))
				X.lua, X.pieces, X.impacts, X.fx, X.win = 0, 0, 0, 0, nil
				-- frames over 150 ms for a second: stop (1000 rounds a second sat at 4 fps until the user paused)
				X.slow = fm > 150 and (X.slow or 0) + 1 or 0
				if X.slow >= 2 then
					local vtOrig = vt
					s:StressStop("aborted: frames over 150 ms for a second")
					return vtOrig(s, ...)
				end
			end
			if g - X.t0 >= ph[2] then
				phaseEnd(s, ph)
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
	if spawner then log(string.format("STRESS start: spawn %s, %d a second at one spot ahead, no gun", spawnName, rps))
	else
		log(string.format("STRESS start: %d rounds a second, %s%s", rps, mode == "aim" and "where you look (not at people or animals)" or "into the sky",
			phases == STRESS_AB and ", impact A/B" or ""))
	end
	local total = 0
	for _, ph in ipairs(phases) do total = total + ph[2] end
	self:Notify(mode == "spawn" and string.format("stress test: %s x%d a second, %d s", spawnName, rps, total)
		or mode == "aim" and string.format("stress test: %d rounds/s where you look, %d s", rps, total)
		or string.format("stress test: %d rounds/s, look at the sky for %d s", rps, total))
end
