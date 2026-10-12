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
	function n.actor:IsDead() return n.damage >= 100 end
	function n.actor:GetHealth() return 100 - n.damage end
	function n.actor:GetMaxHealth() return 100 end
	function n.actor:AddBlood() end
	world.npcs[#world.npcs + 1] = n
	return n
end
Physics.RayWorldIntersection = function(pos, dir, _, _, skip1, skip2, hits)
	local best
	local function try(t, hit) if t >= 0 and t <= 1 and (not best or t < best.t) then hit.t = t; best = hit end end
	for _, sl in ipairs(world.slabs) do
		if dir.y ~= 0 then
			try((sl.y0 - pos.y) / dir.y, { normal = vec(0, -1, 0), surface = sl.surface })
			try((sl.y1 - pos.y) / dir.y, { normal = vec(0, 1, 0), surface = sl.surface })
		end
	end
	if world.ground and dir.z ~= 0 then try((world.ground.z - pos.z) / dir.z, { normal = vec(0, 0, 1), surface = world.ground.surface }) end
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
local function reset() world.slabs, world.npcs, world.ground, landed, M.decals, M.fxNames = {}, {}, nil, {}, {}, {} end
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

	-- a shot skimming stone ground (2 deg): it skids off and lands on the wall far ahead
	reset()
	world.ground = { z = 31.0, surface = 1 }
	world.slabs = { { y0 = 300, y1 = 301, surface = 1 } }
	fire(math.rad(-2))
	check(string.format("%s: skims the ground at 2 deg -> ricochet (%s), then hits the wall", id, tostring(M.fxNames["deagle_impacts.ricochet_stone"])),
		#landed == 2 and (M.fxNames["deagle_impacts.ricochet_stone"] or 0) == 1 and landed[2].hit.pos.y >= 300 - 1e-6, #landed)

	-- straight into the ground at 45 deg: no ricochet, it stays
	reset()
	world.ground = { z = 31.0, surface = 1 }
	fire(math.rad(-45))
	check(string.format("%s: into the ground at 45 deg: stays (%d hits)", id, #landed), #landed == 1 and not M.fxNames["deagle_impacts.ricochet_stone"], #landed)

	-- worn cuirass (condition 0.5) on a torso: the 9 mm gets through only up close; the .50 AE also at 30 m
	for _, dist in ipairs({ 5, 30 }) do
		reset()
		local guard = newNpc(200 + dist, { { guid = cuirass.guid, health = 0.5 } })
		local l0 = #M.logs
		fire()
		local through = true
		for i = l0 + 1, #M.logs do if M.logs[i]:find("stopped by", 1, true) then through = false end end
		local sparks = (M.fxNames["deagle_impacts.metal"] or 0) > 0
		local want = big or dist == 5
		check(string.format("%s: worn cuirass at %d m: %s (%.1f HP, %.0f stamina), sparks %s, ricochet fx %s", id, dist,
			through and "through" or "stopped: a bruise", guard.damage, guard.stamina, tostring(sparks), tostring(M.fxNames["deagle_impacts.ricochet_metal"])),
			through == want and sparks and guard.stamina > 0 and (through or (guard.damage > 0 and (M.fxNames["deagle_impacts.ricochet_metal"] or 0) == 1)), guard.damage)
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

-- the armour math on its own: punch vs a layer's stab defense, condition, angle, glance
local phys9 = Deagle:ShotPhysics(Deagle.guns.glock_17)
local layers = { { a = cuirass.a, health = 1 } }
local e0 = phys9.energy
check("a new cuirass stops a 9 mm even at the muzzle", (select(2, Deagle:ThroughArmor(layers, phys9, e0, 1))) ~= nil, nil)
layers[1].health = 0.5
check("a worn one lets it through at the muzzle, square-on", Deagle:ThroughArmor(layers, phys9, e0, 1) > 0, nil)
check("but not 20 deg off square-on", (select(2, Deagle:ThroughArmor(layers, phys9, e0, math.cos(math.rad(20))))) ~= nil, nil)
check("past 70 deg plate always turns it (glance)", select(4, Deagle:ThroughArmor(layers, Deagle:ShotPhysics(Deagle.guns.deagle), Deagle:ShotPhysics(Deagle.guns.deagle).energy, math.cos(math.rad(75)))) == true, nil)
-- every surface a round can hit has penetration data (only pass-through helpers may lack it)
local missing = {}
for name, s in pairs(Deagle.surfaces) do
	if s.fx ~= "pass" and not Deagle.penetration[name] then missing[#missing + 1] = name end
end
table.sort(missing)
check("every hittable surface has penetration data", #missing == 0, table.concat(missing, ", "))
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("penetration: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
