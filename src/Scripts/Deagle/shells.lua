-- Shells (Claude 4): ejected cases, their entities, bounces and clinks.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local RON_SURFACE, camBasis, entityHex, surfaceName, try, vadd, vlen, vscale = U.RON_SURFACE, U.camBasis, U.entityHex, U.surfaceName, U.try, U.vadd, U.vlen, U.vscale

-- casing out of the viewmodel's ejection port: random cone, speed and spin so no two take the same path; port smoke
-- Casings come from a pool: CASE_POOL of them are made once, then the oldest is picked up again (its motion stopped
-- with ResetPhysics and thrown anew). Spawning a physics entity every shot made ~10 a second at 750 rpm and left up to
-- 200 rigid bodies lying around for 20 s; now at most CASE_POOL exist.
local CASE_POOL = 12
-- Casings in flight pass through each other (cfg.eject.ghost). At 250 rounds a second ~4 are thrown from the same
-- port spot in one frame, each inside the others: the rigid-body solver took the game from 60 to 5 fps within 2 s
-- (deagle_stress 250 sky shot casebody, 2026-10-17; the port smoke cost nothing). Each casing has its own collision
-- class bit and ignores that bit from its throw to its first bounce; on the ground they collide again, so they still
-- lie on each other. 2^30: KCD2's classes (g_PhysicsCollisionClass) use bits 0-27; Lua numbers are floats, exact here.
local CASE_CLASS = 1073741824
local function caseGhost(e, on)
	pcall(e.SetPhysicParams, e, PHYSICPARAM_COLLISION_CLASS,
		on and { collisionClass = CASE_CLASS, collisionClassIgnore = CASE_CLASS } or { collisionClassIgnoreUNSET = CASE_CLASS })
end
function Deagle:CaseEntity()
	self.casePool = self.casePool or {}
	self.caseNext = (self.caseNext or 0) % CASE_POOL + 1
	local id = self.casePool[self.caseNext]
	local e = id and System.GetEntity(id)
	if e then
		e:ResetPhysics()
		return e
	end
	e = System.SpawnEntity({
		class = "BasicEntity", name = "deagle_case_" .. self.caseNext, position = { x = 0, y = 0, z = -1000 },
		flags = ENTITY_FLAG_NO_SAVE,
		properties = { object_Model = self.cfg.modelDir .. "deagle_case.cgf", Physics = { bPhysicalize = true, bRigidBody = true, bPushableByPlayers = false, Density = -1, Mass = 0.02 } },
	})
	e:SetFlags(ENTITY_FLAG_NO_SAVE, 0)   -- never into a save
	self.casePool[self.caseNext] = e.id
	return e
end

local function inRange(r) return r[1] + math.random() * (r[2] - r[1]) end
function Deagle:EjectCase()
	local port = self:VmBone(self.gun.bones.brass)
	if not port then return end
	local c = self.cfg
	try("eject case", function()
		local _, f, r, u = camBasis()
		local E = c.eject
		local case = self:CaseEntity()
		case:SetWorldPos(port)
		case:SetDirectionVector(f)   -- its long axis (the model's Y) down the bore, as it leaves the chamber
		local up, back = math.rad(inRange(E.up)), math.rad(inRange(E.back))
		local out = vadd(vscale(r, math.cos(back)), vscale(f, -math.sin(back)))
		local v = vscale(vadd(vscale(out, math.cos(up)), vscale(u, math.sin(up))), inRange(E.speed))
		local okP, pv = pcall(function() return self:Player():GetVelocity() end)
		if okP and type(pv) == "table" then v = vadd(v, pv) end   -- the brass keeps the shooter's own motion
		-- end over end: the ejector stops the case head while the extractor pulls the rim, so the mouth swings out to
		-- the right (spin about -up), with some wobble
		local w = vadd(vscale(u, -inRange(E.spin)), vadd(vscale(r, (math.random() - 0.5) * 40), vscale(f, (math.random() - 0.5) * 40)))
		if self.native then self:Native(string.format("casespin %s %.0f", entityHex(case.id), E.spinCap)) end
		case:SetVelocityEx(v, w)
		local ghost = c.eject.ghost and true or false
		caseGhost(case, ghost)
		self.casesFlying = self.casesFlying or {}
		self.casesFlying[case.id] = { t = System.GetCurrTime(), bounces = 0, ev = self.gun.ron and self.gun.ron.shell, ghost = ghost }
	end)
	self:SpawnFx(c.portFx, port, System.GetViewCameraUpDir(), 1.0)
end

-- A casing's bounces: Ready or Not plays its shell event (9mmShell) on every bounce with Surface = what it landed on
-- (BulletShell, ShellBounceFMODAudio; it skips bounces under 0.5 m/s). KCD2 doesn't report physics contacts to Lua,
-- so each frame a flying casing's velocity is read: a bounce is a frame where it was falling and now isn't (or lost
-- over cfg.caseBounce.drop of its speed). The surface is a short ray down from it.
function Deagle:CaseTick()
	local flying = self.casesFlying
	if not flying or not next(flying) then return end
	local c, now = self.cfg.caseBounce, System.GetCurrTime()
	for id, f in pairs(flying) do
		local e = System.GetEntity(id)
		local ok, v = false, nil
		if e then ok, v = pcall(e.GetVelocity, e) end
		if not ok or not v or now - f.t > c.life or f.bounces >= c.max then
			if e and f.ghost then caseGhost(e, false) end   -- never left a ghost on the ground
			flying[id] = nil
		else
			local speed = vlen(v)
			local was = f.v
			if was then
				local wasSpeed = vlen(was)
				local landed = was.z < -c.minSpeed and v.z > -0.25 * math.abs(was.z)
				local lost = wasSpeed > c.minSpeed and speed < wasSpeed * (1 - c.drop)
				if (landed or lost) and wasSpeed >= c.minSpeed then
					f.bounces = f.bounces + 1
					if f.bounces == 1 and self.native then self:Native(string.format("casespin %s 20", entityHex(id))) end   -- the physics' own cap on the ground
					if f.bounces == 1 and f.ghost then caseGhost(e, false); f.ghost = false end   -- landed: collides with casings again
					local pos = e:GetWorldPos()
					local hits = {}
					local okR, n = pcall(Physics.RayWorldIntersection, vadd(pos, { x = 0, y = 0, z = 0.05 }), { x = 0, y = 0, z = -0.3 }, 1,
						ent_static + ent_terrain, id, nil, hits)
					local sf = okR and n and n > 0 and hits[1] and RON_SURFACE[surfaceName(hits[1])]
					if sf == "bell" then sf = 44 end   -- a casing on a bell: steel
					-- Claude 3's sound budget: clinks 12 a second (burst 6), merged within 0.3 m / 0.1 s
					if sf and self.native and f.ev and self:SoundBudget("casing", f.ev, pos) then
						self:Native(string.format("ron3 %.3f %.2f %.2f %.2f %s|Surface=%d", c.volume, pos.x, pos.y, pos.z, f.ev, sf))
					end
				end
			end
			f.v = v
		end
	end
end
