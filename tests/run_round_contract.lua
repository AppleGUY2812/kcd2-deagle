-- The round contract (docs/ARCHITECTURE.md step 6): what any round simulation, today's Lua one or the native one to come,
-- must do. Two kinds of checks:
-- 1. Golden traces: scripted scenes (sky, a stone wall at 300 m, a grazing steel plate, a plank wallbang, a water skip,
--    an NPC, maxHops), each round fired with a fixed seed, recorded as every hit handed to ApplyHit (time, place, normal,
--    surface, path length, energy, direction, goes on) and the round's state every 0.1 s, then compared against
--    tests/golden/round_contract.json. The Lua sim must match it exactly (1e-6); a native sim within NATIVE_TOL (below).
--    A change to the flight on purpose: `luajit tests/run_round_contract.lua --write` and the diff of the golden file
--    shows what moved.
-- 2. Invariants a sim must keep whatever its inside: the same shot at 30 / 60 / 144 fps lands at the same place (the
--    frame rate must not change where a round goes); a long hitch can't push a round through a wall; a round fired in
--    a frame that ends before its first hit lands in that frame (a close shot is instant); the sky round ends by range
--    or life and never by anything else; every hit's path length grows along the path.
-- usage: luajit tests/run_round_contract.lua [--write | --native path/to/libdeaglesim.so]   (from the repo root)
local WRITE, NATIVE = false, nil
for i = 1, #(arg or {}) do
	if arg[i] == "--write" then WRITE = true elseif arg[i] == "--native" then NATIVE = arg[i + 1] end
end
local GOLDEN = "tests/golden/round_contract.json"
-- what a native sim may differ by from the golden traces (float32 positions, its own step split): agreed with Claude 5
local NATIVE_TOL = { pos = 0.01, energyRel = 0.01, t = 1 / 60 + 1e-6 }
local LUA_TOL = 1e-6

local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
M.surfaceNames[5], M.surfaceNames[6] = "mat_metal", "mat_water"
-- the native sim (native/sim, Claude 5) in place of the Lua StepRound, through LuaJIT's FFI; it reads the water and pass
-- surface ids, so it goes in after the names above. ApplyHit, tracers, the kill cam and FlyBy stay Lua
if NATIVE then dofile("native/sim/sim_ffi.lua").install(Deagle, NATIVE); print("native sim: " .. NATIVE) end
local function vec(x, y, z) return { x = x, y = y, z = z } end
local function dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end

-- the world: planes { p = point, n = normal (unit), surface = id, entity, half = { u, v } extents or nil (endless) }
local planes = {}
local npc = { soul = {}, actor = {}, human = {}, class = "NPC", damage = 0, this = { id = "npcWUID" }, id = "npc1" }
function npc:GetName() return "target" end
function npc:GetWorldPos() return vec(0, 50, 0) end
npc.GetBonePos = M.skeleton
function npc.actor:RagDollize() end
function npc:AddImpulse() end
function npc:GetDirectionVector() return vec(0, -1, 0) end
function npc.soul:DealDamage(_, hp) npc.damage = npc.damage + hp end
function npc.actor:IsDead() return npc.damage >= 100 end
function npc.actor:GetHealth() return 100 - npc.damage end
function npc.actor:GetMaxHealth() return 100 end
function npc.actor:GetArmor() return 0 end
function npc.actor:GetMaxArmor() return 1 end
function npc.actor:AddBlood() end
function npc.soul:AddBuff() end
Physics.RayWorldIntersection = function(pos, dir, _, _, skip1, skip2, hits)
	local best
	for _, pl in ipairs(planes) do
		local dn = dot(dir, pl.n)
		local skipped = pl.entity and (pl.entity.id == skip1 or pl.entity.id == skip2)   -- like KCD2: the skip ids aren't hit
		if dn ~= 0 and not skipped then
			local t = dot(vec(pl.p.x - pos.x, pl.p.y - pos.y, pl.p.z - pos.z), pl.n) / dn
			if t >= 0 and t <= 1 and (not best or t < best.t) then
				local hp = vec(pos.x + dir.x * t, pos.y + dir.y * t, pos.z + dir.z * t)
				local inside = true
				if pl.box then   -- an axis box the plane is cut to
					local b = pl.box
					inside = hp.x >= b[1] - 1e-9 and hp.x <= b[2] + 1e-9 and hp.y >= b[3] - 1e-9 and hp.y <= b[4] + 1e-9
						and hp.z >= b[5] - 1e-9 and hp.z <= b[6] + 1e-9
				end
				if inside then
					best = { t = t, pos = hp, normal = pl.n, surface = pl.surface, entity = pl.entity, partid = pl.entity and 1 or nil }
				end
			end
		end
	end
	if not best then return 0 end
	best.dist = best.t * math.sqrt(dot(dir, dir))
	hits[1] = best
	return 1
end
local function wall(y, surface, n)   -- an endless wall across the shot at y, facing back down the shot
	return { p = vec(0, y, 0), n = n or vec(0, -1, 0), surface = surface }
end

-- recording, for both runs (Claude 5's ask): every hit handed to ApplyHit with Lua's answer (goes on, and the round's
-- pos / vel / skip / tumble after it: a native sim replays these instead of running ApplyHit), and the round's place
-- after every frame
local rec
local surfaceName = Deagle.U.surfaceName
local function round6(x) return math.floor(x * 1e6 + 0.5) / 1e6 end
local function snap(r)
	return { pos = vec(r.pos.x, r.pos.y, r.pos.z), vel = vec(r.vel.x, r.vel.y, r.vel.z), dist = r.dist, age = r.age,
		hops = r.hops or 0, tumble = r.tumble and true or false, skip = r.skip or false }
end
local applyHit = Deagle.ApplyHit
function Deagle:ApplyHit(hit, dir, phys, r)
	local h
	if rec and r == rec.r then
		h = { frame = rec.frame, t = M.t - rec.t0, age = r.age, pos = vec(hit.pos.x, hit.pos.y, hit.pos.z),
			normal = vec(hit.normal.x, hit.normal.y, hit.normal.z), surface = surfaceName(hit) or tostring(hit.surface),
			entity = hit.entity and (hit.entity.id or "entity") or false, partid = hit.partid or false,
			dist = hit.dist, energy = hit.energy, dir = vec(dir.x, dir.y, dir.z) }
	end
	local goesOn = applyHit(self, hit, dir, phys, r)
	if h then
		h.goesOn = goesOn and true or false
		h.answer = snap(r)
		rec.hits[#rec.hits + 1] = h
	end
	return goesOn
end

-- one round from (0, 0, 10) along dir, at fps, for up to secs; returns the record
local function fly(gun, dir, fps, secs, seed)
	Deagle:UseGun(gun)
	local phys = Deagle:ShotPhysics()
	Deagle.rounds, Deagle.roundsToken = {}, nil
	M.timers = {}
	math.randomseed(seed)
	Deagle.frameDt = 1 / fps
	local len = math.sqrt(dot(dir, dir))
	local d = vec(dir.x / len, dir.y / len, dir.z / len)
	rec = { hits = {}, samples = {}, t0 = M.t, frame = 0,
		fire = { pos = vec(0, 0, 10), dir = d, gun = gun, seed = seed, fps = fps, phys = { velocity = phys.velocity,
			dragLength = phys.dragLength, tumbleDragLength = Deagle:TumbleDragLength(phys), bullet = phys.bullet, d = phys.d,
			caliber = phys.caliber }, range = Deagle.cfg.range, life = Deagle.ROUND_LIFE, maxHops = Deagle.cfg.impactCap.maxHops } }
	-- frame 0: the step the round makes in the frame it's fired (LaunchRound: one frame's worth)
	local r = Deagle:LaunchRound(vec(0, 0, 10), d, phys, nil)
	rec.r = r
	rec.samples[1] = { frame = 0, t = 0, pos = vec(r.pos.x, r.pos.y, r.pos.z), done = r.landed and true or false }
	local frames = math.floor(secs * fps + 0.5)
	for f = 1, frames do
		rec.frame = f
		M.advance(1 / fps, fps)
		local t = M.t - rec.t0
		local alive = false
		for _, x in ipairs(Deagle.rounds or {}) do if x == r then alive = true end end
		if not alive and not rec.endT then rec.endT = t end
		if not rec.samples[#rec.samples].done then
			rec.samples[#rec.samples + 1] = { frame = f, t = round6(t), pos = vec(r.pos.x, r.pos.y, r.pos.z), done = not alive }
		end
		if rec.endT and t > rec.endT + 0.2 then break end
	end
	rec.final = snap(r)
	rec.landed = r.landed and true or false
	rec.endT = rec.endT and round6(rec.endT) or false
	rec.r, rec.t0, rec.frame = nil, nil, nil
	return rec
end

-- the scenes. Each is fired with the Deagle and the Glock.
local SCENES = {
	{ name = "sky 45 deg", world = {}, dir = vec(0, 1, 1), secs = 7 },
	{ name = "stone wall 300 m", world = { wall(300, 1) }, dir = vec(0, 1, 0), secs = 2 },
	-- a steel wall along the shot at x = 1 (it reaches it 14.3 m out, 4 deg off its face), stone ground at z = 0
	{ name = "steel plate grazed 4 deg, then stone ground", dir = vec(math.sin(math.rad(4)), math.cos(math.rad(4)), 0), secs = 2,
		world = { { p = vec(1, 0, 10), n = vec(-1, 0, 0), surface = 5, box = { 0.5, 1.5, 0, 200, 9, 11 } },
			{ p = vec(0, 0, 0), n = vec(0, 0, 1), surface = 1 } } },
	{ name = "wood plank 2 cm at 30 m, stone wall at 60 m", dir = vec(0, 1, 0), secs = 2,
		world = { wall(30, 3), { p = vec(0, 30.02, 0), n = vec(0, 1, 0), surface = 3 }, wall(60, 1) } },
	{ name = "water skip at 4 deg", dir = vec(0, 1, -math.tan(math.rad(4))), secs = 3, water = { level = 0, x0 = -50, x1 = 50, y0 = 0, y1 = 2000 } },
	-- an invisible window blocker and a map barrier (fx "pass": FlightHit flies on through them in the same step), then stone
	{ name = "window blocker 20 m, map barrier 25 m, stone wall 40 m", dir = vec(0, 1, 0), secs = 1,
		world = { wall(20, 2), wall(25, 4), wall(40, 1) } },
	{ name = "NPC at 50 m", dir = vec(0, 1, 0), secs = 1,
		world = { { p = vec(0, 49.85, 0), n = vec(0, -1, 0), entity = npc, box = { -0.3, 0.3, 49, 51, 8.5, 11 } }, wall(80, 1) } },
}

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end

local function setScene(sc)
	planes = sc.world or {}
	M.waters = sc.water and { sc.water } or {}
	npc.damage = 0
end

-- the scene as data, for the native test to build the same world: planes (point, normal, surface name, entity id,
-- box x0 x1 y0 y1 z0 z1) and water areas
local function sceneData(sc)
	local out = { planes = {}, waters = {} }
	for _, pl in ipairs(sc.world or {}) do
		out.planes[#out.planes + 1] = { p = pl.p, n = pl.n, surface = pl.surface and M.surfaceNames[pl.surface] or false,
			entity = pl.entity and pl.entity.id or false, box = pl.box or false }
	end
	if sc.water then out.waters[1] = sc.water end
	return out
end

-- 1. golden traces
local traces = {}
for _, gun in ipairs({ "deagle", "glock_17" }) do
	Deagle:OnAction("deagle_toggle", "press"); M.advance(0.5)
	for i, sc in ipairs(SCENES) do
		setScene(sc)
		local tr = fly(gun, sc.dir, 60, sc.secs, 1000 + i)
		tr.scene = sceneData(sc)
		traces[gun .. " / " .. sc.name] = tr
	end
end

-- JSON both ways (the golden file is read by this test and by native/sim's C++ test): objects with sorted keys,
-- numbers to 17 digits so a write and a read give back the same double
local function toJson(v, ind)
	ind = ind or ""
	local t = type(v)
	if t == "number" then return string.format("%.17g", v) end
	if t == "boolean" then return tostring(v) end
	if t == "string" then return string.format("%q", v) end
	if #v > 0 or next(v) == nil then
		local out = {}
		for i = 1, #v do out[i] = ind .. "  " .. toJson(v[i], ind .. "  ") end
		return #out == 0 and "[]" or "[\n" .. table.concat(out, ",\n") .. "\n" .. ind .. "]"
	end
	local keys = {}
	for k in pairs(v) do keys[#keys + 1] = k end
	table.sort(keys)
	local out = {}
	for _, k in ipairs(keys) do out[#out + 1] = ind .. "  " .. string.format("%q", k) .. ": " .. toJson(v[k], ind .. "  ") end
	return "{\n" .. table.concat(out, ",\n") .. "\n" .. ind .. "}"
end
local function fromJson(str)
	local i = 1
	local function ws() i = str:find("[^ \t\r\n]", i) or #str + 1 end
	local val
	local function strv()
		local j, out = i + 1, {}
		while true do
			local c = str:sub(j, j)
			if c == '"' then i = j + 1; return table.concat(out) end
			if c == "\\" then j = j + 1; c = str:sub(j, j); out[#out + 1] = c == "n" and "\n" or c else out[#out + 1] = c end
			j = j + 1
		end
	end
	function val()
		ws()
		local c = str:sub(i, i)
		if c == "{" then
			local o = {}; i = i + 1; ws()
			if str:sub(i, i) == "}" then i = i + 1; return o end
			while true do
				ws(); local k = strv(); ws(); i = i + 1   -- the colon
				o[k] = val(); ws()
				local d = str:sub(i, i); i = i + 1
				if d == "}" then return o end
			end
		elseif c == "[" then
			local a = {}; i = i + 1; ws()
			if str:sub(i, i) == "]" then i = i + 1; return a end
			while true do
				a[#a + 1] = val(); ws()
				local d = str:sub(i, i); i = i + 1
				if d == "]" then return a end
			end
		elseif c == '"' then return strv()
		elseif str:sub(i, i + 3) == "true" then i = i + 4; return true
		elseif str:sub(i, i + 4) == "false" then i = i + 5; return false
		else
			local j = str:find("[^%d%.eE%+%-]", i) or #str + 1
			local x = tonumber(str:sub(i, j - 1)); i = j; return x
		end
	end
	return val()
end

if WRITE then
	os.execute("mkdir -p tests/golden")
	local f = assert(io.open(GOLDEN, "w"))
	f:write(toJson(traces) .. "\n")
	f:close()
	print("wrote " .. GOLDEN)
end

local okG, golden = pcall(function() return fromJson(assert(io.open(GOLDEN)):read("*a")) end)
check("golden traces exist (" .. GOLDEN .. ")", okG and type(golden) == "table", golden)
golden = okG and golden or {}

-- the difference between two traces: worst position (m), energy (share), time (s), and what didn't line up
local function compare(a, b)
	local worst = { pos = 0, energy = 0, t = 0 }
	local why
	local function pd(p, q) return math.sqrt((p.x - q.x) ^ 2 + (p.y - q.y) ^ 2 + (p.z - q.z) ^ 2) end
	if #a.hits ~= #b.hits then why = string.format("%d hits vs %d", #a.hits, #b.hits) end
	for i = 1, math.min(#a.hits, #b.hits) do
		local x, y = a.hits[i], b.hits[i]
		worst.pos = math.max(worst.pos, pd(x.pos, y.pos))
		worst.t = math.max(worst.t, math.abs(x.t - y.t))
		worst.energy = math.max(worst.energy, math.abs(x.energy - y.energy) / math.max(1e-9, y.energy))
		if x.surface ~= y.surface or x.entity ~= y.entity or x.goesOn ~= y.goesOn then
			why = why or string.format("hit %d: %s/%s/%s vs %s/%s/%s", i, x.surface, tostring(x.entity), tostring(x.goesOn), y.surface, tostring(y.entity), tostring(y.goesOn))
		end
	end
	if #a.samples ~= #b.samples then why = why or string.format("%d frames vs %d", #a.samples, #b.samples) end
	for i = 1, math.min(#a.samples, #b.samples) do worst.pos = math.max(worst.pos, pd(a.samples[i].pos, b.samples[i].pos)) end
	if a.landed ~= b.landed then why = why or "landed " .. tostring(a.landed) .. " vs " .. tostring(b.landed) end
	if (a.endT == false) ~= (b.endT == false) then why = why or "end " .. tostring(a.endT) .. " vs " .. tostring(b.endT)
	elseif a.endT then worst.t = math.max(worst.t, math.abs(a.endT - b.endT)) end
	return worst, why
end

local names = {}
for k in pairs(traces) do names[#names + 1] = k end
table.sort(names)
for _, k in ipairs(names) do
	local g = golden[k]
	if not g then check(k .. ": in the golden file", false, "missing: --write")
	else
		local w, why = compare(traces[k], g)
		check(string.format("%s: %d hits, %s, matches the golden trace (pos %.1e m, energy %.1e, t %.1e s)", k, #traces[k].hits,
			traces[k].landed and "landed" or "flew out", w.pos, w.energy, w.t),
			not why and w.pos <= LUA_TOL and w.energy <= LUA_TOL and w.t <= LUA_TOL, why)
	end
end

-- what each scene must show at all (so a golden file written from a broken sim can't pass)
local function T(k) return traces[k] or { hits = {}, samples = {} } end
for _, gun in ipairs({ "deagle", "glock_17" }) do
	local sky = T(gun .. " / sky 45 deg")
	check(gun .. ": the sky round hits nothing and ends by range or life", #sky.hits == 0 and sky.endT and not sky.landed
		and (sky.final.dist > Deagle.cfg.range or sky.final.age > Deagle.ROUND_LIFE), sky.final and sky.final.dist)
	local st = T(gun .. " / stone wall 300 m")
	check(gun .. ": the 300 m wall is hit once, in stone, and stops it", #st.hits == 1 and st.hits[1].surface == "mat_rock" and st.landed
		and math.abs(st.hits[1].pos.y - 300) < 1e-6, #st.hits)
	local pl = T(gun .. " / steel plate grazed 4 deg, then stone ground")
	check(gun .. ": the grazed plate turns it (goes on, tumbling) and it lands later", #pl.hits >= 2 and pl.hits[1].surface == "mat_metal"
		and pl.hits[1].goesOn and pl.final.tumble, pl.hits[1] and pl.hits[1].surface)
	local wd = T(gun .. " / wood plank 2 cm at 30 m, stone wall at 60 m")
	check(gun .. ": through the plank, then the wall at 60 m with less energy", #wd.hits == 2 and wd.hits[1].goesOn
		and math.abs(wd.hits[2].pos.y - 60) < 1e-6 and wd.hits[2].energy < wd.hits[1].energy, #wd.hits)
	local wa = T(gun .. " / water skip at 4 deg")
	check(gun .. ": the 4 deg water hit skips (goes on)", wa.hits[1] and wa.hits[1].surface == "mat_water" and wa.hits[1].goesOn,
		wa.hits[1] and wa.hits[1].surface)
	local bl = T(gun .. " / window blocker 20 m, map barrier 25 m, stone wall 40 m")
	check(gun .. ": flies through the blocker and the barrier, hits the 40 m wall", #bl.hits == 1 and bl.hits[1].surface == "mat_rock"
		and math.abs(bl.hits[1].pos.y - 40) < 1e-6, #bl.hits)
	local np = T(gun .. " / NPC at 50 m")
	check(gun .. ": the NPC is hit first, at 49.85 m, once", np.hits[1] and np.hits[1].entity == "npc1" and math.abs(np.hits[1].dist - 49.85) < 0.01
		and not (np.hits[2] and np.hits[2].entity == "npc1"),
		np.hits[1] and np.hits[1].entity)
	for k, tr in pairs(traces) do
		if k:find("^" .. gun) then
			local grows = true
			for i = 2, #tr.hits do if tr.hits[i].dist < tr.hits[i - 1].dist - 1e-9 then grows = false end end
			if not grows then check(k .. ": hit path lengths grow along the path", false, "out of order") end
		end
	end
end

-- 2. invariants
-- 2a. the frame rate doesn't move where a round lands: the 300 m wall and the plank at 30 / 60 / 144 fps
for _, gun in ipairs({ "deagle", "glock_17" }) do
	for _, si in ipairs({ 2, 4 }) do
		local sc = SCENES[si]
		local at = {}
		for _, fps in ipairs({ 30, 60, 144 }) do
			setScene(sc)
			at[fps] = fly(gun, sc.dir, fps, sc.secs, 1000 + si)
		end
		local last = function(t) return t.hits[#t.hits] end
		local a, b, c = last(at[30]), last(at[60]), last(at[144])
		local spread = a and b and c and math.max(math.abs(a.pos.z - b.pos.z), math.abs(c.pos.z - b.pos.z),
			math.abs(a.energy - b.energy) / b.energy, math.abs(c.energy - b.energy) / b.energy) or -1
		check(string.format("%s / %s: lands at the same height and energy at 30 / 60 / 144 fps (worst %.4f)", gun, sc.name, spread),
			a and b and c and #at[30].hits == #at[60].hits and #at[144].hits == #at[60].hits and spread >= 0 and spread < NATIVE_TOL.pos, spread)
	end
end
-- 2b. a long hitch (one 0.5 s frame) can't carry a round through a wall
for _, gun in ipairs({ "deagle", "glock_17" }) do
	setScene({ world = { wall(100, 1) } })
	Deagle:UseGun(gun)
	local phys = Deagle:ShotPhysics()
	Deagle.rounds, Deagle.roundsToken, M.timers = {}, nil, {}
	Deagle.frameDt = 1 / 60
	rec = { hits = {}, samples = {}, t0 = M.t }
	local r = Deagle:LaunchRound(vec(0, 0, 10), vec(0, 1, 0), phys, nil)
	rec.r = r
	M.advance(0.5, 2)
	M.advance(0.5, 60)
	check(gun .. ": a 0.5 s hitch doesn't carry it through the 100 m wall", #rec.hits == 1 and math.abs(rec.hits[1].pos.y - 100) < 1e-6, #rec.hits)
	rec.r = nil
end
-- 2c. a close shot lands in the frame it's fired: a wall at 5 m, no frame advanced
for _, gun in ipairs({ "deagle", "glock_17" }) do
	setScene({ world = { wall(5, 1) } })
	Deagle:UseGun(gun)
	Deagle.rounds, Deagle.roundsToken, M.timers = {}, nil, {}
	Deagle.frameDt = 1 / 60
	rec = { hits = {}, samples = {}, t0 = M.t }
	local r = Deagle:LaunchRound(vec(0, 0, 10), vec(0, 1, 0), Deagle:ShotPhysics(), nil)
	check(gun .. ": a wall at 5 m is hit in the frame the round is fired (no frame advanced)", r.landed and #(Deagle.rounds or {}) == 0, r.landed)
end

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("round contract: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
