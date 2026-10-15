-- Hits (Claude 4): body parts from bones, blood, hit reactions, the impact mark (effect, sound, hole), the kill
-- shove, ApplyHit.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local SURFACES, entityHex, log, try, vadd, vcross, vdot, vnorm = U.SURFACES, U.entityHex, U.log, U.try, U.vadd, U.vcross, U.vdot, U.vnorm
local vscale, vsub = U.vscale, U.vsub

-- Body part from where the ray hit, relative to the target's feet and facing (human skeletons only).
local function bodyPartFor(e, pos)
	if not (e.actor and e.human) then return nil end
	local base = e:GetWorldPos()
	local h = pos.z - base.z
	if h > 1.5 then return "head" end
	local fwd = e:GetDirectionVector(1)
	local right = vnorm(vcross(fwd, { x = 0, y = 0, z = 1 }))
	local side = (pos.x - base.x) * right.x + (pos.y - base.y) * right.y
	if h < 0.9 then return side >= 0 and "leg_right" or "leg_left" end
	if math.abs(side) > 0.2 then return side >= 0 and "arm_right" or "arm_left" end
	return "torso"
end

-- first entry of a ray (origin o, unit direction d) into a sphere / a capsule from pa to pb (radius r): distance
-- along the ray, 0 when it starts inside, nil when it misses (the capsule: nearest of its body and its two end spheres)
local function raySphere(o, d, c, r)
	local oc = vsub(o, c)
	local b, cc = vdot(d, oc), vdot(oc, oc) - r * r
	if cc <= 0 then return 0 end
	local h = b * b - cc
	if h < 0 then return nil end
	local t = -b - math.sqrt(h)
	return t >= 0 and t or nil
end
local function rayCapsule(o, d, pa, pb, r)
	local best
	local function take(t) if t and (not best or t < best) then best = t end end
	local ba, oa = vsub(pb, pa), vsub(o, pa)
	local baba = vdot(ba, ba)
	if baba > 1e-10 then
		local bard, baoa = vdot(ba, d), vdot(ba, oa)
		local a = baba - bard * bard
		if a > 1e-10 then
			local b = baba * vdot(d, oa) - baoa * bard
			local cc = baba * vdot(oa, oa) - baoa * baoa - r * r * baba
			local h = b * b - a * cc
			if h >= 0 then
				local t = (-b - math.sqrt(h)) / a
				local y = baoa + t * bard
				if t >= 0 and y > 0 and y < baba then take(t) end
			end
		end
		take(raySphere(o, d, pb, r))
	end
	take(raySphere(o, d, pa, r))
	return best
end

-- Where a round really hits a person. Rays only report the living capsule round an NPC (checked live: surface
-- mat_flesh, no part), so the round's line is tested against the body's own capsules (guns/hitbones.lua: KCD2's
-- ragdoll proxies measured from male.chr, placed on the live skeleton by GetBonePos): the first one it enters. Torso
-- capsules use their outer radius (the chest's flat sides), limbs and head their fitted one. Returns the capsule
-- entry, its point and the outward normal there; false when the line misses every capsule (past an arm, between the
-- legs: the round flies on); nil when the skeleton can't be read (not a human).
function Deagle:BoneHit(e, from, dir)
	if not (self.hitBones and e.human and e.GetBonePos) then return nil end
	local cache, read = {}, 0
	local function bone(name)
		local p = cache[name]
		if p == nil then
			local ok, v = pcall(e.GetBonePos, e, name)
			p = ok and type(v) == "table" and v.x and v or false
			if p then read = read + 1 end
			cache[name] = p
		end
		return p or nil
	end
	local best, bestT, bestR, pa0, pb0
	for _, c in ipairs(self.hitBones) do
		local a, b = bone(c.a), bone(c.b)
		if a and b then
			local ab = vsub(b, a)
			local pa, pb = vadd(a, vscale(ab, c.t0)), vadd(a, vscale(ab, c.t1))
			local r = c.zone == "torso" and c.rMax or c.r
			local t = rayCapsule(from, dir, pa, pb, r)
			if t and t >= 0 and (not bestT or t < bestT - 1e-4 or (math.abs(t - bestT) <= 1e-4 and r < bestR)) then
				best, bestT, bestR, pa0, pb0 = c, t, r, pa, pb
			end
		end
	end
	if read == 0 then return nil end
	if not best then return false end
	local pos = vadd(from, vscale(dir, bestT))
	local ab = vsub(pb0, pa0)
	local k = vdot(ab, ab) > 1e-10 and math.max(0, math.min(1, vdot(vsub(pos, pa0), ab) / vdot(ab, ab))) or 0
	local normal = vnorm(vsub(pos, vadd(pa0, vscale(ab, k))))
	return best, pos, normal
end

-- Blood on what's behind the target and under it, Ready or Not's way: a spray on a wall within splatRange (3 m) behind
-- when the round came out the back (a burst behind a head), drips on the floor under the wound. Their decals at natural
-- size (m, docs/impact_fx_build.md section 6) x the round's impact scale, +-15 %, turned at random.
local SPATTER = {
	wall = { { "blood_spatter_1", 0.45 }, { "blood_spatter_2", 0.5 }, { "blood_spatter_3", 0.4 } },
	head = { { "blood_spatter_5", 1.0 } },
	floor = { { "blood_spatter_6", 0.18 }, { "blood_spatter_7", 0.18 } },
}
function Deagle:Splatter(e, hit, dir, scale, exits)
	local c = self.cfg
	local function decal(at, set)
		local d = set[math.random(#set)]
		local size = d[2] * scale * (0.85 + 0.3 * math.random())
		try("blood decal", Particle.CreateMatDecal, vadd(at.pos, vscale(at.normal, 0.01)), at.normal, size, c.splatLife,
			"Materials/deagle/decals/" .. d[1], math.random() * 360, dir)
	end
	local hits = {}
	if exits then
		local ok, n = pcall(Physics.RayWorldIntersection, vadd(hit.pos, vscale(dir, 0.15)), vscale(dir, c.splatRange), 1, ent_all, e.id, self:Player().id, hits)
		if ok and n and n > 0 and hits[1] then decal(hits[1], hit.bodyPart == "head" and SPATTER.head or SPATTER.wall) end
	end
	hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, hit.pos, { x = dir.x * 0.4, y = dir.y * 0.4, z = -2.5 }, 1, ent_static + ent_terrain, e.id, nil, hits)
	if ok and n and n > 0 and hits[1] then decal(hits[1], SPATTER.floor) end
end

-- A wound's blood (deagle_blood.xml, Ready or Not's): the entry splash back toward the shooter (with a cloth tuft when
-- the round came through padding / mail / plate), the exit spray along the round only when it came out the back (a
-- burst out of the back of a head), blood on the wall behind and the floor, KCD2's blood paint and bleeding.
function Deagle:Bleed(e, hit, dir, exits, scale, padded)
	local c = self.cfg
	local part = hit.bodyPart
	scale = scale or 1
	exits = exits ~= false
	self:SpawnFx(padded and c.bloodArmorFx or c.bloodFx, hit.pos, hit.normal or vscale(dir, -1), scale)
	if exits then
		self:SpawnFx(part == "head" and c.headFx or c.bloodExitFx, vadd(hit.pos, vscale(dir, part == "head" and 0.15 or 0.25)), dir, scale)
	end
	self:Splatter(e, hit, dir, scale, exits)
	if part and c.bloodPaint > 0 then
		try("paint blood", function() e.actor:AddBlood(c.bloodPaint, part) end)
	end
	local dead = false
	pcall(function() dead = e.actor:IsDead() or e.actor:GetHealth() <= 0 end)
	if c.bleeding and not dead then
		try("bleeding", function() e.soul:AddBuff(c.bleedingBuff) end)
	end
end

-- KCD2's own flinch / stagger on the one hit: Lua's DealDamage tags every hit as a fall, which has no reactions
-- (docs/kcd2_hit_reaction_re.md), so the plugin builds the game's hit-reaction event itself. Strength by the game's
-- own rule for a hit: health lost = minor injury (5), 50 or more = major (6); stamina only (a round stopped by armour):
-- under 10 tickle (2), under 50 unpleasant (3), else exhausting (4). Not on the dead or unconscious.
function Deagle:HitReact(e, hit, dir, dmg, stam)
	local c = self.cfg
	if not c.hitReactions then return end
	if not self.native then
		if not self.saidNoHitReact then
			self.saidNoHitReact = true
			log("$4hit reactions: the native plugin isn't running (deagle_native.asi): NPCs won't flinch")
		end
		return
	end
	local down = false
	pcall(function() down = e.actor:IsDead() or e.actor:GetHealth() <= 0 or (e.actor.IsUnconscious and e.actor:IsUnconscious()) end)
	if down then return end
	local flags = c.hitReactFlags
	if c.hitAI == "missile" and flags % 0x40 >= 0x20 then flags = flags - 0x20 end   -- its AI stimulus: HitAI's instead
	self:Native(string.format("hitreact 0x%s 0x%s %d %d %s %.3f %.3f %.3f %.4f %.4f %.4f %x", entityHex(e.id),
		entityHex(self:Player().id), c.hitReactType, self:HitStrength(dmg, stam), hit.bodyPart or "torso", hit.pos.x,
		hit.pos.y, hit.pos.z, dir.x, dir.y, dir.z, flags))
end
-- the game's HitReactionStrength for a hit: health lost = minor (5), cfg.hitReactMajor or more = major (6); stamina
-- only (stopped by armour): under 10 tickle (2), under 50 unpleasant (3), else exhausting (4)
function Deagle:HitStrength(dmg, stam)
	stam = stam or 0
	return dmg > 0 and (dmg >= self.cfg.hitReactMajor and 6 or 5) or (stam >= 50 and 4 or stam >= 10 and 3 or 2)
end

-- the mark a round leaves: the surface's effect, sized by the round, and a bullet hole that stays a while
function Deagle:Mark(s, hit, dir, scale, size)
	local c = self.cfg
	if s.fx then self:SpawnFx(s.fx == "blood" and c.bloodFx or ("deagle_impacts." .. s.fx), hit.pos, hit.normal, scale) end
	if s.ron then self:HitSound(s.ron, hit.pos, scale) end
	if s.hole then
		size = (size or c.holeSize) * scale * (0.85 + 0.3 * math.random())
		try("bullet hole", Particle.CreateMatDecal, vadd(hit.pos, vscale(hit.normal, 0.005)), hit.normal, size, c.holeLife,
			"Materials/deagle/decals/bullet_hole_" .. s.hole, math.random() * 360, dir, hit.entity and hit.entity.id or nil, hit.partid)
	end
end

-- A killing round drops the body at once (KCD2 would play a death animation first and ragdoll ~1 s later,
-- docs/kcd2_gore_ragdoll_re.md) and shoves it by the round's momentum at the part it hit: a ray along the round
-- against the fresh ragdoll returns that part. punch per momentum like corpses, x cfg.gore.killPush.
function Deagle:KillShove(e, hit, dir, phys, energy)
	local g = self.cfg.gore
	if not g.ragdollOnKill then return end
	try("ragdoll on kill", function() e.actor:RagDollize() end)
	local speed = math.sqrt(2 * (energy or phys.energy) / phys.bullet)
	local push = self.cfg.physics.punchPerMomentum * phys.bullet * speed * g.killPush
	Script.SetTimer(g.pushDelay, function()
		local part, hits = -1, {}
		local ok, n = pcall(Physics.RayWorldIntersection, vsub(hit.pos, vscale(dir, 0.6)), vscale(dir, 1.2), 1, ent_all, Deagle:Player().id, nil, hits)
		if ok and n and n > 0 and hits[1] and hits[1].entity == e and hits[1].partid then part = hits[1].partid end
		try("kill shove", function() e:AddImpulse(part, hit.pos, dir, push, 1) end)
		log(string.format("kill shove %s part %s push %.0f", tostring(e:GetName()), tostring(part), push))
	end)
end

-- A round hitting something. r = the round in flight (nil for an instant ray): true when it flies on (through a body
-- or wall, or turned off armour / stone), with r moved to where it goes on from.
function Deagle:ApplyHit(hit, dir, phys, r)
	local c = self.cfg
	phys = phys or self:ShotPhysics()
	local e = hit.entity
	local p = self:Player()
	local dead, goesOn = false, false
	if e and e.actor then pcall(function() dead = e.actor:IsDead() end) end
	if e and e ~= p and (dead or (e.class and string.find(string.lower(e.class), "dead"))) then
		hit.bodyPart = bodyPartFor(e, hit.pos)
		local scale = self:ImpactScale(phys, hit.dist, hit.energy)
		self:Bleed(e, hit, dir, true, scale)
		self:HitSound(SURFACES.mat_flesh.ron, hit.pos, scale)   -- a corpse was silent (the user, 2026-10-13)
		log("corpse hit " .. tostring(e:GetName()))
	elseif e and e.flock_entity and e ~= p then
		-- a chicken, cat, rat or fish: one Boid entity of a flock, no soul. KCD2 kills it through Boid:OnHit ->
		-- Boids.OnBoidHit(flock, boid, hit), which reads pos, dir and damage (the body is shoved by dir x damage: the
		-- round's momentum). Rounds only made a world impact on it (the user, 2026-10-14: "i cant shoot chickens"). A body
		-- this small doesn't stop a round: on through it
		local scale = self:ImpactScale(phys, hit.dist, hit.energy)
		if not e.bWasHit then
			e.bWasHit = 1
			try("boid hit", function() Boids.OnBoidHit(e.flock_entity, e, { pos = hit.pos, dir = dir, damage = phys and phys.momentum or 1,
				shooterId = p.id, shooter = p }) end)   -- the shooter: KCD2's animal-kill crime checks the killer (AI/animal/animals.xml)
		end
		self:SpawnFx(c.bloodFx, hit.pos, hit.normal or vscale(dir, -1), scale)
		self:SpawnFx(c.bloodExitFx, vadd(hit.pos, vscale(dir, 0.1)), dir, scale)
		self:HitSound(SURFACES.mat_flesh.ron, hit.pos, scale)
		log("boid hit " .. tostring(e:GetName()))
		if r then r.skip, r.pos = e.id, hit.pos; goesOn = true end
	elseif e and e.soul and e ~= p then
		if c.hitMode == "hit" then
			try("CreateHit", function()
				g_gameRules:CreateHit(e.id, p.id, p.id, c.damage, 0, nil, hit.partid or -1, c.hitType, hit.pos, dir, hit.normal)
			end)
			self:Bleed(e, hit, dir)
		else
			local bone, at, normal = self:BoneHit(e, vsub(hit.pos, vscale(dir, 1.5)), dir)
			if bone == false then                       -- through the gaps of the body: on past it
				if r then r.skip, r.pos = e.id, hit.pos end
				return r ~= nil
			end
			local part
			if bone then
				part, hit.pos, hit.normal, hit.zone = bone.armor, at, normal, bone.zone
			else
				if e.human and not self.saidNoBones then
					self.saidNoBones = true
					log("$4hit zones: can't read " .. tostring(e:GetName()) .. "'s skeleton (GetBonePos): zone from the hit height")
				end
				part = bodyPartFor(e, hit.pos)
			end
			hit.bodyPart = part
			local dmg, stam, through = 0, 0, nil
			local stopped, hard, glance, padded, scale
			if phys then        -- from the cartridge: through what's worn there, then the energy left in the body
				local a = self:ArmorHit(e, hit, dir, phys, r, part)   -- ballistics.lua
				dmg, stam, through, stopped, hard, glance, padded, scale = a.dmg, a.stam, a.through, a.stopped, a.hard, a.glance, a.padded, a.scale
				goesOn = goesOn or a.goesOn
				if part == "head" then
					dmg = dmg * c.physics.headMult
					-- into the skull, dead: a Glock round through a head at 0.7 m left 31 of 100 HP (in game 2026-10-14)
					if (a.left or 0) >= c.physics.skullJPerMm2 * math.pi * (phys.d * 1000 / 2) ^ 2 then
						local hp = 100
						pcall(function() hp = e.actor:GetHealth() end)
						dmg = math.max(dmg, hp)
					end
				end
				-- a wound means more to a small body: x (a man's mass / its mass)^sizeExponent (a 2.5 kg hare: x5.7, a 25 kg
				-- dog x1.8, a 500 kg horse x0.4; KCD2 gives every creature 100 HP)
				local mass = 0
				pcall(function() mass = e:GetMass() end)
				if mass and mass > 0 then
					local k = (c.physics.bodyMass / mass) ^ c.physics.sizeExponent
					dmg, stam = dmg * k, stam * k
				end
			else                -- a gun without cartridge data: its CS2 numbers
				dmg = c.damage * (part == "head" and c.headshotMult or 1) * c.rangeMod ^ ((hit.dist or 0) / 12.7)
				stam = c.stamina
			end
			local dummy = self:IsTestNpc(e)
			if dummy then   -- deagle_testnpc: takes no damage (no crime either); says what the hit would have done
				self:Notify(string.format("test NPC: %s, %s%s, would lose %.0f health %.0f stamina", tostring(part),
					hard and "armour " or "", stopped and "stopped it" or "went through", dmg or 0, stam or 0))
			else
				local attacker = p.id
				if c.hitAI == "missile" then attacker = __null end   -- who did it is HitAI's message (no attacker: as Quick.lua's dd)
				try("DealDamage", function() e.soul:DealDamage(stam, dmg, attacker, false, part) end)
			end
			self:HitReact(e, hit, dir, dmg, stam)
			if not dummy then self:HitAI(e, dmg, stam) end
			if r then r.hitIds = r.hitIds or {}; r.hitIds[e.id] = true end
			if not dummy then self:PainVoice(e) end
			local killed = false
			pcall(function() killed = e.actor:GetHealth() <= 0 end)
			if killed and phys then self:KillShove(e, hit, dir, phys, hit.energy) end
			if not dummy then self:ShotXP(killed) end
			local exits = through and through > c.exitEnergy
			if not stopped then self:Bleed(e, hit, dir, exits == true, scale, padded) end
			if exits and r then                         -- through and through: on with what's left, past this body
				r.pos, r.vel, r.skip = vadd(hit.pos, vscale(dir, c.bodyExit)), vscale(dir, math.sqrt(2 * through / phys.bullet)), e.id
				r.dist = r.dist + c.bodyExit
				goesOn = true
			end
			local hp = "?"
			pcall(function() hp = string.format("%.1f/%.1f", e.actor:GetHealth(), e.actor:GetMaxHealth()) end)
			log(string.format("hit %s part %s dist %.1f hp %s%s%s", tostring(e:GetName()), tostring(part), hit.dist or -1, hp,
				stopped and (" stopped by " .. tostring(stopped.a.name) .. (glance and " (glanced)" or "")) or "",
				exits and string.format(", out the back with %.0f J", through) or ""))
		end
	else
		goesOn = self:Impact(hit, dir, phys, r)
	end
	-- props and corpses are shoved (dead comes from the actor: an NPC entity has no IsDead of its own, so corpses never
	-- moved); the living get KCD2's hit reaction instead
	if e and e ~= p and e.AddImpulse and (not e.soul or dead) and not e.flock_entity then
		local push = phys and c.physics.punchPerMomentum * phys.momentum or c.impulse   -- the shove follows momentum
		try("impulse", function() e:AddImpulse(hit.partid or -1, hit.pos, dir, push, 1) end)
	end
	return goesOn
end
