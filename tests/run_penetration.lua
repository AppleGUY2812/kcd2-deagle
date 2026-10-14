-- Wallbang, ricochet, armour and over-penetration (BALLISTICS.md phases 2-3) in the mock game: walls are slabs with a
-- front and a back face (rays hit faces from either side, like the worst case for the far-side search), NPCs are
-- boxes with an inventory (guns/armor.lua items) and soul / actor stubs.
-- usage: luajit tests/run_penetration.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local function vec(x, y, z) return { x = x, y = y, z = z } end
M.surfaceNames[5] = "mat_glass"

-- the world: slabs { y0, y1, surface } across the shot, a ground plane, NPC boxes
local world = { slabs = {}, npcs = {}, ground = nil }
local items = {}
_G.ItemManager = { GetItem = function(id) return items[id] end, GetItemName = function() return "" end }
local nextId = 0
local function newNpc(y, outfit)
	nextId = nextId + 1
	local n = { id = "npc" .. nextId, y = y, soul = {}, actor = {}, human = {}, class = "NPC", damage = 0, stamina = 0, inv = {} }
	n.this = { id = n.id }   -- the script handle a brain message goes to (Deagle:HitAI)
	for _, o in ipairs(outfit or {}) do
		local iid = n.id .. "_" .. #n.inv
		items[iid] = { class = o.guid, health = o.health or 1 }
		n.inv[#n.inv + 1] = iid
	end
	n.inventory = { GetInventoryTable = function() return n.inv end }
	function n:GetName() return self.id end
	function n:GetWorldPos() return vec(self.x or 100, self.y, self.z or 30.3) end   -- a shot at camera height (31.75) is a torso hit
	n.GetBonePos = M.skeleton
	function n:GetDirectionVector() return vec(0, -1, 0) end
	function n.soul:DealDamage(st, hp) n.damage = n.damage + hp; n.stamina = n.stamina + st; n.hits = (n.hits or 0) + 1 end
	function n.soul:AddBuff() end
	function n:GetMass() return self.mass or 80 end
	function n.actor:RagDollize() n.ragdoll = M.t end
	function n:AddImpulse(part, pos, dir, push) n.push = push end
	function n.actor:IsDead() return n.damage >= 100 end
	function n.actor:GetHealth() return 100 - n.damage end
	function n.actor:GetMaxHealth() return 100 end
	function n.actor:AddBlood() end
	world.npcs[#world.npcs + 1] = n
	return n
end
Physics.RayWorldIntersection = function(pos, dir, _, mask, skip1, skip2, hits)
	-- KCD2 crashed on a ray that asked for water (the Trosky tannery pit, 2026-10-14): none may
	if mask and bit.band(mask, 0x200) ~= 0 then error("a ray asked for ent_water (crashed KCD2 in a tannery pit)") end
	local best
	local function try(t, hit) if t >= 0 and t <= 1 and (not best or t < best.t) then hit.t = t; best = hit end end
	for _, sl in ipairs(world.slabs) do
		if dir.y ~= 0 then
			try((sl.y0 - pos.y) / dir.y, { normal = vec(0, -1, 0), surface = sl.surface })
			try((sl.y1 - pos.y) / dir.y, { normal = vec(0, 1, 0), surface = sl.surface })
		end
	end
	if world.ground and dir.z ~= 0 then try((world.ground.z - pos.z) / dir.z, { normal = vec(0, 0, 1), surface = world.ground.surface }) end
	-- boxes { min, max, surface }: a ray from outside hits the face it enters (outward normal); from inside, the face it
	-- leaves by (worst case, like the slabs)
	for _, b in ipairs(world.boxes or {}) do
		local tn, tf, nn, nf, inside = -math.huge, math.huge, nil, nil, true
		for _, ax in ipairs({ "x", "y", "z" }) do
			local d, p0 = dir[ax], pos[ax]
			if d == 0 then
				if p0 < b.min[ax] or p0 > b.max[ax] then inside = false end
			else
				local t1, t2 = (b.min[ax] - p0) / d, (b.max[ax] - p0) / d
				local n1, n2 = vec(0, 0, 0), vec(0, 0, 0)
				n1[ax], n2[ax] = -1, 1
				if t1 > t2 then t1, t2, n1, n2 = t2, t1, n2, n1 end
				if t1 > tn then tn, nn = t1, n1 end
				if t2 < tf then tf, nf = t2, n2 end
			end
		end
		if inside and tn <= tf then
			if tn >= 0 then try(tn, { normal = nn, surface = b.surface }) else try(tf, { normal = nf, surface = b.surface }) end
		end
	end
	for _, n in ipairs(world.npcs) do
		if n.id ~= skip2 and dir.y ~= 0 then
			local t = (n.y - pos.y) / dir.y
			local x, z = pos.x + dir.x * t, pos.z + dir.z * t
			local b = n:GetWorldPos()   -- the living capsule round the body: wider than it
			if math.abs(x - b.x) < 0.45 and z > b.z and z < b.z + 1.95 then try(t, { normal = vec(0, -1, 0), entity = n }) end
		end
	end
	if not best then return 0 end
	best.pos = vec(pos.x + dir.x * best.t, pos.y + dir.y * best.t, pos.z + dir.z * best.t)
	best.dist = best.t * math.sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z)
	hits[1] = best
	return 1
end
local landed = {}
local applyHit = Deagle.ApplyHit
function Deagle:ApplyHit(hit, dir, phys, r)
	landed[#landed + 1] = { hit = hit, surface = M.surfaceNames[hit.surface], entity = hit.entity, energy = hit.energy }
	return applyHit(self, hit, dir, phys, r)
end

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function reset() world.slabs, world.npcs, world.ground, world.boxes, landed, M.decals, M.fxNames, M.waters = {}, {}, nil, {}, {}, {}, {}, {} end
M.surfaceNames[6] = "mat_water"
M.surfaceNames[7] = "mat_metal"
M.surfaceNames[8] = "mat_door_metal"
M.surfaceNames[9] = "mat_road"
M.surfaceNames[10] = "mat_ceramics"
-- math.random() returns v while f runs (ricochet rolls made certain); math.random(a, b) and seeding work as before
local function withRandom(v, f)
	local old = math.random
	math.random = function(...) if select("#", ...) > 0 then return old(...) end return v end
	local ok, err = pcall(f)
	math.random = old
	if not ok then error(err, 0) end
end
local function fire(pitch)
	Deagle.state.ammo, Deagle.state.lastShot = Deagle.gun.magSize, -100
	Deagle.cfg.spreadHip, Deagle.cfg.spreadAim, Deagle.cfg.heatPerShot, Deagle.state.heat = 0, 0, 0, 0
	Deagle.cfg.physics.barrelAim = 0
	Deagle.cfg.accuracy.scale = 0                      -- a straight shot (CS2 accuracy has its own test)
	M.view.pitch, M.view.yaw = pitch or 0, 0
	M.fxNames = {}
	Deagle:Shoot(true)
	M.advance(1.5, 60)
end
-- KCD2 items from guns/armor.lua by name
local function item(name)
	for g, a in pairs(Deagle.armor) do if a.name == name then return { guid = g, a = a } end end
	error("no armour item " .. name)
end
local cuirass = item("Cuirass01_m01_C2")   -- plate, stab 132, torso
print(string.format("-- cuirass %s stab %d; armour punch %d", cuirass.a.name, cuirass.a.stab, Deagle.cfg.armor.punch))

for _, id in ipairs({ "deagle", "glock_17" }) do
	Deagle:UseGun(id)
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
	local big = id == "deagle"

	-- a 3 cm plank wall at 20 m, stone behind at 40 m: through the plank (exit debris + exit hole), on into the stone
	reset()
	world.slabs = { { y0 = 220, y1 = 220.03, surface = 3 }, { y0 = 240, y1 = 241, surface = 1 } }
	fire()
	local plank, stone = landed[1], landed[2]
	local exitHole
	for _, d in ipairs(M.decals) do if d.pos.y > 220.02 and d.pos.y < 220.05 then exitHole = d end end
	check(string.format("%s: through a 3 cm plank (exit fx %s, exit hole %s), then the stone wall behind with %.0f of %.0f J",
		id, tostring((M.fxNames["deagle_impacts.wood_exit"] or 0) > 0), exitHole and string.format("%.3f m", exitHole.size) or "none",
		stone and stone.energy or -1, plank and plank.energy or -1),
		plank and stone and plank.surface == "mat_wood" and stone.surface == "mat_rock" and stone.energy < plank.energy
		and (M.fxNames["deagle_impacts.wood_exit"] or 0) == 1 and exitHole ~= nil and exitHole.size > 0.06, #landed)

	-- 15 cm of timber: the .50 AE gets through (~19 cm of wood), the 9 mm (~10 cm) stops in it
	reset()
	world.slabs = { { y0 = 220, y1 = 220.15, surface = 3 }, { y0 = 240, y1 = 241, surface = 1 } }
	fire()
	check(string.format("%s: 15 cm of timber %s", id, #landed == 2 and "shot through" or "stops it"), (#landed == 2) == big, #landed)

	-- a glass pane modelled with no thickness: through it
	reset()
	world.slabs = { { y0 = 215, y1 = 215, surface = 5 }, { y0 = 240, y1 = 241, surface = 1 } }
	fire()
	check(string.format("%s: through a glass pane (%d hits)", id, #landed), #landed == 2 and landed[1].surface == "mat_glass", #landed)

	-- a shot skimming stone ground (2 deg): it skids off and lands on the wall far ahead (tumbling, it may drop back and
	-- skip again on the way)
	reset()
	world.ground = { z = 31.0, surface = 1 }
	world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
	fire(math.rad(-2))
	local skips = M.fxNames["deagle_impacts.ricochet_stone"] or 0
	check(string.format("%s: skims the ground at 2 deg -> ricochet (%d), then hits the wall", id, skips),
		skips >= 1 and #landed == skips + 1 and landed[#landed].hit.pos.y >= 300 - 1e-6, #landed)

	-- straight into the ground at 45 deg: no ricochet, it stays
	reset()
	world.ground = { z = 31.0, surface = 1 }
	fire(math.rad(-45))
	check(string.format("%s: into the ground at 45 deg: stays (%d hits)", id, #landed), #landed == 1 and not M.fxNames["deagle_impacts.ricochet_stone"], #landed)

	-- stone's critical angle is 12 deg: at 8 deg it skips (98 %), at 20 deg it doesn't (0.03 %); the old Ready or Not
	-- rule skipped 15 % of 20 deg hits and only 37 % of 8 deg ones
	local full = Deagle.cfg.impactVolume * Deagle.cfg.ricochet.volume   -- a round at full speed: Ready or Not's level
	for _, a in ipairs({ { 8, true }, { 20, false } }) do
		reset()
		world.ground = { z = 31.0, surface = 1 }
		world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
		local n0 = #M.native
		withRandom(0.5, function() fire(math.rad(-a[1])) end)
		local whine
		for i = n0 + 1, #M.native do
			whine = whine or M.native[i]:match("^%[DN%] ron3 ([%d%.]+) [^|]*Ricochets")
		end
		check(string.format("%s: stone ground at %d deg: %s (%d hits, whine at x%s)", id, a[1], a[2] and "skips off" or "stays", #landed, tostring(whine)),
			(#landed == 2) == a[2] and (whine ~= nil) == a[2]
			and (not a[2] or (tonumber(whine) <= full + 1e-3 and tonumber(whine) > 0.9 * full)), #landed)
	end

	-- wood skips too below its critical angle (plywood 14-18 deg); Ready or Not's wood never did
	reset()
	world.ground = { z = 31.0, surface = 3 }
	world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
	withRandom(0.5, function() fire(math.rad(-8)) end)
	check(string.format("%s: wooden floor at 8 deg: skips off (%d hits)", id, #landed), #landed == 2, #landed)

	-- water: a skipping round leaves at ~10 deg whatever it came in at (9 mm: 2.5 -> 9.5, 5 -> 12, 8.5 -> 9.5), with
	-- ~10 % of its energy, tumbling; stone sends it off flatter than it came. Climb = the average angle from the skip to
	-- the wall 93 m on (gravity bends it below the 10 it left at; a 25 deg climb off a 6 deg skip was seen in game)
	local function skim(surface, deg)
		reset()
		world.ground = { z = 31.0, surface = surface }
		if surface == 6 then   -- water: a lake whose surface is at 31 m over a stone bed 5 m down
			world.ground = { z = 26.0, surface = 1 }
			M.waters = { { level = 31.0, x0 = -1e4, x1 = 1e4, y0 = -1e4, y1 = 1e4 } }
		end
		world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
		withRandom(0.5, function() fire(math.rad(-deg)) end)
		if #landed ~= 2 then return -1, -1, 1 end
		local a, b = landed[1].hit.pos, landed[2].hit.pos
		return math.deg(math.atan((b.z - a.z) / math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2))), b.z - 31,
			landed[2].energy / landed[1].energy, math.sqrt((b.x - a.x) ^ 2 + (b.y - a.y) ^ 2 + (b.z - a.z) ^ 2)
	end
	local n0 = #M.native
	local climb3, _, kept = skim(6, 3)
	local spang, splash = false, false
	for i = n0 + 1, #M.native do
		spang = spang or M.native[i]:find("Ricochets", 1, true) ~= nil
		splash = splash or M.native[i]:find("Surface=" .. tostring(Deagle.surfaces.mat_water.ron) .. " ", 1, true) ~= nil
	end
	check(string.format("%s: a skip off water splashes without the metal ricochet sound (splash %s, spang %s)", id,
		tostring(splash), tostring(spang)), splash and not spang, spang)
	local climb6 = skim(6, 6)
	local _, zStone = skim(1, 3)
	check(string.format("%s: skims water and climbs ~10 deg whatever the angle in (3 in: %.1f, 6 in: %.1f), stone keeps it low (%.1f m up)",
		id, climb3, climb6, zStone), climb3 > 5 and climb3 < 10 and climb6 > 5 and climb6 < 10 and zStone > 0 and zStone < 6, climb6)
	check(string.format("%s: off water it keeps under 10 %% of its energy by the wall (%.1f %%)", id, kept * 100), kept > 0 and kept < 0.1, kept)

	-- sheet steel (1 mm, 9 mm FMJ: Science & Justice 2023): skips below ~15 deg and leaves at ~6 deg whatever the angle,
	-- never steeper than it came; off steel a round keeps ~75 % of its energy (J Forensic Sci Soc 1994)
	local c10, c4 = skim(8, 10), skim(8, 4)
	check(string.format("%s: sheet steel at 10 deg skips and climbs under 6 deg (%.1f); at 4 deg under 4 (%.1f)", id, c10, c4),
		c10 > 3 and c10 < 6 and c4 > 1 and c4 < 4, c10)
	local _, _, keptSteel, flown = skim(7, 3)
	local air = math.exp(-2 * flown / Deagle:TumbleDragLength(Deagle:ShotPhysics(Deagle.gun)))   -- tumbling the whole way
	check(string.format("%s: off steel it keeps ~75 %% of its energy (%.0f %% at the wall %.0f m on, the air took x%.2f)", id,
		keptSteel * 100, flown, air), math.abs(keptSteel / air - 0.75) < 0.05, keptSteel / air)

	-- measured departures (Diaczuk 2014 CUNY thesis, 9 mm): off 1/2" steel plate and concrete / marble at ~0.46 x the
	-- angle, a 12 deg hit on solid steel skips whole (critical ~15, it was 6); off glazed tile at ~1.4 deg whatever the
	-- angle (JFS 2023); off packed ground as off sand, 2.8 + 0.78 x (4 -> 6) and still at 12 (critical 14)
	local s12, c12 = skim(7, 12), skim(1, 10)
	local t10, t4 = skim(10, 10), skim(10, 4)
	local g4, g12 = skim(9, 4), skim(9, 12)
	local _, _, keptStone, flownS = skim(1, 3)
	local airS = math.exp(-2 * flownS / Deagle:TumbleDragLength(Deagle:ShotPhysics(Deagle.gun)))
	check(string.format("%s: steel at 12 deg skips, climbs %.1f (5.5 off it); stone at 10 climbs %.1f (4.6 off it); tile 10 / 4 in: %.1f / %.1f; ground 4 / 12 in: %.1f / %.1f; stone keeps %.0f %%",
		id, s12, c12, t10, t4, g4, g12, keptStone / airS * 100),
		s12 > 3 and s12 < 5.6 and c12 > 2.5 and c12 < 4.7 and t10 > -0.5 and t10 < 1.5 and t4 > -0.5 and t4 < 1.5
		and g4 > 4 and g4 < 6.2 and g12 > 9 and g12 < 12.3 and math.abs(keptStone / airS - 0.6) < 0.05, s12)

	-- steel hit at 45 deg (far above its ~15 deg critical angle): no bounce, the round stops and breaks up, its fragments
	-- spraying along the face splashAngle deg off it (the user's iron door, in game 2026-10-14: hits at 45-75 deg)
	reset()
	world.ground = { z = 31.0, surface = 7 }
	world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
	M.fxLog = {}
	withRandom(0.5, function() fire(math.rad(-45)) end)
	local spray
	for _, f in ipairs(M.fxLog) do if f.n == Deagle.cfg.armorRicochetFx then spray = f end end
	local sprayUp = spray and math.deg(math.asin(spray.dir.z)) or -1
	check(string.format("%s: steel at 45 deg: no bounce (%d hits), fragments spray along it %.0f deg off the face, onward (%s)", id,
		#landed, sprayUp, spray and string.format("%.2f", spray.dir.y) or "none"),
		#landed == 1 and spray ~= nil and math.abs(sprayUp - Deagle.cfg.armor.splashAngle) < 0.5 and spray.dir.y > 0.9, #landed)

	-- a tumbling round drags like a tumbling fragment: Cd from its shape (Moxnes 2017's fit to McCleskey's fragments:
	-- sphere 0.42, cube 0.64), on a quarter of its surface; point-first it's AIR_CD on its front
	local cube, sphere = Deagle:TumbleCd(1 / 1.5), Deagle:TumbleCd((4 / 3 * math.pi) ^ (2 / 3) / math.pi)
	check(string.format("tumbling drag fit: cube %.3f (0.64), sphere %.3f (0.42)", cube, sphere),
		math.abs(cube - 0.64) < 0.01 and math.abs(sphere - 0.42) < 0.05, cube)
	local phys = Deagle:ShotPhysics(Deagle.gun)
	local tumbleLen = Deagle:TumbleDragLength(phys)
	reset()
	local r = { pos = vec(100, 200, 50), vel = vec(0, 140, 0), phys = phys, tumble = true, dist = 0, age = 0 }
	Deagle:MoveRound(r, 0.02)
	local want = 140 / (1 + 140 * 0.02 / tumbleLen)
	check(string.format("%s: tumbling, %.0f m drag length (point-first %.0f: x%.1f the drag), 140 m/s -> %.2f after 20 ms (%.2f)",
		id, tumbleLen, phys.dragLength, phys.dragLength / tumbleLen, math.abs(r.vel.y), want),
		phys.dragLength / tumbleLen > 2 and phys.dragLength / tumbleLen < 3.2 and math.abs(r.vel.y - want) < 0.01, r.vel.y)

	-- a rock corner: 20 deg down onto a block's top, 2 cm from its back edge (no ricochet: 20 > 12 deg). The .50 AE cuts
	-- the corner and comes out bent away from the rock (up), not straight on; the 9 mm can't get into stone at all
	reset()
	world.boxes = { { min = vec(90, 201, 25), max = vec(110, 202.08, 31.0), surface = 1 } }
	world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
	withRandom(0.5, function() fire(math.rad(-20)) end)
	local straightZ = 31.0 - (300 - 202.06) * math.tan(math.rad(20))
	local zBehind = #landed == 2 and landed[2].hit.pos.z or nil
	check(string.format("%s: clips a rock corner at 20 deg: %s", id, big and string.format("bent up off it (wall hit at %.1f m, straight on would be %.1f m)",
		zBehind or -99, straightZ) or string.format("stops (%d hits)", #landed)),
		big and zBehind and zBehind > straightZ + 8 or (not big and #landed == 1), zBehind)

	-- a plank's faces are parallel: through it dead straight (only the drop over 40 m, a few cm)
	reset()
	world.slabs = { { y0 = 220, y1 = 220.03, surface = 3 }, { y0 = 240, y1 = 241, surface = 1 } }
	fire()
	check(string.format("%s: through a plank square-on: straight on (%.3f m sideways, %.3f m drop)", id,
		#landed == 2 and math.abs(landed[2].hit.pos.x - 100) or -1, #landed == 2 and 31.75 - landed[2].hit.pos.z or -1),
		#landed == 2 and math.abs(landed[2].hit.pos.z - 31.75) < 0.15 and math.abs(landed[2].hit.pos.x - 100) < 0.01, #landed)

	-- worn cuirass (condition 0.5) on a torso: the 9 mm gets through only up close; the .50 AE also at 30 m
	for _, dist in ipairs({ 5, 30 }) do
		reset()
		local guard = newNpc(200 + dist, { { guid = cuirass.guid, health = 0.5 } })
		world.slabs = { { y0 = 190, y1 = 191, surface = 1 } }   -- a wall behind the shooter: a round bounced back hits it
		local l0 = #M.logs
		fire()
		local through = true
		for i = l0 + 1, #M.logs do if M.logs[i]:find("stopped by", 1, true) then through = false end end
		local sparks = (M.fxNames["deagle_impacts.metal"] or 0) > 0
		local want = big or dist == 5
		check(string.format("%s: worn cuirass at %d m: %s (%.1f HP, %.0f stamina), sparks %s, ricochet fx %s", id, dist,
			through and "through" or "stopped: a bruise", guard.damage, guard.stamina, tostring(sparks), tostring(M.fxNames["deagle_impacts.ricochet_metal"])),
			through == want and sparks and guard.stamina > 0 and (through or (guard.damage > 0 and (M.fxNames["deagle_impacts.ricochet_metal"] or 0) == 1)), guard.damage)
		if not through then
			check(string.format("%s: cuirass at %d m stops it square-on: it flattens and sprays along the plate, nothing comes back (%d hits)", id, dist, #landed),
				#landed == 1, #landed)
		end
		local bled = (M.fxNames["deagle_blood.armor_entry"] or 0) + (M.fxNames["deagle_blood.entry"] or 0)
		check(string.format("%s: cuirass at %d m: %s", id, dist, through and "blood through the armour (cloth tuft entry)" or "no blood"),
			through and (M.fxNames["deagle_blood.armor_entry"] or 0) == 1 or (not through and bled == 0), bled)
	end

	-- the same NPC unarmoured: a full wound, no sparks
	reset()
	local peasant = newNpc(230)
	world.slabs = { { y0 = 232, y1 = 233, surface = 1 } }      -- a wall 2 m behind him
	fire()
	check(string.format("%s: no armour at 30 m: %.1f HP, no sparks", id, peasant.damage), peasant.damage > 20 and not M.fxNames["deagle_impacts.metal"], peasant.damage)
	local spray
	for _, d in ipairs(M.decals) do if d.mat:find("blood_spatter_[123]$") and d.pos.y < 232.1 then spray = d end end
	check(string.format("%s: entry + exit blood, spray on the wall behind (%s %.2f m)", id, spray and spray.mat:match("[^/]+$") or "none", spray and spray.size or -1),
		(M.fxNames["deagle_blood.entry"] or 0) == 1 and (M.fxNames["deagle_blood.exit"] or 0) == 1 and spray ~= nil, M.fxNames["deagle_blood.exit"])

	-- through and through: two unarmoured men in a line, 5 m and 8 m; the round goes through the first into the second
	reset()
	local first, second = newNpc(205), newNpc(208)
	fire()
	check(string.format("%s: through the first man (%.0f HP) into the second (%.0f HP)", id, first.damage, second.damage),
		first.hits == 1 and second.hits == 1 and second.damage > 0 and second.damage < first.damage, second.hits)
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
end

-- hit zones from the bones (guns/hitbones.lua on the mock skeleton): head height -> head, 23 cm to the side at the
-- arm -> left arm (the NPC faces the shooter, so its left is the shooter's right), 40 cm out -> past the arm: the round
-- flies on into the wall behind and the man takes nothing
Deagle:UseGun("glock_17"); Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
local function zoneShot(x, z)
	reset()
	local man = newNpc(210)
	man.x, man.z = 100 - x, 31.75 - z
	world.slabs = { { y0 = 220, y1 = 221, surface = 1 } }
	local l0 = #M.logs
	fire()
	local part
	for i = l0 + 1, #M.logs do part = M.logs[i]:match("hit npc%d+ part (%S+)") or part end
	return part, man, landed
end
local part = zoneShot(0, 1.75)
check("head height -> head (" .. tostring(part) .. ")", part == "head", part)
part = zoneShot(0, 1.35)
check("chest height -> torso (" .. tostring(part) .. ")", part == "torso", part)
part = zoneShot(0.235, 1.25)
check("23 cm out at the arm -> arm_left (" .. tostring(part) .. ")", part == "arm_left", part)
local man, hits
part, man, hits = zoneShot(0.40, 1.25)
check(string.format("40 cm out: past the arm, into the wall behind (man %d hits, wall %s)", man.hits or 0, tostring(hits[#hits] and hits[#hits].surface)),
	(man.hits or 0) == 0 and hits[#hits] and hits[#hits].surface == "mat_rock", man.hits)
part = zoneShot(0.10, 0.75)
check("thigh height, 10 cm out -> leg_left (" .. tostring(part) .. ")", part == "leg_left", part)

-- a killing round drops the body at once and shoves it by the round's momentum a moment later
Deagle:UseGun("deagle"); Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
reset()
local victim = newNpc(210)
victim.damage = 90
local t0 = M.t
fire()
check(string.format("a kill: ragdoll at once (%s), shoved %.0f a moment later", tostring(victim.ragdoll and victim.ragdoll - t0 < 0.05), victim.push or -1),
	victim.ragdoll and victim.ragdoll - t0 < 0.05 and victim.push and victim.push > 100, victim.push)
reset()
local alive = newNpc(210)
fire()
check("a wound that doesn't kill: no ragdoll", alive.ragdoll == nil and alive.damage > 0 and alive.damage < 100, alive.ragdoll)
Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)

-- size: a 2.5 kg hare dies to one Glock round; a 500 kg horse takes less than a man (KCD2 gives all 100 HP)
Deagle:UseGun("glock_17"); Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
local function sized(mass)
	reset()
	local a = newNpc(220)
	a.mass = mass
	fire()
	return a.damage
end
local man, hare, horse = sized(80), sized(2.5), sized(500)
check(string.format("Glock torso: man %.0f HP, hare %.0f (dead), horse %.0f", man, hare, horse), hare >= 100 and horse < man and horse > 0, hare)

-- the AI hears every shot (gun_fired on the shooter, louder for the Deagle) and every impact (bullet_hit at the spot);
-- the impact's sound plays from there, late by the distance (20 m: ~0.06 s)
local function shotMult(id)
	Deagle:UseGun(id); Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
	reset()
	world.slabs = { { y0 = 220, y1 = 221, surface = 1 } }   -- 20 m: inside a stone hit's hearing range (40 m)
	M.aiSounds = {}
	local n0 = #M.native
	fire()
	local shot, hit, snd
	for _, a in ipairs(M.aiSounds) do
		if a.kind == 14 then shot = a end
		if a.kind == 16 then hit = a end
	end
	for i = n0 + 1, #M.native do snd = snd or M.native[i]:match("^%[DN%] ron3 [%d%.]+ [%-%d%.]+ 2[12]%d%.%d+ [%-%d%.]+ (event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces|Surface=8) ") end
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
	return shot, hit, snd
end
local gShot, gHit, gSnd = shotMult("glock_17")
local dShot = shotMult("deagle")
check(string.format("AI hears the shots: Glock x%.1f, Deagle x%.1f (louder); the impact x%.2f", gShot and gShot.mult or -1, dShot and dShot.mult or -1, gHit and gHit.mult or -1),
	gShot and dShot and gHit and gShot.wuid ~= nil and dShot.mult > gShot.mult and gHit.pos.y >= 220 - 1e-6, gShot)
check("the stone wall's impact sound (Ready or Not's ConcreteStrong) played from the wall (" .. tostring(gSnd) .. ")", gSnd ~= nil, gSnd)

-- bullet paths (deagle_paths): a shot through a plank into stone leaves flight (yellow), inside (red), on (cyan)
-- pieces and hit crosses, drawn every frame until cleared
M.console("deagle_paths", "1")
reset()
world.slabs = { { y0 = 220, y1 = 220.03, surface = 3 }, { y0 = 240, y1 = 241, surface = 1 } }
fire()
local kinds = {}
for _, l in ipairs(Deagle.paths or {}) do kinds[table.concat(l.c, ",")] = (kinds[table.concat(l.c, ",")] or 0) + 1 end
local l0 = M.lines or 0
M.advance(0.1, 60)
local drawn = (M.lines or 0) - l0
check(string.format("bullet paths: %d pieces (yellow %d, red %d, cyan %d, crosses), %d lines drawn in 6 frames", #(Deagle.paths or {}),
	kinds["1,0.9,0.1"] or 0, kinds["1,0.15,0.1"] or 0, kinds["0.2,0.9,1"] or 0, drawn),
	(kinds["1,0.9,0.1"] or 0) > 0 and (kinds["1,0.15,0.1"] or 0) > 0 and (kinds["0.2,0.9,1"] or 0) > 0 and drawn >= 6 * #Deagle.paths, drawn)
Deagle.pathsToken = {}                               -- a stale token (a dev reload broke the loop): it restarts
M.advance(1.0, 60)
local l1 = M.lines
fire()
M.advance(0.1, 60)
check("bullet paths: a dead draw loop restarts on the next shot", M.lines > l1, M.lines - l1)
M.console("deagle_paths", "clear"); M.advance(0.1, 60)
local afterClear = M.lines
M.advance(0.1, 60)
check("deagle_paths clear (quoted like the game's console): nothing left, nothing drawn", #Deagle.paths == 0 and M.lines == afterClear, #Deagle.paths)
M.console("deagle_paths", "0")
check("deagle_paths 0: off", Deagle.cfg.debugPaths == false, Deagle.cfg.debugPaths)

-- the armour math on its own: punch vs a layer's stab defense, condition, angle, glance
local phys9 = Deagle:ShotPhysics(Deagle.guns.glock_17)
local layers = { { a = cuirass.a, health = 1 } }
local e0 = phys9.energy
check("a new cuirass stops a 9 mm even at the muzzle", (select(2, Deagle:ThroughArmor(layers, phys9, e0, 1))) ~= nil, nil)
layers[1].health = 0.5
check("a worn one lets it through at the muzzle, square-on", Deagle:ThroughArmor(layers, phys9, e0, 1) > 0, nil)
check("but not 20 deg off square-on", (select(2, Deagle:ThroughArmor(layers, phys9, e0, math.cos(math.rad(20))))) ~= nil, nil)
check("a tumbling round (after a ricochet / corner) has less punch: the worn one stops it square-on",
	(select(2, Deagle:ThroughArmor(layers, phys9, e0, 1, true))) ~= nil, nil)
local physD = Deagle:ShotPhysics(Deagle.guns.deagle)
withRandom(0.5, function()
	check("plate turns a hit 3 deg off its face (critical angle 15: 1 mm sheet steel, 9 mm)",
		select(4, Deagle:ThroughArmor(layers, physD, physD.energy, math.sin(math.rad(3)))) == true, nil)
	check("and one 12 deg off it",
		select(4, Deagle:ThroughArmor(layers, physD, physD.energy, math.sin(math.rad(12)))) == true, nil)
	check("but not one 20 deg off it (the old rule always turned those)",
		select(4, Deagle:ThroughArmor(layers, physD, physD.energy, math.sin(math.rad(20)))) ~= true, nil)
end)
-- the chance to skip off: half at the critical angle, nearly all 3 deg flatter, almost none 3 deg steeper, none for
-- materials that never skip
local rc = function(crit, a) return Deagle:RicochetChance(crit, a) end
check(string.format("ricochet chance around a 12 deg critical angle: 9 deg %.2f, 12 deg %.2f, 15 deg %.2f; never-skip %.2f",
	rc(12, 9), rc(12, 12), rc(12, 15), rc(0, 1)),
	rc(12, 9) > 0.9 and math.abs(rc(12, 12) - 0.5) < 1e-9 and rc(12, 15) < 0.1 and rc(0, 1) == 0, nil)
-- a tumbling round gets less far into things: 15 cm of timber stops a tumbling .50 AE that a straight one goes through
local function timber(tumble)
	reset()
	world.slabs = { { y0 = 220, y1 = 220.15, surface = 3 } }
	local r = { pos = vec(100, 220, 31.75), vel = vec(0, physD.velocity, 0), dist = 20, tumble = tumble }
	local hit = { pos = vec(100, 220, 31.75), normal = vec(0, -1, 0), surface = 3, dist = 20, energy = physD.energy }
	local through
	withRandom(0.5, function() through = Deagle:Impact(hit, vec(0, 1, 0), physD, r) end)
	return through
end
check("15 cm of timber: a straight .50 AE goes through, a tumbling one stops", timber(false) == true and timber(true) == false, nil)
-- water surfaces: a round meets a lake's surface, not its bed (ent_all leaves water out: a round once went through and
-- splashed on the bed, up to 5 m down, 2026-10-13), and no ray asks for water (ent_water, 0x200): KCD2 handed a tannery
-- pit's water back with no object and crashed reading it (2026-10-14). The surface comes from CryAction.GetWaterInfo
do
	local rwi, masks = Physics.RayWorldIntersection, {}
	Physics.RayWorldIntersection = function(pos, dir, maxHits, mask, ...)
		masks[#masks + 1] = mask
		return rwi(pos, dir, maxHits, mask, ...)
	end
	local function lake(pitch, x0)   -- a lake with its surface at 30 m over a stone bed 5 m down, from x0 on
		reset()
		world.ground = { z = 25.0, surface = 1 }
		M.waters = { { level = 30.0, x0 = x0 or -1e4, x1 = 1e4, y0 = -1e4, y1 = 1e4 } }
		withRandom(0.99, function() fire(math.rad(pitch)) end)   -- 0.99: no skip, it goes in
		return landed[1]
	end
	local down = lake(-30)
	Physics.RayWorldIntersection = rwi
	local water = false
	for _, m in ipairs(masks) do if bit.band(m, 0x200) ~= 0 then water = true end end
	check(string.format("a round meets a lake's surface (%s at %.2f m), not its bed 5 m down; %d flight rays, none asks for water",
		down and down.surface or "nothing", down and down.hit.pos.z or 0, #masks),
		down and down.surface == "mat_water" and math.abs(down.hit.pos.z - 30) < 1e-3 and down.hit.normal.z == 1 and not water, water)
	-- KCD2's water areas reach above their surface, and dry ground gives the level sea at -1e6: neither is a surface
	reset()
	world.slabs = { { y0 = 230, y1 = 231, surface = 3 } }
	M.waters = { { level = 31.0, x0 = -1e4, x1 = 1e4, y0 = 200, y1 = 1e4 } }   -- the round flies 0.75 m over it
	fire()
	local over = landed[1]
	reset()
	world.slabs = { { y0 = 230, y1 = 231, surface = 3 } }
	fire()
	local dry = landed[1]
	check(string.format("over water and over dry land a round flies on to the wall (%s, %s)", over and over.surface or "nothing",
		dry and dry.surface or "nothing"), over and over.surface == "mat_wood" and dry and dry.surface == "mat_wood", over and over.surface)
end
-- deagle_gunfx 0: no flash, smoke, muzzle light, casing, tracer or gunshot, so the impacts can be watched on their own;
-- the impact itself (effect, hole, sound) is unchanged
do
	local function shot()
		reset()
		world.slabs = { { y0 = 220, y1 = 221, surface = 1 } }
		M.decals = {}
		local n0, e0 = #M.native, M.spawnedCount or 0
		fire()
		local gun, fx = {}, {}
		for name, k in pairs(M.fxNames) do
			if name:find("^deagle%.muzzle") or name:find("^deagle%.port") or name:find("^deagle_tracers") then gun[#gun + 1] = name end
			if name:find("^deagle_impacts%.") then fx[#fx + 1] = name end
		end
		for i = n0 + 1, #M.native do
			local l = M.native[i]
			if l:find("^%[DN%] flashfx") or l:find("^%[DN%] fxs deagle_tracers") or l:find("Fire|") or l:find("casespin") then gun[#gun + 1] = l:sub(1, 40) end
		end
		return gun, fx, #M.decals
	end
	Deagle:UseGun("deagle"); Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
	local gunOn, fxOn, holesOn = shot()
	M.console("deagle_gunfx", "0")
	local gunOff, fxOff, holesOff = shot()
	M.console("deagle_gunfx", "1")
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
	check(string.format("deagle_gunfx 0: gun effects (flash, smoke, casing, tracer, shot) %d -> %d (%s); impact effects %d -> %d, holes %d -> %d",
		#gunOn, #gunOff, table.concat(gunOff, " "), #fxOn, #fxOff, holesOn, holesOff),
		#gunOn > 0 and #gunOff == 0 and #fxOff == #fxOn and #fxOff > 0 and holesOff == holesOn, #gunOff)
end
-- every surface a round can hit has penetration data (only pass-through helpers may lack it)
local missing = {}
for name, s in pairs(Deagle.surfaces) do
	if s.fx ~= "pass" and not Deagle.penetration[name] then missing[#missing + 1] = name end
end
table.sort(missing)
check("every hittable surface has penetration data", #missing == 0, table.concat(missing, ", "))
-- a ricochet's loudness follows the energy the round brings (amplitude ~ its square root): Ready or Not's level at full
-- speed, 10 dB under it slowed to 129 m/s by a water skip, 18 dB under spent at 54 m/s (heard far off in the free camera)
local function db(v) return 20 * math.log10(Deagle:HitLoudness(physD, 0.5 * physD.bullet * v * v)) end
check(string.format("ricochet loudness: Deagle at 442 m/s %.1f dB, 129 m/s %.1f dB, 54 m/s %.1f dB", db(442), db(129), db(54)),
	math.abs(db(442)) < 0.5 and math.abs(db(129) + 10.7) < 0.5 and math.abs(db(54) + 18.3) < 0.5, db(54))
-- a traced shot (cfg.debugShotLog) says why a wall stopped it, or how much it went through
do
	reset()
	Deagle.cfg.debugShotLog = true
	world.slabs = { { y0 = 220, y1 = 220.4, surface = 3 }, { y0 = 260, y1 = 260.03, surface = 3 } }
	local l0 = #M.logs
	fire()
	world.slabs = { { y0 = 220, y1 = 220.03, surface = 3 }, { y0 = 260, y1 = 261, surface = 1 } }
	fire()
	Deagle.cfg.debugShotLog = false
	local stopped, through
	for i = l0 + 1, #M.logs do
		stopped = stopped or M.logs[i]:match("stopped in mat_wood: (no far side within its reach [%d%.]+ cm)")
		through = through or M.logs[i]:match("through mat_wood (3%.0 cm %(could [%d%.]+%))")
	end
	check(string.format("shot log: 40 cm of wood: %s; a 3 cm plank: through %s", tostring(stopped), tostring(through)),
		stopped ~= nil and through ~= nil, stopped)
end
-- the shot log traces a round each frame it moves; a paused game (unfocused, a menu) ticks with dt 0 and logs nothing
do
	local r = { pos = vec(100, 0, 31.75), vel = vec(0, physD.velocity, 0), phys = physD, dist = 0, age = 0, logId = 9001 }
	local l0 = #M.logs
	for _ = 1, 5 do Deagle:StepRound(r, 0) end
	local paused = #M.logs - l0
	Deagle:StepRound(r, 1 / 60)
	local ran = 0
	for i = l0 + 1, #M.logs do if M.logs[i]:find("shot 9001 t=", 1, true) then ran = ran + 1 end end
	check(string.format("shot log: 5 paused frames log %d lines, a moving frame 1", paused), paused == 0 and ran == 1, ran)
end
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("penetration: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
