-- Shells (Claude 4): ejected cases, their entities, bounces and clinks.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local RON_SURFACE, entityHex, surfaceName, try, vadd, vcross, vdot, vlen, vnorm, vscale = U.RON_SURFACE, U.entityHex, U.surfaceName, U.try, U.vadd, U.vcross, U.vdot, U.vlen, U.vnorm, U.vscale

-- A casing out of the viewmodel's ejection port, thrown the way Ready or Not throws its BulletShell (decompiled
-- ABaseShell init FUN_1449d27e0, docs/ron/gun_smoke_shells.md 2): out of the gun's right side 40 deg up (no cone), at
-- max(2 m/s, Henry's sideways speed) x a random 1-3 and nothing of his other motion (the case starts at rest and gets
-- a velocity change); its own rotation kept from where it last lay plus a random whole -10..10 deg of pitch and yaw;
-- spin one random whole number 0..360 rad/s on all three world axes, which the physics' top spin cuts to
-- cfg.eject.spinCap; damping 1 /s. Its physics stops at 4 s and it goes back to the pool at 6 s (hidden).
-- Casings come from a pool: CASE_POOL of them are made once, then the oldest is picked up again (its motion stopped
-- with ResetPhysics and thrown anew), so at most CASE_POOL exist. 80 holds 6 s of the fastest gun here (CS2 rifles,
-- 0.08 s a shot: 75); faster fire (deagle_stress) takes back the oldest early.
local CASE_POOL = 80
-- Casings in flight pass through each other (cfg.eject.ghost). At 250 rounds a second ~4 are thrown from the same
-- port spot in one frame, each inside the others: the rigid-body solver took the game from 60 to 5 fps within 2 s
-- (deagle_stress 250 sky shot casebody, 2026-10-17; the port smoke cost nothing). Casings in flight carry a class bit
-- of their own (one bit, all casings) and ignore it; at the first bounce both go, so a landed casing collides with
-- everything again, a falling one included (CryEngine skips a pair when either one's type is in the other's ignore:
-- clearing only the ignore let a falling casing sink through a pile, Claude 5). 2^30: KCD2's classes
-- (g_PhysicsCollisionClass) use bits 0-27; Lua numbers are floats, exact here.
local CASE_CLASS = 1073741824
local function caseGhost(e, on)
	pcall(e.SetPhysicParams, e, PHYSICPARAM_COLLISION_CLASS, on and { collisionClass = CASE_CLASS, collisionClassIgnore = CASE_CLASS }
		or { collisionClassUNSET = CASE_CLASS, collisionClassIgnoreUNSET = CASE_CLASS })
end
-- one pool per casing model: a gun with its own casing (gun.caseModel, Ready or Not's 50AE_Casing for the Grizzly)
-- throws its own; the others throw ours
function Deagle:CaseEntity()
	local model = self.gun.caseModel or self.cfg.modelDir .. "deagle_case.cgf"
	self.casePools = self.casePools or {}
	local pool = self.casePools[model]
	if not pool then pool = { next = 0 }; self.casePools[model] = pool end
	pool.next = pool.next % CASE_POOL + 1
	local id = pool[pool.next]
	local e = id and System.GetEntity(id)
	if e then
		e:Hide(0)
		e:EnablePhysics(true)   -- stopped at cfg.eject.freeze
		e:ResetPhysics()
		return e
	end
	e = System.SpawnEntity({
		class = "BasicEntity", name = "deagle_case_" .. model:match("([^/]+)%.cgf$") .. "_" .. pool.next, position = { x = 0, y = 0, z = -1000 },
		flags = ENTITY_FLAG_NO_SAVE,
		properties = { object_Model = model, Physics = { bPhysicalize = true, bRigidBody = true, bPushableByPlayers = false, Density = -1, Mass = 0.02 } },
	})
	e:SetFlags(ENTITY_FLAG_NO_SAVE, 0)   -- never into a save
	pool[pool.next] = e.id
	return e
end

-- the gun's own axes at the shot (right, barrel, up), from its muzzle bone: Ready or Not throws from a point on the
-- gun mesh, so the throw turns with the gun, not with the camera
local function gunAxes(self)
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return nil end
	local ok, _, ld = pcall(self.MuzzleLocal, self, vm)
	if not ok then return nil end
	local X, Y, Z = vm:GetDirectionVector(0), vm:GetDirectionVector(1), vm:GetDirectionVector(2)
	local f = vnorm(vadd(vadd(vscale(X, ld.x), vscale(Y, ld.y)), vscale(Z, ld.z)))
	local r = vnorm(vcross(f, Z))
	return r, f, vcross(r, f)
end

function Deagle:EjectCase()
	local port = self:VmBone(self.gun.bones.brass)
	if not port then return end
	local c = self.cfg
	try("eject case", function()
		local E = c.eject
		local r, _, u = gunAxes(self)
		if not r then return end
		local case = self:CaseEntity()
		-- Ready or Not's BulletShell: linear and angular damping 1 (KCD2 damps both with one number, in contact and in
		-- free fall; its own free-flight damping is ~0.1 /s, MODLOG 2026-10-13). Every throw: deagle_set takes at once
		case:SetPhysicParams(PHYSICPARAM_SIMULATION, { damping = E.damping, freefall_damping = E.damping })
		case:SetWorldPos(port)
		local a = case:GetAngles()
		a.x, a.z = a.x + math.rad(math.random(-E.nudge, E.nudge)), a.z + math.rad(math.random(-E.nudge, E.nudge))
		case:SetAngles(a)
		local out = vadd(vscale(r, math.cos(math.rad(E.angle))), vscale(u, math.sin(math.rad(E.angle))))
		local side = 0
		local okP, pv, pr = pcall(function() local pl = self:Player() return pl:GetVelocity(), pl:GetDirectionVector(0) end)
		if okP and type(pv) == "table" and type(pr) == "table" then side = vdot(pv, pr) end
		local v = vscale(out, math.max(E.base, side) * (E.times[1] + math.random() * (E.times[2] - E.times[1])))
		local s = math.random(0, E.spin)
		local w = { x = s, y = s, z = s }
		local wl = vlen(w)
		if wl > E.spinCap then w = vscale(w, E.spinCap / wl) end
		if self.native then self:Native(string.format("casespin %s %.2f", entityHex(case.id), E.spinCap)) end
		case:SetVelocityEx(v, w)
		local ghost = E.ghost and true or false
		caseGhost(case, ghost)
		self.casesFlying = self.casesFlying or {}
		self.casesFlying[case.id] = { t = System.GetCurrTime(), bounces = 0, ev = self.gun.ron and self.gun.ron.shell, ghost = ghost }
		-- Ready or Not's two timers (game time, so they run with the gun put away too): physics off, then back to
		-- the pool. A casing thrown again before then has a new throw: the old timers leave it alone.
		local id, throw = case.id, {}
		self.caseThrow = self.caseThrow or {}
		self.caseThrow[id] = throw
		Script.SetTimer(math.floor(E.freeze * 1000), function()
			local e = Deagle.caseThrow[id] == throw and System.GetEntity(id)
			if e then e:EnablePhysics(false) end
		end)
		Script.SetTimer(math.floor(E.life * 1000), function()
			local e = Deagle.caseThrow[id] == throw and System.GetEntity(id)
			if e then e:Hide(1) end
		end)
		self:SpawnFx(c.portFx, port, out, 1.0)
	end)
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
