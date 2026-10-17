-- The native round sim (native/sim/sim.cpp) driven from LuaJIT's FFI, for the offline contract (Claude 5, ARCHITECTURE
-- step 6). Two ways in, both run by tests/run_round_contract.lua:
-- * NS.install(Deagle, lib): Deagle:StepRound is replaced by one that moves the round natively and hands every hit to
--   Lua's ApplyHit where MoveRound does (--native). LaunchRound / RoundsTick stay Lua.
-- * NS.dn(Deagle, lib): a table with exactly the plugin's DN.* round binding (ARCHITECTURE step 6), for ballistics.lua's
--   cfg.roundSim = "native" path (--dn). The world (rays, water, living NPCs) is the mock's, called back from C; the
--   tracer draws come out as the plugin's own fxr commands.
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
local function load(libPath, headerPath)
	if not declared then ffi.cdef(cdefs(headerPath or "native/sim/sim.h")); declared = true end
	return ffi.load(libPath)
end
local cp = function(v) return { x = v.x, y = v.y, z = v.z } end

-- the world, as the Lua code asks it: the ray FlightHit casts (the player skipped), waterCross's water lookup, FlyBy's
-- living query and filter. Entities cross the C side as handles (0 = none): handles[h] is the Lua value
local function world(D, L, S)
	local W = { handles = {}, byObj = {} }
	function W.handle(e)
		if e == nil or e == false then return 0 end
		local h = W.byObj[e]
		if not h then h = #W.handles + 1; W.handles[h] = e; W.byObj[e] = h end
		return h
	end
	local HITS = {}
	W.ray = ffi.cast("sim_ray_fn", function(_, from, move, skip, out)
		local p = D:Player()
		HITS[1] = nil
		local ok, n = pcall(Physics.RayWorldIntersection, { x = from.x, y = from.y, z = from.z }, { x = move.x, y = move.y, z = move.z },
			1, ent_all, p and p.id, skip ~= 0 and W.handles[tonumber(skip)] or nil, HITS)
		if not ok then return -1 end
		local h = n and n > 0 and HITS[1]
		if not h then return 0 end
		out.dist = h.dist
		out.pos.x, out.pos.y, out.pos.z = h.pos.x, h.pos.y, h.pos.z
		out.normal.x, out.normal.y, out.normal.z = h.normal.x, h.normal.y, h.normal.z
		out.surface = type(h.surface) == "number" and h.surface or -1
		out.partid = h.partid or -1
		out.entity = W.handle(h.entity)
		return 1
	end)
	local WP = { x = 0, y = 0, z = 0 }
	W.water = ffi.cast("sim_water_fn", function(_, at, level, normal)
		WP.x, WP.y, WP.z = at.x, at.y, at.z
		local ok, lv, n = pcall(CryAction.GetWaterInfo, WP)
		if not ok or type(lv) ~= "number" then return 0 end
		level[0] = lv
		if type(n) == "table" then normal.x, normal.y, normal.z = n.x, n.y, n.z end
		return 1
	end)
	W.living = ffi.cast("sim_living_fn", function(_, c, radius, out, max)
		local ok, found = pcall(Physics.SamplePhysEnvironment, { x = c.x, y = c.y, z = c.z }, radius, ent_living)
		if not ok or type(found) ~= "table" then return 0 end
		local p, n = D:Player(), 0
		for i = 1, #found do
			local e = found[i]
			if n < max and type(e) == "table" and e ~= p and e.soul and e.actor and e.this then
				local okD, dead = pcall(e.actor.IsDead, e.actor)
				local okP, at = pcall(e.GetWorldPos, e)
				if not (okD and dead) and okP and at then
					out[n].entity = W.handle(e)
					out[n].feet.x, out[n].feet.y, out[n].feet.z = at.x, at.y, at.z
					n = n + 1
				end
			end
		end
		return n
	end)
	L.sim_set_world(S, W.ray, W.water, nil)
	L.sim_set_living(S, W.living)
	return W
end

local function passIds(D)
	local pass = {}
	for id = 0, 1023 do
		local s = D.surfaces[D.U.surfaceName({ surface = id })]
		if s and s.fx == "pass" then pass[#pass + 1] = id end
	end
	return pass
end

-- install(Deagle, libPath[, headerPath]): StepRound natively (stats: NS.stats())
function NS.install(D, libPath, headerPath)
	local L = load(libPath, headerPath)
	local cfg = ffi.new("sim_config", { step = 0.05, life = D.ROUND_LIFE, range = D.cfg.range, gravity = 9.81,
		maxHops = D.cfg.impactCap.maxHops, waterSurface = -1, minSpeedOn = 30 })
	local okW, waterId = pcall(System.GetSurfaceTypeIdByName, "mat_water")
	if okW and type(waterId) == "number" then cfg.waterSurface = waterId end
	local S = ffi.gc(L.sim_create(4096, cfg), L.sim_destroy)
	local W = world(D, L, S)
	local pass = passIds(D)
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

	-- StepRound, natively: the same duties after the move (tracer, kill cam, near misses) stay Lua here
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
				partid = e.partid >= 0 and e.partid or nil, entity = e.entity ~= 0 and W.handles[tonumber(e.entity)] or nil,
				dist = e.dist, energy = e.energy }
			r.pos, r.dist, r.age = hit.pos, e.dist, RS.age   -- one table, as MoveRound's r.pos = hit.pos
			r.vel = { x = RS.vel.x, y = RS.vel.y, z = RS.vel.z }   -- its velocity at the hit
			local goesOn = self:ApplyHit(hit, cp(e.dir), r.phys, r)
			if self.KillCamHit then self:KillCamHit(hit, r) end
			V1.x, V1.y, V1.z = r.pos.x, r.pos.y, r.pos.z
			V2.x, V2.y, V2.z = r.vel.x, r.vel.y, r.vel.z
			st = L.sim_answer(S, id, goesOn and 1 or 0, V1, V2, W.handle(r.skip), r.tumble and 1 or 0, 0, 0)
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

	NS.sim, NS.lib, NS.keep = S, L, { W }
	return S
end

-- dn(Deagle, libPath[, headerPath]): the plugin's DN.* round functions over the FFI, same names, arguments and results.
-- The plugin's clock is the game clock: System.GetCurrTime() here
function NS.dn(D, libPath, headerPath)
	local L = load(libPath, headerPath)
	local S, W
	local fxNames, fxIds = {}, {}
	local DN = { TRACE_BASE = 2000000 }
	local KIND = { "hit", "done", "landed", "flyby", "ahead" }
	local V1, V2 = ffi.new("sim_vec"), ffi.new("sim_vec")
	local N = ffi.new("uint32_t[1]")
	local RS = ffi.new("sim_round")
	-- the mock's System.GetEntity knows only spawned entities: the scenes' hand-made NPCs are found by their id too
	local byId = {}
	local getEntity = System.GetEntity
	System.GetEntity = function(id) return getEntity(id) or byId[id] end
	local function entityId(h)
		local e = h ~= 0 and W.handles[tonumber(h)]
		if type(e) ~= "table" then return nil end
		if e.id ~= nil then byId[e.id] = e end
		return e.id
	end
	local function skipHandle(id) return id and W.handle(System.GetEntity(id) and id) or 0 end

	function DN.simInit(t)
		local cfg = ffi.new("sim_config", { step = t.step, life = t.life, range = t.range, gravity = t.gravity or 9.81,
			maxHops = t.maxHops, waterSurface = t.waterSurface or -1, minSpeedOn = t.minSpeedOn, nearMiss = t.nearMiss or 0,
			chest = t.chest or 1.25, flybyGap = t.flybyGap or 0.75, aheadK = t.aheadK or 0, aheadMinDist = t.aheadMinDist or 0 })
		S = ffi.gc(L.sim_create(t.capacity or 4096, cfg), L.sim_destroy)
		W = world(D, L, S)
		local pass = t.pass or {}
		L.sim_set_pass(S, ffi.new("int32_t[?]", math.max(1, #pass), pass), #pass)
		-- the tracer streaks: the plugin restarts the round's own emitter (its fxr command; id = DN.TRACE_BASE + round, clear
		-- of the Lua rounds' trace ids 1..1000000 in the plugin's one emitter table)
		local draw = ffi.cast("sim_draw_fn", function(_, round, fx, count, length, at, dir)
			D:Native(string.format("fxr %d %s %.3f %.3f %.3f %.3f %.3f %.4f %.4f %.4f", DN.TRACE_BASE + round, fxNames[fx] or "?", count, length,
				at.x, at.y, at.z, dir.x, dir.y, dir.z))
		end)
		L.sim_set_draw(S, draw)
		NS.sim, NS.lib, NS.keep = S, L, { W, draw }
		return true
	end
	function DN.simPhys(velocity, dragLength, tumbleDragLength, bullet)
		return L.sim_add_phys(S, ffi.new("sim_phys", { velocity = velocity, dragLength = dragLength,
			tumbleDragLength = tumbleDragLength, bullet = bullet }))
	end
	function DN.fire(px, py, pz, dx, dy, dz, phys, firstDt)
		V1.x, V1.y, V1.z = px, py, pz
		V2.x, V2.y, V2.z = dx, dy, dz
		local id = L.sim_launch(S, V1, V2, phys, System.GetCurrTime(), firstDt)
		if id == 0xFFFFFFFF then return nil, "full" end
		return id
	end
	-- tracer(id, fx name, length, reach, muzzle offset ox oy oz, start, dash, anchor: 1 start, 0 end, 0.5 middle)
	function DN.tracer(id, fx, length, reach, ox, oy, oz, start, dash, anchor)
		local k = fxIds[fx]
		if not k then k = #fxNames + 1; fxNames[k] = fx; fxIds[fx] = k end
		V1.x, V1.y, V1.z = ox or 0, oy or 0, oz or 0
		L.sim_tracer(S, id, k, length, reach, V1, start, dash, anchor)
	end
	function DN.ahead(on) L.sim_set_ahead(S, on and 1 or 0) end
	function DN.advance() L.sim_advance(S, System.GetCurrTime(), 0.1) end
	function DN.events()
		local list = L.sim_events(S, N)
		local out = {}
		for k = 0, N[0] - 1 do
			local e = list[k]
			local h, st = e.hit, e.state
			local ev = { kind = KIND[e.kind], id = e.round, vel = cp(st.vel), dist = st.dist, age = st.age, hops = st.hops }
			if e.kind == 1 or e.kind == 4 or e.kind == 5 then   -- the hit, the near miss or the look-ahead's point
				ev.pos, ev.dir, ev.entity = cp(h.pos), cp(h.dir), entityId(h.entity)
				if e.kind ~= 4 then
					ev.normal, ev.surface = cp(h.normal), h.surface >= 0 and h.surface or nil
					ev.partid = h.partid >= 0 and h.partid or nil
				end
				if e.kind == 1 then ev.dist, ev.energy = h.dist, h.energy
				elseif e.kind == 5 then ev.hitDist = h.dist end
			else ev.pos = cp(st.pos) end
			out[#out + 1] = ev
		end
		L.sim_clear_events(S)
		return out
	end
	function DN.answer(id, goesOn, px, py, pz, vx, vy, vz, skip, tumble, markHit, turned)
		V1.x, V1.y, V1.z = px, py, pz
		V2.x, V2.y, V2.z = vx, vy, vz
		L.sim_answer(S, id, goesOn and 1 or 0, V1, V2, skipHandle(skip), tumble and 1 or 0,
			(markHit == true or markHit == 1) and 1 or 0, turned and 1 or 0)
	end
	function DN.round(id, t)   -- as the plugin: fills t (px .. hops), true; nil when the round is gone
		if L.sim_get(S, id, RS) == 0 then return nil end
		t.px, t.py, t.pz, t.vx, t.vy, t.vz = RS.pos.x, RS.pos.y, RS.pos.z, RS.vel.x, RS.vel.y, RS.vel.z
		t.dist, t.age, t.hops = RS.dist, RS.age, RS.hops
		return true
	end
	return DN
end

function NS.stats()
	local st = ffi.new("sim_stats")
	NS.lib.sim_get_stats(NS.sim, st)
	return { rays = tonumber(st.rays), steps = tonumber(st.steps), hits = tonumber(st.hits) }
end

return NS
