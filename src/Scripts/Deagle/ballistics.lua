-- Rounds in flight, ricochets, corners, wallbangs and armour: the ballistics half of the mod, kept apart from
-- deagle_main.lua (viewmodel, sounds, effects, NPC reactions) so the two can be worked on at once. Loaded right after
-- deagle_main.lua (Deagle:LoadModule), whose helpers it uses through Deagle.U. Data: guns/penetration.lua (materials:
-- density, armour level, critical angle), guns/armor.lua (what NPCs wear), guns/calibers.lua (rounds).
local U = Deagle.U
local log, try, vadd, vsub, vscale, vlen, vnorm, vdot, vcross, coneDir = U.log, U.try, U.vadd, U.vsub, U.vscale, U.vlen, U.vnorm,
	U.vdot, U.vcross, U.coneDir
local surfaceName, SURFACES = U.surfaceName, Deagle.surfaces
local PATH_FLY, PATH_ON, PATH_IN = U.PATH.fly, U.PATH.on, U.PATH.inside

-- ---------------------------------------------------------------------------
-- energy, armour, ricochets
-- ---------------------------------------------------------------------------
-- A round's punch: its energy per area of its cross-section, against a 9 mm at the muzzle (1 = 9 mm). What gets a
-- round through a wall or a piece of armour (sectional energy; the .50 AE ~1.9).
function Deagle:EnergyRatio(phys, energy)
	local ref = self.calibers["9x19"]
	return (energy / (phys.d * phys.d)) / (0.5 * ref.bullet * ref.velocity * ref.velocity / (ref.d * ref.d))
end

-- How big a round's impact looks: debris and crater volume go with the energy it lands with, so their size goes with
-- its cube root, against a 9 mm at the muzzle (= 1: the size the effects and holes are made at). Deagle ~1.5 close up.
function Deagle:ImpactScale(phys, dist, energy)
	if not phys then return 1 end
	local ref = self.calibers["9x19"]
	local eIn = energy or phys.energy * math.exp(-2 * (dist or 0) / phys.dragLength)
	return (eIn / (0.5 * ref.bullet * ref.velocity * ref.velocity)) ^ (1 / 3)
end

-- What an NPC wears on one zone (head / torso / arm_* / leg_*), outer layer first: the items in its inventory that are
-- armour or clothing (guns/armor.lua, generated from KCD2's tables) and cover that zone, with their condition (0..1).
-- KCD2 gives Lua no "equipped" query; an NPC's inventory is its outfit (checked live: a guard's lists exactly the
-- plate, mail and gambeson he wears).
function Deagle:ArmorOn(e, zone)
	local out = {}
	if not (self.armor and zone and e.inventory) then return out end
	pcall(function()
		for _, id in pairs(e.inventory:GetInventoryTable() or {}) do
			local it = ItemManager.GetItem(id)
			local a = it and self.armor[string.lower(tostring(it.class))]
			if a then
				for _, z in ipairs(a.zones) do
					if z == zone then out[#out + 1] = { a = a, health = tonumber(it.health) or 1 }; break end
				end
			end
		end
	end)
	table.sort(out, function(x, y) return x.a.order > y.a.order end)
	return out
end

-- A round against the layers on the spot it hit, outer first (BALLISTICS.md "Small rounds bounce off plate"). Its punch
-- is cfg.armor.punch for a 9 mm at the muzzle, times its energy ratio, times cos of the angle off square-on (a plate
-- hit at an angle is thicker along the path). Each layer takes its KCD2 stab defense out of it, scaled by its
-- condition (a worn piece keeps conditionFloor + the rest x condition); a layer with more defense than the punch left
-- stops the round. Plate and mail can turn it first: a hit flatter than their critical angle (guns/penetration.lua
-- armorRicochet x cfg.armor.critScale) skips off (RicochetChance). A tumbling round has cfg.tumble.pen of its punch.
-- So a 9 mm gets through a worn cuirass only up close and square-on, a .50 AE through most plate. Returns the energy
-- that reaches the body (0 when stopped), the layer that stopped it, whether a hard layer (plate / mail) was hit and
-- whether the round skipped off it.
function Deagle:ThroughArmor(layers, phys, energy, cosI, tumble)
	local m = self.cfg.armor
	local punch0 = m.punch * self:EnergyRatio(phys, energy) * math.max(cosI, 0.05) * (tumble and self.cfg.tumble.pen or 1)
	local graze = math.deg(math.asin(math.min(1, cosI)))   -- between the round's path and the armour's face
	local punch, hard = punch0, false
	for _, l in ipairs(layers) do
		local isHard = l.a.layer == "plate" or l.a.layer == "mail"
		hard = hard or isHard
		local ric = isHard and self.armorRicochet and self.armorRicochet[l.a.layer]
		if ric and math.random() < self:RicochetChance(ric.crit, graze, m.critScale) then return 0, l, true, true end
		local def = l.a.stab * (m.conditionFloor + (1 - m.conditionFloor) * l.health)
		if punch < def then return 0, l, hard end
		punch = punch - def
	end
	return energy * punch / punch0, nil, hard
end

-- The chance a round skips off a material whose critical angle is crit deg, hit graze deg off its face: half at crit,
-- nearly always when flatter, almost never when steeper (cfg.ricochet); crit 0 = never
function Deagle:RicochetChance(crit, graze, critScale)
	local c = self.cfg.ricochet
	if not crit or crit <= 0 then return 0 end
	return 1 / (1 + math.exp((graze - crit * (critScale or c.critScale)) / c.width))
end

-- A round turned off a surface or armour: on along it, leaving at lift x its angle to the surface (hard surfaces
-- cfg.ricochetLift: flatter than it came in) up to exitMax deg, or at exit deg whatever it came in at (water);
-- scattered, with keep of its energy, tumbling. It flies on and can hit something else; its tracer starts again here.
function Deagle:Deflect(r, pos, normal, dir, keep, scatter, skip, lift, exit, exitMax)
	local c = self.cfg
	local along = vsub(dir, vscale(normal, vdot(dir, normal)))
	if vlen(along) < 1e-6 then along = vcross(normal, math.abs(normal.z) < 0.9 and { x = 0, y = 0, z = 1 } or { x = 1, y = 0, z = 0 }) end
	along = vnorm(along)
	local graze = math.deg(math.asin(math.min(1, math.max(0, -vdot(dir, normal)))))
	local a = math.rad(exit or math.min(graze * (lift or c.ricochetLift), exitMax or 90))
	local out = vadd(vscale(along, math.cos(a)), vscale(normal, math.sin(a)))
	out = coneDir(out, scatter)
	local up = vdot(out, normal)
	if up < 0.01 then out = vnorm(vadd(out, vscale(normal, 0.01 - up))) end   -- never back into the surface (>= 0.6 deg off it)
	local speed = vlen(r.vel) * math.sqrt(keep)
	r.pos, r.vel, r.skip = vadd(pos, vscale(normal, 0.01)), vscale(out, speed), skip
	r.tumble, r.turnPos, r.turnDist = true, r.pos, r.dist
	if c.tracers and c.tracer.mode == "flying" then self:SpawnFx(self:TracerFx(speed), r.pos, out, 1.0) end
	return out
end

-- A round plate stops doesn't bounce back: it flattens and breaks up, and the fragments spray out along the plate, most
-- of them cfg.armor.splashAngle deg off its face, on the way the round was going (FBI steel-target tests: police1.com
-- "Training with steel targets"); a square-on hit sprays them any way round.
function Deagle:Splash(pos, normal, dir, scale)
	local c = self.cfg
	local along = vsub(dir, vscale(normal, vdot(dir, normal)))
	if vlen(along) < 0.2 then   -- square-on: a random way along the plate
		local t = math.abs(normal.z) < 0.9 and { x = 0, y = 0, z = 1 } or { x = 1, y = 0, z = 0 }
		local u = vnorm(vsub(t, vscale(normal, vdot(t, normal))))
		local a = math.random() * 2 * math.pi
		along = vadd(vscale(u, math.cos(a)), vscale(vcross(normal, u), math.sin(a)))
	end
	local s = math.rad(c.armor.splashAngle)
	local out = vnorm(vadd(vscale(vnorm(along), math.cos(s)), vscale(normal, math.sin(s))))
	self:SpawnFx(c.armorRicochetFx, pos, out, scale)
	return out
end

-- A round on an NPC's body part (ApplyHit, deagle_main.lua): through what's worn there (ThroughArmor), the wound from
-- the energy left (Wound), armour sparks and sounds, a glance off plate / mail (Deflect) or a round the plate stopped
-- breaking up on it (Splash). Returns { dmg, stam, through (J out the back), stopped (layer), hard, glance, padded,
-- scale, goesOn }; the body's size and the head multiplier are ApplyHit's.
function Deagle:ArmorHit(e, hit, dir, phys, r, part)
	local c = self.cfg
	local a = { dmg = 0, stam = 0 }
	local energy = hit.energy or phys.energy * math.exp(-2 * (hit.dist or 0) / phys.dragLength)
	local cosI = hit.normal and math.abs(vdot(dir, hit.normal)) or 1
	local layers = self:ArmorOn(e, part)
	for _, l in ipairs(layers) do a.padded = a.padded or l.a.layer == "padding" or l.a.layer == "mail" or l.a.layer == "plate" end
	local left
	left, a.stopped, a.hard, a.glance = self:ThroughArmor(layers, phys, energy, cosI, r and r.tumble)
	a.scale = self:ImpactScale(phys, hit.dist, energy)
	local bare, shove = self:Wound(phys, hit.dist, energy)
	a.stam = shove                                    -- the shove lands whatever stops the round
	if a.stopped then
		a.dmg = bare * c.armor.bruise                 -- blunt trauma: a bruise under the armour
	else
		local _, _, _, out = self:Wound(phys, hit.dist, left)
		a.dmg = self:Wound(phys, hit.dist, left)
		a.through = out
	end
	if a.hard then
		self:SpawnFx(c.armorFx, hit.pos, hit.normal or vscale(dir, -1), a.scale)
		self:HitSound((part == "head" and "armor_helmet" or "armor_body") .. (a.stopped and "_block" or ""), hit.pos, a.scale)
	end
	if not a.stopped then self:HitSound(SURFACES.mat_flesh.ron, hit.pos, a.scale) end
	if a.glance and r then                        -- skipped off plate / mail: off it goes, sparks along its new way
		local ric = self.armorRicochet[a.stopped.a.layer]   -- the layer it skipped off: plate as sheet steel, mail as steel
		local out = self:Deflect(r, hit.pos, hit.normal or vscale(dir, -1), dir, ric.keep, c.armor.scatter, e.id, ric.lift, ric.exit,
			ric.exitMax)
		self:SpawnFx(c.armorRicochetFx, hit.pos, out, a.scale * c.ricochet.fxScale)
		self:HitSound("ricochet", hit.pos, a.scale, c.ricochet.volume)
		a.goesOn = true
	elseif a.stopped and (a.stopped.a.layer == "plate" or a.stopped.a.layer == "mail") then
		self:Splash(hit.pos, hit.normal or vscale(dir, -1), dir, a.scale)   -- flattened on it: no bounce back
	end
	return a
end

-- ---------------------------------------------------------------------------
-- the world: ricochets, corners, wallbangs
-- ---------------------------------------------------------------------------
local EXIT_FX = { stone = true, plaster = true, wood = true, metal = true, glass = true, cloth = true }   -- <family>_exit
local THIN = { glass = true, cloth = true, straw = true }   -- panes / sheets: often built with no thickness

-- A round hitting the world (BALLISTICS.md phase 3). A hit flatter than the material's critical angle skips off
-- (RicochetChance, guns/penetration.lua: measured angles): ricochet whine, and sparks off hard surfaces. Otherwise the
-- surface's mark (stopped by steel, the round breaks up and sprays fragments along it), then through it if the round can be: its punch has to reach the surface's armour level, and it gets
-- through cfg.wallbang.budget x energy ratio / density metres of it (half that tumbling). A ray back from that deep
-- finds the far side; if the thing is thinner, the round comes out there (debris out the back, a bigger exit hole)
-- with the energy it didn't spend. Out through a face that isn't the far side (a corner, an edge) it's bent off away
-- from the material and tumbles (cfg.deflect). True when the round flies on.
function Deagle:Impact(hit, dir, phys, r)
	local c = self.cfg
	local name = surfaceName(hit)
	local s = SURFACES[name]
	if not s then
		self.unknownSurface = self.unknownSurface or {}
		if not self.unknownSurface[name] then
			self.unknownSurface[name] = true
			log("$4impact: surface '" .. tostring(name) .. "' isn't in the table (deagle_main.lua SURFACES)")
		end
		return false
	end
	local energy = hit.energy or (phys and phys.energy * math.exp(-2 * (hit.dist or 0) / phys.dragLength))
	local scale = self:ImpactScale(phys, hit.dist, energy)
	local pen = self.penetration and self.penetration[name]
	local cosI = math.max(0, -vdot(dir, hit.normal))
	local graze = math.deg(math.asin(math.min(1, cosI)))     -- between the shot and the surface
	if r and pen and math.random() < self:RicochetChance(pen.crit, graze) then
		self:Mark(s, hit, dir, scale * 0.6)
		local out = self:Deflect(r, hit.pos, hit.normal, dir, pen.keep or c.ricochetKeep, c.ricochetScatter, nil,
			pen.lift or (not pen.hard and 1 or nil), pen.exit, pen.exitMax)
		if pen.hard then
			self:SpawnFx("deagle_impacts.ricochet_" .. (s.fx == "metal" and "metal" or "stone"), hit.pos, out, scale * c.ricochet.fxScale)
		end
		self:HitSound("ricochet", hit.pos, scale, c.ricochet.volume)
		if self.state.logActions then log(string.format("ricochet %s at %.0f deg (critical %.0f)", name, graze, pen.crit)) end
		return true
	end
	self:Mark(s, hit, dir, scale)
	if self.state.logActions then log(string.format("impact %s -> %s x%.2f hole %s", name, s.fx, scale, tostring(s.hole))) end
	-- stopped by steel: the round breaks up on it and its fragments spray along the face (Splash, as on plate armour)
	local function stops()
		if pen and pen.hard and s.fx == "metal" then self:Splash(hit.pos, hit.normal, dir, scale) end
		return false
	end
	if not (r and pen and pen.penetrable) or s.fx == "water" then return stops() end
	local ratio = self:EnergyRatio(phys, energy)
	if 2 + math.floor(math.log(ratio) / math.log(2) + 0.5) < pen.armour then return stops() end   -- 9 mm level 2, .50 AE 3
	local depth = c.wallbang.budget * ratio / pen.density * (r.tumble and c.tumble.pen or 1)
	local reach, back, p = depth + 0.01, {}, self:Player()
	local ok, n = pcall(Physics.RayWorldIntersection, vadd(hit.pos, vscale(dir, reach)), vscale(dir, -reach), 1, ent_all, p and p.id, nil, back)
	local far = ok and n and n > 0 and back[1]
	local thick = far and far.entity == hit.entity and reach - far.dist or nil
	if THIN[s.fx] then thick = math.max(thick or 0, 0.005)
	elseif not thick or thick < 0.002 then return stops() end     -- the far side is out of reach (or it's the near side)
	if thick >= depth then return stops() end
	local exitPos = vadd(hit.pos, vscale(dir, thick))
	local exitNormal = far and vdot(far.normal, dir) > 0 and far.normal or dir
	if EXIT_FX[s.fx] then self:SpawnFx("deagle_impacts." .. s.fx .. "_exit", exitPos, dir, scale) end
	if s.fx == "glass" then self:HitSound(s.ron, exitPos, scale) end
	if s.hole then
		self:Mark({ hole = s.hole }, { pos = exitPos, normal = exitNormal, entity = hit.entity }, dir, scale, c.holeExitSize)
	end
	local left = energy * (1 - thick / depth)
	-- in and out through faces that aren't each other's far side (a corner, an edge): the two outward normals add up
	-- to where the material isn't, so that's the way it's pushed (a plank's faces cancel: straight through)
	local out = dir
	if far and vdot(far.normal, dir) > 0 then
		local side = vadd(hit.normal, exitNormal)
		side = vsub(side, vscale(dir, vdot(side, dir)))
		local lop = vlen(side)
		if lop > 0.1 then
			local b = math.rad(c.deflect.maxDeg * math.min(1, lop) * (0.5 + 0.5 * thick / depth))
			out = coneDir(vnorm(vadd(vscale(dir, math.cos(b)), vscale(vnorm(side), math.sin(b)))), c.deflect.scatter)
			r.tumble = true
			self:SpawnFx("deagle_impacts." .. s.fx, exitPos, exitNormal, scale * c.deflect.chipScale)   -- the corner breaks off
			if self.state.logActions then log(string.format("deflect %s: corner %.1f cm, bent %.0f deg", name, thick * 100, math.deg(b))) end
		end
	end
	r.pos, r.vel, r.dist = vadd(exitPos, vscale(out, 0.002)), vscale(out, math.sqrt(2 * left / phys.bullet)), r.dist + thick
	if out ~= dir then r.turnPos, r.turnDist = r.pos, r.dist end
	if c.tracers and c.tracer.mode == "flying" then self:SpawnFx(self:TracerFx(vlen(r.vel)), r.pos, out, 1.0) end
	if self.state.logActions then log(string.format("through %s %.1f cm (could %.1f), %.0f J left", name, thick * 100, depth * 100, left)) end
	return true
end

-- ---------------------------------------------------------------------------
-- rounds in flight (BALLISTICS.md phase 1): a shot is a round with the cartridge's muzzle speed, gravity and air drag,
-- moved every frame and checked against the world along the stretch it covered (a ray per step, so nothing is
-- skipped however fast it flies). Air drag is the one Wound uses for energy: speed falls as e^(-distance / dragLength),
-- dv/dt = -v|v| / dragLength. The first step covers one frame at the shot, so up close a round lands in the frame it's
-- fired, like before; farther away it arrives late and low (a .50 AE at 100 m: ~0.24 s and ~27 cm under the line).
-- ---------------------------------------------------------------------------
local GRAVITY = 9.81             -- m/s^2
-- what a round's flight rays hit: ent_all plus water surfaces (CryEngine's ent_water; KCD2 has no global for it and
-- its ent_all, 287, leaves water out: a round went through a lake's surface and splashed on the bed, up to 5 m down)
local FLIGHT_MASK = ent_all % 0x400 >= 0x200 and ent_all or ent_all + 0x200   -- 0x200 = ent_water
local ROUND_STEP = 0.02          -- s: a longer frame is cut into steps this long, so the arc stays smooth
local ROUND_LIFE = 6             -- s: a round that hit nothing is dropped after this, or past cfg.range (a Deagle round
                                 -- takes ~5 s to fly 1000 m)

-- A tumbling round (after a ricochet or a corner) shows its side as often as its point: it drags like a tumbling
-- fragment. Tumbling changes only the area it shows, on average a quarter of its surface, and the drag coefficient
-- follows the shape: Cd = 1.41 - 1.149 V^(2/3) / A, the fit to McCleskey's tumbling fragments at low Mach (a cube 0.64,
-- a sphere 0.42, a plate 1.4: Moxnes et al., Defence Technology 2017). The round: a cylinder of its calibre holding its
-- weight at jacketed-lead density. A .50 AE comes out Cd 0.58 on 1.6x its front, ~2.5x the drag of flying point-first
-- (a 9 mm ~2.8x). Fragments drag more again near the speed of sound (not modelled).
local AIR_RHO = 1.2              -- kg/m^3, as deagle_main.lua
local BULLET_RHO = 11000         -- kg/m^3: lead (11.34) in a copper jacket (8.96)
function Deagle:TumbleCd(shape) return 1.41 - 1.149 * shape end
function Deagle:TumbleDragLength(phys)
	local front = math.pi * (phys.d / 2) ^ 2
	local len = phys.bullet / (BULLET_RHO * front)
	local area = (2 * front + math.pi * phys.d * len) / 4       -- the area a tumbling body shows, on average
	return 2 * phys.bullet / (AIR_RHO * self:TumbleCd((front * len) ^ (2 / 3) / area) * area)
end

-- one step of h seconds: true when the round is done (it hit something, or flew out of range / time)
function Deagle:MoveRound(r, h)
	local speed = vlen(r.vel)
	local v1 = vscale(r.vel, 1 / (1 + speed * h / (r.tumble and self:TumbleDragLength(r.phys) or r.phys.dragLength)))
	v1.z = v1.z - GRAVITY * h
	local move = vscale(vadd(r.vel, v1), 0.5 * h)
	local hits = {}
	local p = self:Player()
	local ok, n = pcall(Physics.RayWorldIntersection, r.pos, move, 1, FLIGHT_MASK, p and p.id, r.skip, hits)
	if not ok then
		log("$4raycast failed: " .. tostring(n))
		return true
	end
	local from, left, through = r.pos, move, 0
	while n and n > 0 and hits[1] do
		local hit = hits[1]
		local s = not hit.entity and SURFACES[surfaceName(hit)]
		if s and s.fx == "pass" and through < 8 then
			-- an invisible blocker or foliage: on through it, from just past the hit, for the rest of this step
			through = through + 1
			local dir, done = vnorm(move), hit.dist + 0.001     -- hit.dist counts from this step's start
			if done >= vlen(move) then break end
			from, left = vadd(r.pos, vscale(dir, done)), vscale(dir, vlen(move) - done)
			hits = {}
			ok, n = pcall(Physics.RayWorldIntersection, from, left, 1, FLIGHT_MASK, p and p.id, r.skip, hits)
			if not ok then log("$4raycast failed: " .. tostring(n)); return true end
			if n and n > 0 and hits[1] then hits[1].dist = hits[1].dist + done end
		else
			local frac = hit.dist / vlen(move)   -- share of this step flown before the hit
			hit.dist = r.dist + hit.dist        -- the ray's distance is from this step's start: the path flown so far
			local speed = vlen(r.vel)
			hit.energy = 0.5 * r.phys.bullet * speed * speed
			self:PathAdd(r.pos, hit.pos, (r.hops or 0) > 0 and PATH_ON or PATH_FLY)
			self:PathMark(hit.pos, hit.entity and hit.entity.soul and PATH_IN or { 1, 1, 1 })
			r.dist, r.age, r.pos = hit.dist, r.age + h * frac, hit.pos
			if not self:ApplyHit(hit, vnorm(move), r.phys, r) then r.landed = true; return true end
			if vlen(vsub(r.pos, hit.pos)) > 0.02 then self:PathAdd(hit.pos, r.pos, PATH_IN) end   -- through a wall / body
			-- on through a wall / body, or off armour / stone: the rest of this step from where it goes on
			r.hops = (r.hops or 0) + 1
			if r.hops > 8 or vlen(r.vel) < 30 then return true end
			return self:MoveRound(r, h * (1 - frac))
		end
	end
	self:PathAdd(r.pos, vadd(r.pos, move), (r.hops or 0) > 0 and PATH_ON or PATH_FLY)
	r.pos, r.vel, r.age, r.dist = vadd(r.pos, move), v1, r.age + h, r.dist + vlen(move)
	return r.dist > self.cfg.range or r.age > ROUND_LIFE
end

function Deagle:StepRound(r, dt)
	local done, from = false, r.pos
	while dt > 1e-6 do
		local h = math.min(dt, ROUND_STEP)
		dt = dt - h
		if self:MoveRound(r, h) then done = true; break end
	end
	self:TracerBullet(r, from)
	if r.logId then
		log(string.format("shot %d t=%.4f pos %.2f %.2f %.2f speed %.1f dist %.1f%s", r.logId, System.GetCurrTime(), r.pos.x, r.pos.y,
			r.pos.z, vlen(r.vel), r.dist, done and (r.landed and " HIT" or " END") or ""))
	end
	return done
end

function Deagle:LaunchRound(pos, dir, phys, trail)
	local r = { pos = pos, vel = vscale(dir, phys.velocity), phys = phys, dist = 0, age = 0, t = System.GetCurrTime(), trail = trail }
	if self.cfg.debugShotLog then
		self.shotLogN = (self.shotLogN or 0) + 1
		r.logId = self.shotLogN
		log(string.format("shot %d t=%.4f FIRED eye %.2f %.2f %.2f dir %.4f %.4f %.4f speed %.1f gun %s fov %.3f", r.logId, r.t, pos.x, pos.y, pos.z,
			dir.x, dir.y, dir.z, phys.velocity, self.gun.id, System.GetViewCameraFov and System.GetViewCameraFov() or -1))
		if self.fc then log(string.format("shot %d freecam aim pitch %.4f yaw %.4f", r.logId, self.fc.camP, self.fc.camY)) end
		self:Native("freecam status")   -- the free camera's place, in the plugin log
	end
	if self:StepRound(r, self.frameDt or 1 / 30) then return r end   -- landed in this frame
	self.rounds = self.rounds or {}
	table.insert(self.rounds, r)
	if not self.roundsToken or System.GetCurrTime() - (self.roundsTickT or -1e9) > 0.5 then   -- none, or a dead chain
		local token = {}
		self.roundsToken = token
		Script.SetTimer(0, function() Deagle:RoundsTick(token) end)
	end
	return r
end

-- every frame while rounds fly: each moves by the game time since its last move (a round fired this frame waits)
function Deagle:RoundsTick(token)
	if token ~= self.roundsToken then return end
	self.roundsTickT = System.GetCurrTime()
	local now, keep = System.GetCurrTime(), {}
	for _, r in ipairs(self.rounds or {}) do
		local dt = now - r.t
		r.t = now
		if not self:StepRound(r, dt) then keep[#keep + 1] = r end
	end
	self.rounds = keep
	if #keep > 0 then Script.SetTimer(0, function() Deagle:RoundsTick(token) end)
	else self.roundsToken = nil end
end
