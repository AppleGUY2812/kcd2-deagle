-- The native round sim (native/sim/sim.cpp) driven from LuaJIT's FFI, for the offline contract (Claude 5, ARCHITECTURE
-- step 6): Deagle:StepRound is replaced by one that moves the round natively and hands every hit to Lua's ApplyHit,
-- exactly where MoveRound does, so tests/run_round_contract.lua can fly the same scenes through either sim. LaunchRound
-- and RoundsTick stay Lua (they call StepRound). In the game the plugin's typed binding takes this file's place.
-- usage: local NS = dofile("native/sim/sim_ffi.lua"); NS.install(Deagle, "/path/libdeaglesim.so")
local ffi = require("ffi")
local NS = {}

local function cdefs(path)
	local src = assert(io.open(path)):read("*a")
	local out = {}
	for line in src:gmatch("[^\n]*") do
		if not line:match("^%s*#") and not line:match('^extern "C"') and not line:match("^}%s*$") then out[#out + 1] = line end
	end
	return table.concat(out, "\n")
end
local declared = false

-- install(Deagle, libPath[, headerPath]): returns the sim (stats: NS.stats())
function NS.install(D, libPath, headerPath)
	if os.getenv("DEAGLE_SIM") == "frame" and not NS.framing then   -- the frame API instead (installFrame, below)
		NS.framing = true
		return NS.installFrame(D, libPath, headerPath)
	end
	if not declared then ffi.cdef(cdefs(headerPath or "native/sim/sim.h")); declared = true end
	local L = ffi.load(libPath)
	local U = D.U
	local cfg = ffi.new("sim_config", { step = 0.05, life = D.ROUND_LIFE, range = D.cfg.range, gravity = 9.81,
		maxHops = D.cfg.impactCap.maxHops, waterSurface = -1, minSpeedOn = 30, nearMiss = D.cfg.nearMiss or 0, chest = 1.25,
		flybyGap = 0.75 })
	local okW, waterId = pcall(System.GetSurfaceTypeIdByName, "mat_water")
	if okW and type(waterId) == "number" then cfg.waterSurface = waterId end
	local S = ffi.gc(L.sim_create(4096, cfg), L.sim_destroy)

	-- entities cross the C side as handles: 0 = none
	local handles, byObj = {}, {}
	local function handle(e)
		if e == nil or e == false then return 0 end
		local h = byObj[e]
		if not h then h = #handles + 1; handles[h] = e; byObj[e] = h end
		return h
	end

	-- the world: the same ray FlightHit casts (the player skipped) and the water lookup waterCross asks
	local HITS = {}
	local ray = ffi.cast("sim_ray_fn", function(_, from, move, skip, out)
		local p = D:Player()
		HITS[1] = nil
		local ok, n = pcall(Physics.RayWorldIntersection, { x = from.x, y = from.y, z = from.z }, { x = move.x, y = move.y, z = move.z },
			1, ent_all, p and p.id, skip ~= 0 and handles[tonumber(skip)] or nil, HITS)
		if not ok then return -1 end
		local h = n and n > 0 and HITS[1]
		if not h then return 0 end
		out.dist = h.dist
		out.pos.x, out.pos.y, out.pos.z = h.pos.x, h.pos.y, h.pos.z
		out.normal.x, out.normal.y, out.normal.z = h.normal.x, h.normal.y, h.normal.z
		out.surface = type(h.surface) == "number" and h.surface or -1
		out.partid = h.partid or -1
		out.entity = handle(h.entity)
		return 1
	end)
	local WP = { x = 0, y = 0, z = 0 }
	local water = ffi.cast("sim_water_fn", function(_, at, level, normal)
		WP.x, WP.y, WP.z = at.x, at.y, at.z
		local ok, lv, n = pcall(CryAction.GetWaterInfo, WP)
		if not ok or type(lv) ~= "number" then return 0 end
		level[0] = lv
		if type(n) == "table" then normal.x, normal.y, normal.z = n.x, n.y, n.z end
		return 1
	end)
	L.sim_set_world(S, ray, water, nil)

	-- pass-through surfaces: every id whose name surfaces.lua marks fx = "pass"
	local pass = {}
	for id = 0, 1023 do
		local s = D.surfaces[U.surfaceName({ surface = id })]
		if s and s.fx == "pass" then pass[#pass + 1] = id end
	end
	L.sim_set_pass(S, ffi.new("int32_t[?]", math.max(1, #pass), pass), #pass)

	local physIds = setmetatable({}, { __mode = "k" })
	local function physId(p)
		local id = physIds[p]
		if not id then
			id = L.sim_add_phys(S, ffi.new("sim_phys", { velocity = p.velocity, dragLength = p.dragLength,
				tumbleDragLength = D:TumbleDragLength(p), bullet = p.bullet }))
			physIds[p] = id
		end
		return id
	end

	local V1, V2 = ffi.new("sim_vec"), ffi.new("sim_vec")
	local RS = ffi.new("sim_round")
	local SIM_HIT, SIM_FLYING, SIM_LANDED = 1, 0, 3
	local function sync(r, id)
		L.sim_get(S, id, RS)
		r.pos = { x = RS.pos.x, y = RS.pos.y, z = RS.pos.z }
		r.vel = { x = RS.vel.x, y = RS.vel.y, z = RS.vel.z }
		r.dist, r.age, r.hops = RS.dist, RS.age, RS.hops
	end
	local cp = function(v) return { x = v.x, y = v.y, z = v.z } end

	-- StepRound, natively: the same duties after the move (tracer, kill cam, near misses, the shot log)
	function D:StepRound(r, dt)
		local id = r.nid
		if not id then
			local v = r.vel
			local sp = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
			V1.x, V1.y, V1.z = r.pos.x, r.pos.y, r.pos.z
			V2.x, V2.y, V2.z = v.x / sp, v.y / sp, v.z / sp
			id = L.sim_fire(S, V1, V2, physId(r.phys))
			assert(id ~= 0xFFFFFFFF, "native sim full")
			r.nid = id
		end
		local from = cp(r.pos)
		local st = L.sim_step(S, id, dt)
		while st == SIM_HIT do
			local e = L.sim_pending_hit(S, id)
			L.sim_get(S, id, RS)
			local hit = { pos = cp(e.pos), normal = cp(e.normal), surface = e.surface >= 0 and e.surface or nil,
				partid = e.partid >= 0 and e.partid or nil, entity = e.entity ~= 0 and handles[tonumber(e.entity)] or nil,
				dist = e.dist, energy = e.energy }
			r.pos, r.dist, r.age = hit.pos, e.dist, RS.age   -- one table, as MoveRound's r.pos = hit.pos
			r.vel = { x = RS.vel.x, y = RS.vel.y, z = RS.vel.z }   -- its velocity at the hit
			local dir = cp(e.dir)
			local goesOn = self:ApplyHit(hit, dir, r.phys, r)
			if self.KillCamHit then self:KillCamHit(hit, r) end
			V1.x, V1.y, V1.z = r.pos.x, r.pos.y, r.pos.z
			V2.x, V2.y, V2.z = r.vel.x, r.vel.y, r.vel.z
			st = L.sim_answer(S, id, goesOn and 1 or 0, V1, V2, handle(r.skip), r.tumble and 1 or 0, 0)
			if st == SIM_LANDED then r.landed = true end
		end
		sync(r, id)
		local done = st ~= SIM_FLYING
		self:TracerBullet(r, from)
		if self.KillCamStep then self:KillCamStep(r, done) end
		self:FlyBy(r, from)
		if done then L.sim_free(S, id); r.nid = nil end
		return done
	end

	NS.sim, NS.lib, NS.handles, NS.handle, NS.physId = S, L, handles, handle, physId
	NS.keep = { ray, water }   -- the callbacks live as long as the sim
	return S
end

-- installFrame(Deagle, libPath): the game's path through the frame API instead (sim_launch / sim_advance / events):
-- LaunchRound and RoundsTick are replaced, and Lua touches a round only at its events (a hit, its end). The round's
-- pos / vel / dist / age / hops are read from the sim when asked (a metatable), so nothing is copied per frame. What
-- StepRound also did per round per frame (TracerBullet, KillCamStep, FlyBy) is NOT done here: those move native.
-- This is the shape ballistics.lua's native path takes (its owner's); the contract runs it with DEAGLE_SIM=frame.
function NS.installFrame(D, libPath, headerPath)
	NS.install(D, libPath, headerPath)   -- the world, the pass surfaces, phys ids, handles
	local L, S = NS.lib, NS.sim
	-- who may hear a near miss: npc.lua FlyBy's query and filter (living, not the player, not dead)
	local living = ffi.cast("sim_living_fn", function(_, c, radius, out, max)
		local ok, found = pcall(Physics.SamplePhysEnvironment, { x = c.x, y = c.y, z = c.z }, radius, ent_living)
		if not ok or type(found) ~= "table" then return 0 end
		local p, n = D:Player(), 0
		for i = 1, #found do
			local e = found[i]
			if n < max and type(e) == "table" and e ~= p and e.soul and e.actor and e.this then
				local okD, dead = pcall(e.actor.IsDead, e.actor)
				local okP, at = pcall(e.GetWorldPos, e)
				if not (okD and dead) and okP and at then
					out[n].entity = NS.handle(e)
					out[n].feet.x, out[n].feet.y, out[n].feet.z = at.x, at.y, at.z
					n = n + 1
				end
			end
		end
		return n
	end)
	L.sim_set_living(S, living)
	NS.keep[#NS.keep + 1] = living
	local byId = {}
	local RS = ffi.new("sim_round")
	local LIVE = { pos = true, vel = true, dist = true, age = true, hops = true }
	local live = { __index = function(r, k)
		if not LIVE[k] then return nil end
		local id = rawget(r, "nid")
		if not id or L.sim_get(S, id, RS) == 0 then return nil end
		if k == "pos" then return { x = RS.pos.x, y = RS.pos.y, z = RS.pos.z } end
		if k == "vel" then return { x = RS.vel.x, y = RS.vel.y, z = RS.vel.z } end
		return k == "hops" and RS.hops or RS[k]
	end }
	local function keepFinal(r, st)
		rawset(r, "pos", { x = st.pos.x, y = st.pos.y, z = st.pos.z }); rawset(r, "vel", { x = st.vel.x, y = st.vel.y, z = st.vel.z })
		rawset(r, "dist", st.dist); rawset(r, "age", st.age); rawset(r, "hops", st.hops)
	end
	local V1, V2 = ffi.new("sim_vec"), ffi.new("sim_vec")
	local N = ffi.new("uint32_t[1]")
	local cp = function(v) return { x = v.x, y = v.y, z = v.z } end
	local ended = false
	local function events()
		local k = 0
		while true do
			local list = L.sim_events(S, N)
			if k >= N[0] then break end
			local e = list[k]
			k = k + 1
			local id, r = e.round, byId[e.round]
			if r then
				if e.kind == 1 then   -- a hit: the round as it reaches it, ApplyHit decides
					local h = e.hit
					local hit = { pos = cp(h.pos), normal = cp(h.normal), surface = h.surface >= 0 and h.surface or nil,
						partid = h.partid >= 0 and h.partid or nil, entity = h.entity ~= 0 and NS.handles[tonumber(h.entity)] or nil,
						dist = h.dist, energy = h.energy }
					keepFinal(r, e.state); rawset(r, "pos", hit.pos)
					local goesOn = D:ApplyHit(hit, cp(h.dir), r.phys, r)
					if D.KillCamHit then D:KillCamHit(hit, r) end
					V1.x, V1.y, V1.z = r.pos.x, r.pos.y, r.pos.z
					V2.x, V2.y, V2.z = r.vel.x, r.vel.y, r.vel.z
					local skip, tumble = NS.handle(r.skip), r.tumble and 1 or 0
					for _, f in ipairs({ "pos", "vel", "dist", "age", "hops" }) do rawset(r, f, nil) end   -- live again
					local marked = hit.entity and r.hitIds and r.hitIds[hit.entity.id] and 1 or 0
					L.sim_answer(S, id, goesOn and 1 or 0, V1, V2, skip, tumble, marked)
				elseif e.kind == 4 then   -- a near miss: npc.lua FlyBy's message (geometry and throttle were native)
					local npc = NS.handles[tonumber(e.hit.entity)]
					if npc then D:FlyByMessage(npc, cp(e.hit.pos), cp(e.hit.dir), r) end
				else                  -- its end: landed (a hit stopped it) or done (range, life, hops)
					keepFinal(r, e.state)
					if e.kind == 3 then r.landed = true end
					rawset(r, "nid", nil)
					byId[id] = nil
					ended = true
				end
			end
		end
		L.sim_clear_events(S)
		if ended then   -- Deagle.rounds keeps the live rounds in launch order (the contract reads it)
			local keep = {}
			for _, r in ipairs(D.rounds or {}) do if rawget(r, "nid") then keep[#keep + 1] = r end end
			D.rounds, ended = keep, false
		end
	end
	local function tick(token)
		if token ~= D.roundsToken then return end
		D.roundsTickT = System.GetCurrTime()
		L.sim_advance(S, System.GetCurrTime(), 0.1)
		events()
		if L.sim_alive(S) > 0 then Script.SetTimer(0, function() tick(token) end) else D.roundsToken = nil end
	end
	function D:LaunchRound(pos, dir, phys, trail)
		local r = setmetatable({ phys = phys, trail = trail, t = System.GetCurrTime() }, live)
		V1.x, V1.y, V1.z = pos.x, pos.y, pos.z
		V2.x, V2.y, V2.z = dir.x, dir.y, dir.z
		local id = L.sim_launch(S, V1, V2, NS.physId(phys), System.GetCurrTime(), self.frameDt or 1 / 30)
		assert(id ~= 0xFFFFFFFF, "native sim full")
		rawset(r, "nid", id)
		byId[id] = r
		self.rounds = self.rounds or {}
		self.rounds[#self.rounds + 1] = r
		events()
		if rawget(r, "nid") and (not self.roundsToken or System.GetCurrTime() - (self.roundsTickT or -1e9) > 0.5) then
			local token = {}
			self.roundsToken = token
			Script.SetTimer(0, function() tick(token) end)
		end
		return r
	end
end

function NS.stats()
	local st = ffi.new("sim_stats")
	NS.lib.sim_get_stats(NS.sim, st)
	return { rays = tonumber(st.rays), steps = tonumber(st.steps), hits = tonumber(st.hits) }
end

return NS
