-- Rounds in flight (BALLISTICS.md phase 1) in the mock game, for every gun with cartridge data: a wall straight
-- ahead and an NPC up close. Checks flight time and drop against a fine reference integration of the same physics,
-- that a slow frame doesn't let a round through the wall, that a close shot lands in the frame it's fired, that
-- rounds stop being moved once they're done, and that every round gets the tracer for its speed (none when off).
-- usage: luajit tests/run_ballistics.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")

local function vec(x, y, z) return { x = x, y = y, z = z } end
-- the world: a wall at y = wallY (none when nil) and an NPC box in front of the camera at y = npcY (none when nil)
local world = { wallY = nil, npcY = nil, wallSurface = 1, barrierY = nil }
local npc = { soul = {}, actor = {}, human = {}, class = "NPC", damage = 0, this = { id = "npcWUID" } }
function npc:GetName() return "target" end
function npc:GetWorldPos() return vec(100, world.npcY or 0, 30.3) end   -- the shot (z 31.75) lands on the torso
npc.GetBonePos = M.skeleton
function npc.actor:RagDollize() end
function npc:AddImpulse() end
function npc:GetDirectionVector() return vec(0, -1, 0) end
function npc.soul:DealDamage(st, hp) npc.damage = npc.damage + hp; npc.hitAt = M.t end
function npc.actor:IsDead() return false end
function npc.actor:GetHealth() return 100 - npc.damage end
function npc.actor:GetMaxHealth() return 100 end
function npc.actor:GetArmor() return 0 end
function npc.actor:GetMaxArmor() return 1 end
function npc.actor:AddBlood() end
function npc.soul:AddBuff() end
Physics.RayWorldIntersection = function(pos, dir, _, _, _, _, hits)
	local best
	local function try(t, hit) if t >= 0 and t <= 1 and (not best or t < best.t) then hit.t = t; best = hit end end
	if world.wallY and dir.y ~= 0 then
		local t = (world.wallY - pos.y) / dir.y
		try(t, { normal = vec(0, -1, 0), surface = world.wallSurface })
	end
	if world.barrierY and dir.y ~= 0 then   -- an invisible window blocker / map barrier (pass-through surfaces)
		try((world.barrierY - pos.y) / dir.y, { normal = vec(0, -1, 0), surface = world.barrierSurface or 2 })
	end
	if world.npcY and dir.y ~= 0 then     -- the NPC's front face: x 99.7..100.3, z 30..31.9
		local t = (world.npcY - pos.y) / dir.y
		local x, z = pos.x + dir.x * t, pos.z + dir.z * t
		if x > 99.7 and x < 100.3 and z > 30 and z < 31.9 then try(t, { normal = vec(0, -1, 0), entity = npc, partid = 1 }) end
	end
	if not best then return 0 end
	best.pos = vec(pos.x + dir.x * best.t, pos.y + dir.y * best.t, pos.z + dir.z * best.t)
	best.dist = best.t * math.sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z)
	hits[1] = best
	return 1
end
local landed = {}
local applyHit = Deagle.ApplyHit
function Deagle:ApplyHit(hit, dir, phys) landed[#landed + 1] = { t = M.t, hit = hit, at = vec(hit.pos.x, hit.pos.y, hit.pos.z) }; return applyHit(self, hit, dir, phys) end

-- reference: the same physics (drag dv/dt = -v|v|/L, gravity) integrated in 10 us steps, straight ahead from the camera
local function reference(phys, y)
	local p, v, t, h = vec(100, 200, 31.75), vec(0, phys.velocity, 0), 0, 1e-5
	while p.y < y do
		local s = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
		v = vec(v.x - v.x * s / phys.dragLength * h, v.y - v.y * s / phys.dragLength * h, v.z - (v.z * s / phys.dragLength + 9.81) * h)
		p = vec(p.x + v.x * h, p.y + v.y * h, p.z + v.z * h)
		t = t + h
	end
	return t, 31.75 - p.z
end

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function fire()
	Deagle.state.ammo = Deagle.gun.magSize
	Deagle.state.lastShot = -100
	Deagle.cfg.spreadHip, Deagle.cfg.spreadAim, Deagle.cfg.heatPerShot, Deagle.state.heat = 0, 0, 0, 0
	Deagle.cfg.physics.barrelAim = 0
	Deagle.cfg.accuracy.scale = 0                      -- a straight shot (CS2 accuracy has its own test)                 -- straight along the camera (the kick would tilt it)
	M.view.pitch, M.view.yaw = 0, 0
	M.fxNames = {}
	local lead = Deagle.frameDt   -- the round's first step: the last frame's length
	Deagle:Shoot(true)
	return M.t, lead
end

local tracerLib = io.open("src/Libs/Particles/deagle_tracers.xml"):read("*a")
local genRange = tonumber(io.open("tools/gen_tracers.py"):read("*a"):match("\nRANGE = ([%d.]+)"))
local genDash = tonumber(io.open("tools/gen_tracers.py"):read("*a"):match("\nDASH = ([%d.]+)"))
check(string.format("tracers are made for cfg.range (%s m, tools/gen_tracers.py %s m)", Deagle.cfg.range, tostring(genRange)), genRange == Deagle.cfg.range, genRange)

for _, id in ipairs({ "deagle", "glock_17" }) do
	Deagle:UseGun(id)
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)     -- drawn: the viewmodel ticks, frame time is known
	local phys = Deagle:ShotPhysics()
	print(string.format("-- %s: %s, %.0f m/s, drag length %.0f m", id, phys.caliber, phys.velocity, phys.dragLength))

	-- 100 m wall at 60 fps: arrives one frame earlier than the round would (the first step covers the frame it's fired
	-- in), give or take a frame; low by the drop; the path length is what Wound sees
	world.wallY, world.npcY = 300, nil
	landed = {}
	local t0, lead = fire()
	M.advance(1.0, 60)
	local tof, drop = reference(phys, 300)
	local l = landed[1]
	local dt = l and (l.t - t0) or -1
	check(string.format("%s: 100 m wall hit after %.3f s (round %.3f s, minus the frame it's fired in)", id, dt, tof),
		l and math.abs(dt - (tof - lead)) <= 1 / 60 + 1e-6, dt)
	local d = l and (31.75 - l.hit.pos.z) or -1
	check(string.format("%s: drop at 100 m %.1f cm (reference %.1f cm)", id, d * 100, drop * 100), l and math.abs(d - drop) < 0.01, d)
	check(string.format("%s: hit distance %.2f m = the path flown", id, l and l.hit.dist or -1), l and math.abs(l.hit.dist - 100) < 0.05, l and l.hit.dist)
	check(string.format("%s: one hit, rounds no longer moved (%d in flight)", id, #(Deagle.rounds or {})),
		#landed == 1 and #(Deagle.rounds or {}) == 0 and Deagle.roundsToken == nil, #landed)

	-- the wall's look: our stone impact, scaled by the round's energy there (cube root against a 9 mm), and a stone hole
	local scale = Deagle:ImpactScale(phys, 100)
	local hole = M.decals and M.decals[#M.decals]
	local stoneScale = M.fxScale and M.fxScale["deagle_impacts.stone"]
	check(string.format("%s: stone wall -> deagle_impacts.stone x%.2f + %s %.3f m", id, stoneScale or -1, hole and hole.mat or "no hole", hole and hole.size or -1),
		(M.fxNames["deagle_impacts.stone"] or 0) == 1 and math.abs((stoneScale or 0) - scale) < 0.02 * scale and hole
		and hole.mat == "Materials/deagle/decals/bullet_hole_stone" and hole.size > 0.85 * Deagle.cfg.holeSize * scale - 1e-6
		and hole.size < 1.15 * Deagle.cfg.holeSize * scale + 1e-6 and hole.life == Deagle.cfg.holeLife, M.fxNames["deagle_impacts.stone"])

	-- invisible blockers in front of the wall (a window blocker, then a map barrier): the round flies through them
	for _, b in ipairs({ 2, 4 }) do
		world.barrierY, world.barrierSurface = 250, b
		landed = {}
		fire()
		M.advance(1.0, 60)
		local h = landed[1]
		check(string.format("%s: flies through %s at 50 m, hits the wall at %.1f m", id, M.surfaceNames[b], h and h.hit.pos.y - 200 or -1),
			#landed == 1 and math.abs(h.hit.pos.y - 300) < 1e-6 and math.abs(h.hit.dist - 100) < 0.05, h and h.hit.pos.y)
	end
	world.barrierY = nil

	-- a wooden wall: wood splinters and a wood hole
	world.wallSurface = 3
	M.decals = {}
	fire()
	M.advance(1.0, 60)
	check(string.format("%s: wood wall -> deagle_impacts.wood + %s", id, M.decals[1] and M.decals[1].mat or "no hole"),
		(M.fxNames["deagle_impacts.wood"] or 0) == 1 and M.decals[1] and M.decals[1].mat == "Materials/deagle/decals/bullet_hole_wood", M.decals[1])
	world.wallSurface = 1

	-- the same wall at 8 fps: long frames are cut into steps, the round can't skip through the wall
	landed = {}
	fire()
	M.advance(1.0, 8)
	l = landed[1]
	d = l and (31.75 - l.hit.pos.z) or -1
	check(string.format("%s: at 8 fps it still hits the wall, drop %.1f cm", id, d * 100), l and math.abs(d - drop) < 0.01 and math.abs(l.hit.pos.y - 300) < 1e-6, d)

	-- 200 m wall: farther = later and lower than at 100 m
	world.wallY = 400
	landed = {}
	M.advance(0.5, 60)                -- back to 60 fps frames before the shot
	t0, lead = fire()
	M.advance(2.0, 60)
	local tof2, drop2 = reference(phys, 400)
	l = landed[1]
	check(string.format("%s: 200 m wall after %.3f s, drop %.1f cm (reference %.3f s / %.1f cm)", id, l and l.t - t0 or -1, l and (31.75 - l.hit.pos.z) * 100 or -1, tof2, drop2 * 100),
		l and math.abs((l.t - t0) - (tof2 - lead)) <= 1 / 60 + 1e-6 and math.abs((31.75 - l.hit.pos.z) - drop2) < 0.01, l and l.t)

	-- nothing in range: the round is dropped past cfg.range and the per-frame move stops
	world.wallY = nil
	landed = {}
	fire()
	M.advance(7.0, 60)                -- 1000 m takes a Deagle round ~5 s
	check(string.format("%s: a miss flies out of range and stops being moved", id), #landed == 0 and #(Deagle.rounds or {}) == 0 and Deagle.roundsToken == nil, #(Deagle.rounds or {}))

	-- an NPC 5 m away: hit in the same frame as the shot, nothing waits for the next frame
	world.npcY = 205
	npc.damage, npc.hitAt = 0, nil
	t0 = fire()
	check(string.format("%s: NPC at 5 m hit in the frame of the shot (%.1f HP)", id, npc.damage), npc.hitAt == t0 and npc.damage > 0, npc.hitAt)
	-- KCD2's own reaction through the plugin: the game's strength rule (50+ HP = major 6, else minor 5), torso, the
	-- round's direction; none for an NPC the hit left dead
	local react
	for i = #M.native, 1, -1 do if M.native[i]:find("^%[DN%] hitreact ") then react = M.native[i]; break end end
	local victim, attacker, htype, strength, part, px, py, pz, dx, dy, dz, flags = (react or ""):match(
		"^%[DN%] hitreact (0x%x+) (0x%x+) (%d+) (%d+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%x+)$")
	local wantStrength = npc.damage >= 50 and "6" or "5"
	check(string.format("%s: hit reaction sent: %s", id, tostring(react)), victim and htype == "0" and strength == wantStrength and part == "torso"
		and tonumber(py) > 204.7 and tonumber(py) <= 205 and tonumber(dy) > 0.99 and flags == "1f", react)   -- 0x20 (AI) off: hitAI's message
	local nReact = #M.native
	npc.damage = 100                  -- dead
	fire()
	local again = false
	for i = nReact + 1, #M.native do if M.native[i]:find("^%[DN%] hitreact ") then again = true end end
	check(string.format("%s: no reaction on a dead NPC", id), not again, again)
	world.npcY = nil

	-- two rounds in flight at once both land
	world.wallY = 300
	landed = {}
	fire(); M.advance(0.05, 60); fire()
	M.advance(1.0, 60)
	check(string.format("%s: two rounds in flight both land (%d)", id, #landed), #landed == 2, #landed)

	-- tracer, "trail": the 100 m wall shot's path laid end to end with pieces from cfg.tracer.start m past the muzzle
	-- to the hit, joining the round's path from the muzzle; every piece along the flight; none when off
	local tc = Deagle.cfg.tracer
	local shot
	local tracer = Deagle.Tracer
	function Deagle:Tracer(from, dir, ph)
		local t = tracer(self, from, dir, ph)
		shot = { from = from, dir = dir, trail = t, muzzle = self:VmBone(self.gun.bones.muzzle) }
		return t
	end
	local function pieces()
		local out = {}
		local log = {}
		for _, f in ipairs(M.fxLog or {}) do log[#log + 1] = f end
		for i = (M.fxsFrom or 1), #M.native do   -- pieces the plugin spawns: fxs <fx> <scale> <len> <pos> <dir>
			local fx, sc, _, px, py, pz, dx, dy, dz = M.native[i]:match("^%[DN%] fx[sr] [%d ]-(%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+) (%S+)$")
			if fx then log[#log + 1] = { n = fx, scale = tonumber(sc), pos = vec(tonumber(px), tonumber(py), tonumber(pz)),
				dir = vec(tonumber(dx), tonumber(dy), tonumber(dz)), t = M.nativeT and M.nativeT[i] or 0 } end
		end
		for _, f in ipairs(log) do
			if f.n:find("^deagle_tracers%.trail") then
				local l = tc.dash * f.scale
				local back = tc.anchor == "start" and 0 or tc.anchor == "end" and l or l / 2   -- from the effect to the piece's start
				local a = vec(f.pos.x - f.dir.x * back, f.pos.y - f.dir.y * back, f.pos.z - f.dir.z * back)
				out[#out + 1] = { a = a, b = vec(a.x + f.dir.x * l, a.y + f.dir.y * l, a.z + f.dir.z * l), dir = f.dir, t = f.t }
			end
		end
		table.sort(out, function(x, y) return x.t < y.t or (x.t == y.t and x.b.y < y.b.y) end)
		return out
	end
	local function dist(a, b) return math.sqrt((a.x - b.x) ^ 2 + (a.y - b.y) ^ 2 + (a.z - b.z) ^ 2) end
	world.wallY = 300
	local ps, hit
	for _, anchor in ipairs({ "start", "end", "mid" }) do   -- every anchor puts the same streak in the same place
		tc.anchor = anchor
		landed, M.fxLog, M.fxsFrom = {}, {}, #M.native + 1
		fire()
		M.advance(1.0, 60)
		ps, hit = pieces(), landed[1] and landed[1].hit
		local q = ps[#ps]
		check(string.format("%s: tracer anchor %s: %d streaks, the last ends on the hit (%.4f m)", id, anchor, #ps, q and hit and dist(q.b, hit.pos) or -1),
			#ps >= 10 and q and hit and dist(q.b, hit.pos) < 1e-3, #ps)
	end
	-- one short streak a frame (the round glowing), each at most cfg.tracer.length long, each further on than the last
	-- by about a frame's flight, all along the shot
	local longest, along, ahead = 0, true, true
	for i, q in ipairs(ps) do
		longest = math.max(longest, dist(q.a, q.b)); along = along and q.dir.y > 0.99
		if i > 1 then ahead = ahead and q.b.y > ps[i - 1].b.y + (i < #ps and 2 or 0) end   -- the last: part of a frame, to the hit
	end
	local frames = {}
	for _, q in ipairs(ps) do frames[q.t] = (frames[q.t] or 0) + 1 end
	local perFrame = 0
	for _, k in pairs(frames) do perFrame = math.max(perFrame, k) end
	check(string.format("%s: tracer: %d streaks, at most %d a frame, longest %.2f m (cfg %.1f), each a frame further on",
		id, #ps, perFrame, longest, tc.length), perFrame == 1 and longest <= tc.length + 1e-3 and along and ahead, longest)
	-- all of a round's pieces come from one emitter the plugin restarts (fxr <round id>): a new emitter per piece took
	-- 140 -> 70 fps and crashed KCD2 (DX12 descriptor heap, 2026-10-15)
	local ids, nIds = {}, 0
	for i = M.fxsFrom, #M.native do
		local rid = M.native[i]:match("^%[DN%] fxr (%d+) deagle_tracers")
		if rid and not ids[rid] then ids[rid] = true; nIds = nIds + 1 end
	end
	check(string.format("%s: tracer: the round's %d pieces come from %d emitter (fxr round id)", id, #ps, nIds), nIds == 1, nIds)
	local first = ps[1]
	check(string.format("%s: first streak leaves from the muzzle side (%.2f m from it)", id, first and shot and shot.muzzle and dist(first.a, shot.muzzle) or -1),
		first and shot and shot.muzzle and dist(first.a, shot.muzzle) < 8, first)
	local used = {}
	for _, f in ipairs(M.fxLog or {}) do if f.n:find("^deagle_tracers%.trail") then used[f.n] = true end end
	for i = 1, #M.native do local fx = M.native[i]:match("^%[DN%] fx[sr] [%d ]-(deagle_tracers%.%S+) ") if fx then used[fx] = true end end
	local names, allIn = {}, true
	for fx in pairs(used) do names[#names + 1] = fx; allIn = allIn and tracerLib:find('Name="' .. fx:match("%.(.+)$") .. '"', 1, true) ~= nil end
	check(string.format("%s: pieces %s (one frame's glow at 60 fps) are in deagle_tracers.xml, dash %.1f m = tools/gen_tracers.py", id,
		table.concat(names, ","), tc.dash), #names > 0 and allIn and used["deagle_tracers.trail_025"] and tc.dash == genDash, genDash)
	-- the NPC at 5 m: the whole streak is laid in the frame of the shot and ends on the hit
	world.wallY, world.npcY = nil, 205
	npc.damage = 0
	landed, M.fxLog, M.fxsFrom = {}, {}, #M.native + 1
	local t1 = fire()
	ps, hit = pieces(), landed[1] and { pos = landed[1].at }   -- where the round met the body (zones move hit.pos in)
	local now = true
	for _, q in ipairs(ps) do now = now and q.t == t1 end
	check(string.format("%s: NPC at 5 m: %d streak in the frame of the shot, ending on the hit (%.4f m off)", id, #ps,
		hit and ps[#ps] and dist(ps[#ps].b, hit.pos) or -1), #ps == 1 and now and hit and dist(ps[#ps].b, hit.pos) < 1e-3, #ps)
	world.npcY = nil
	M.advance(1.0, 60)
	-- off: nothing; "flying": the speed's own particle once, no pieces
	world.wallY = 300
	M.console("deagle_tracers", "0")
	M.fxLog, M.fxsFrom = {}, #M.native + 1
	fire()
	M.advance(1.0, 60)
	check(string.format("%s: no tracer when tracers are off", id), #pieces() == 0 and not Deagle.cfg.tracers, #pieces())
	M.console("deagle_tracers")      -- no argument: on
	tc.mode = "flying"
	M.fxLog, M.fxsFrom = {}, #M.native + 1
	fire()
	local want = Deagle:TracerFx(phys.velocity)
	M.advance(1.0, 60)
	check(string.format("%s: flying mode: %s once, no trail", id, want), (M.fxNames[want] or 0) == 1 and #pieces() == 0
		and tracerLib:find('Name="' .. want:match("%.(v%d+)") .. '"', 1, true) ~= nil, M.fxNames[want])
	tc.mode = "trail"
	Deagle.Tracer = tracer
	world.wallY = nil
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
end
-- every surface KCD2 has (Libs/MaterialEffects/SurfaceTypes.xml, 1.5.6: 74 entries, mat_corpses_barrier twice) does something defined; every effect / hole
-- named exists in deagle_impacts.xml / Materials/deagle/decals
local kcd2 = [[mat_none mat_canopy mat_vegetation_seethrough mat_window mat_default mat_player_collider mat_player_only
mat_no_photomode mat_npc_only mat_horse_only mat_horse_ignore mat_player_only_with_bark mat_level_barrier
mat_deep_water_barrier mat_cart_slowdown mat_corpses_barrier mat_prepadeni_roadTocamp_barrier mat_flesh mat_leather
mat_chainmail mat_plate mat_bells mat_chainmail_plate mat_fabric_plate mat_fabric_chainmail mat_fabric_chainmail_plate
mat_leather_chainmail mat_thatch mat_plaster mat_wicker mat_ceramics mat_organic_stuff mat_glass mat_soil mat_mud
mat_mudwater mat_rockwater mat_slowwater mat_water mat_rock mat_wood mat_wood_soft mat_grass mat_grassdry mat_forest
mat_gravel mat_metal mat_vegetation mat_super_cool_sparkly_type mat_fabric mat_fabric_items mat_sack mat_invulnerable
mat_shield_wood mat_road mat_field mat_grass_tall mat_bushes mat_metal_seethrough mat_vegetation_horse_ignore
mat_rock_horse_ignore mat_wood_unwalk mat_rock_unwalk mat_soil_unwalk mat_stairs_stone mat_stairs_wood mat_slope
mat_railing_wood_seethrough mat_door_wood mat_door_metal mat_barrier_navignore mat_nodraw_designer mat_raycast]]
local impactsLib = io.open("src/Libs/Particles/deagle_impacts.xml"):read("*a")
local missing, count = {}, 0
for name in kcd2:gmatch("%S+") do
	count = count + 1
	local sfc = Deagle.surfaces[name]
	if not sfc then missing[#missing + 1] = name .. " (not in the table)"
	else
		if sfc.fx ~= "pass" and sfc.fx ~= "blood" and not impactsLib:find('\n\t<Particles Name="' .. sfc.fx .. '"', 1, true) then missing[#missing + 1] = name .. " -> no effect " .. sfc.fx end
		if sfc.hole and not io.open("src/Materials/deagle/decals/bullet_hole_" .. sfc.hole .. ".mtl") then missing[#missing + 1] = name .. " -> no hole " .. sfc.hole end
	end
end
check(string.format("all %d KCD2 surfaces have an impact, a hole or pass through", count), #missing == 0 and count == 73, table.concat(missing, ", "))
-- a dev reload can break the frame loop and leave its token behind: the next round must still fly and land
world.wallY = 300
Deagle:UseGun("deagle"); Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
Deagle.roundsToken, Deagle.rounds = {}, {}          -- a stale token, no loop running
landed = {}
fire()
M.advance(1.0, 60)
check("a dead round loop (stale token) restarts: the round still lands (" .. #landed .. ")", #landed == 1, #landed)
world.wallY = nil
check("tracer speeds round to the nearest 25 m/s and stay in 250..1000",
	Deagle:TracerFx(442) == "deagle_tracers.v450" and Deagle:TracerFx(362) == "deagle_tracers.v350" and Deagle:TracerFx(100) == "deagle_tracers.v250" and Deagle:TracerFx(5000) == "deagle_tracers.v1000",
	Deagle:TracerFx(442))
-- a corpse: the round shoves it (its ragdoll part, by momentum) and it makes RoN's flesh sound (both missing: the user,
-- 2026-10-13 "shooting a corpse doesn't knock back, no sound")
local corpse = { soul = {}, actor = {}, human = {}, class = "NPC", this = { id = "corpseWUID" } }
function corpse:GetName() return "corpse" end
function corpse:GetWorldPos() return vec(100, 205, 30.1) end
corpse.GetBonePos = M.skeleton
function corpse.actor:IsDead() return true end
function corpse.actor:GetHealth() return 0 end
function corpse.actor:AddBlood() end
function corpse:GetDirectionVector() return vec(0, -1, 0) end
function corpse:AddImpulse(part, pos, dir, push) corpse.push = { part = part, push = push } end
local n0 = #M.native
Deagle:ApplyHit({ entity = corpse, pos = vec(100, 205, 30.2), normal = vec(0, -1, 0), partid = 7, dist = 5, surface = 1 }, vec(0, 1, 0), Deagle:ShotPhysics())
local flesh = false
for i = n0 + 1, #M.native do if M.native[i]:find("^%[DN%] ron3 .*Surface=" .. tostring(Deagle.surfaces.mat_flesh.ron) .. " ") then flesh = true end end
check(string.format("a corpse hit shoves the body (part %s, push %s) and sounds like flesh (%s)", tostring(corpse.push and corpse.push.part),
	tostring(corpse.push and corpse.push.push), tostring(flesh)), corpse.push ~= nil and corpse.push.part == 7 and corpse.push.push > 0 and flesh, nil)
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("ballistics: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
