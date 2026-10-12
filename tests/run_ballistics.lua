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
local world = { wallY = nil, npcY = nil }
local npc = { soul = {}, actor = {}, human = {}, class = "NPC", damage = 0 }
function npc:GetName() return "target" end
function npc:GetWorldPos() return vec(100, world.npcY or 0, 30) end
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
		try(t, { normal = vec(0, -1, 0), surface = 1 })
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
function Deagle:ApplyHit(hit, dir, phys) landed[#landed + 1] = { t = M.t, hit = hit }; return applyHit(self, hit, dir, phys) end

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
	Deagle.cfg.physics.barrelAim = 0                 -- straight along the camera (the kick would tilt it)
	M.view.pitch, M.view.yaw = 0, 0
	M.fxNames = {}
	local lead = Deagle.frameDt   -- the round's first step: the last frame's length
	Deagle:Shoot(true)
	return M.t, lead
end

local tracerLib = io.open("src/Libs/Particles/deagle_tracers.xml"):read("*a")
local genRange = tonumber(io.open("tools/gen_tracers.py"):read("*a"):match("\nRANGE = ([%d.]+)"))
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
	M.advance(2.0, 60)
	check(string.format("%s: a miss flies out of range and stops being moved", id), #landed == 0 and #(Deagle.rounds or {}) == 0 and Deagle.roundsToken == nil, #(Deagle.rounds or {}))

	-- an NPC 5 m away: hit in the same frame as the shot, nothing waits for the next frame
	world.npcY = 205
	npc.damage, npc.hitAt = 0, nil
	t0 = fire()
	check(string.format("%s: NPC at 5 m hit in the frame of the shot (%.1f HP)", id, npc.damage), npc.hitAt == t0 and npc.damage > 0, npc.hitAt)
	world.npcY = nil

	-- two rounds in flight at once both land
	world.wallY = 300
	landed = {}
	fire(); M.advance(0.05, 60); fire()
	M.advance(1.0, 60)
	check(string.format("%s: two rounds in flight both land (%d)", id, #landed), #landed == 2, #landed)

	-- tracer: the effect for this speed, from the muzzle; it exists in the generated library; none when turned off
	landed = {}
	fire()
	local want = Deagle:TracerFx(phys.velocity)
	check(string.format("%s: tracer %s spawned", id, want), (M.fxNames[want] or 0) == 1, M.fxNames[want])
	check(string.format("%s: %s is in deagle_tracers.xml", id, want), tracerLib:find('Name="' .. want:match("%.(v%d+)") .. '"', 1, true) ~= nil, want)
	M.advance(1.0, 60)
	M.console("deagle_tracers")      -- no argument: on
	Deagle.cfg.tracers = false
	fire()
	check(string.format("%s: no tracer when tracers are off", id), (M.fxNames[want] or 0) == 0, M.fxNames[want])
	Deagle.cfg.tracers = true
	M.advance(1.0, 60)
	world.wallY = nil
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
end
check("tracer speeds round to the nearest 25 m/s and stay in 250..1000",
	Deagle:TracerFx(442) == "deagle_tracers.v450" and Deagle:TracerFx(362) == "deagle_tracers.v350" and Deagle:TracerFx(100) == "deagle_tracers.v250" and Deagle:TracerFx(5000) == "deagle_tracers.v1000",
	Deagle:TracerFx(442))
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("ballistics: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
