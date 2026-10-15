-- Sniper Elite V2's kill cam (docs/se2_killcam.md): a round on its way to a living NPC slows the game to 3 %, the
-- camera rides behind the bullet (1.1 m, easing to 2 m; FOV 10 deg easing to 30), the hit and the way out play at 10 %,
-- then the game is back at its own speed. Numbers read out of the game's data (Common.asr, the default cam at seed 0:
-- Camera.Offset -1.1 -> -2, FieldOfView 10 -> 30, Duration 2 / ImpactTime 1 / ExitTime 1; the stage cam 47955e: TimeScale
-- PreHit 0.03, PostExit 0.1). TimeScale.Internal isn't stored there: it takes PreHit's value (its Inherit flag).
-- Loaded at the end of ballistics.lua; its hooks are Deagle:KillCamStep (every frame a round moves) and
-- Deagle:KillCamHit (when a round hits something). The free camera is the plugin's (native/src/freecam.cpp): `on` starts
-- it at Henry's eye, `nudge` moves it by metres, `rot` aims it; we follow our own position since there's no absolute set.
local U = Deagle.U
local log, vadd, vsub, vscale, vlen, vnorm = U.log, U.vadd, U.vsub, U.vscale, U.vlen, U.vnorm
local RAD = math.pi / 180

local DEFAULTS = {
	on = true,
	minDist = 40,                 -- m the round has flown: nearer kills get no kill cam (a guess: the game scores its cams)
	killOnly = true,              -- only a round that will kill (its damage reaches the NPC's health, or a head height hit)
	lead = 1.0,                   -- s of cam time before the impact (Duration.ImpactTime)
	after = 1.0,                  -- s after the way out (Duration - ExitTime)
	internalTime = 0,             -- s inside the body (Duration.ExitTime - ImpactTime = 0 in the default cam)
	speed = { pre = 0.03, internal = 0.03, post = 0.1 },   -- TimeScale.PreHit / Internal / PostExit
	offset = { -1.1, -2 },        -- m along the bullet's path (negative = behind it), start -> end
	fov = { 10, 30 },             -- degrees, start -> end
	organs = true,                -- at the hit: the NPC's body is hidden and its organs shown (Sniper Elite's X-ray, as models)
}
local function copy(v)
	if type(v) ~= "table" then return v end
	local o = {}
	for k, x in pairs(v) do o[k] = copy(x) end
	return o
end
-- deagle_reset puts back what cfgDefaults holds and drops the rest
Deagle.cfgDefaults = Deagle.cfgDefaults or {}
Deagle.cfgDefaults.killCam = copy(DEFAULTS)
Deagle.cfg.killCam = Deagle.cfg.killCam or copy(DEFAULTS)

function Deagle:KillCamSpeed(v)
	pcall(System.SetCVar, "t_Scale", v)
	self:Native(string.format("speed %.3f", v))   -- our sounds slow down with the game's
end

function Deagle:KillCamEnd(why)
	local kc = self.kc
	if not kc then return end
	self.kc = nil
	if kc.camSent then
		self:Native("freecam rotoff")
		self:Native("freecam off")
		self:Native(string.format("freecam max %d", kc.max0))
	end
	if kc.fov0 then pcall(System.SetViewCameraFov, kc.fov0) end
	if kc.organs then
		if self.RemoveOrgans then self:RemoveOrgans() end
		if kc.target then pcall(kc.target.Hide, kc.target, 0) end   -- the body back
	end
	self:SlowMo(self.slowOn)   -- t_Scale and the plugin's clock back to the player's own speed
	log(string.format("kill cam end (%s) after %.2f s", tostring(why), kc.t))
end
function Deagle:KillCamAbort() self:KillCamEnd("abort") end

-- will this round kill what it's about to hit? (its wound damage against the NPC's health, or a hit at head height)
function Deagle:KillCamLethal(e, h, r)
	local speed = vlen(r.vel)
	local energy = 0.5 * r.phys.bullet * speed * speed
	local dmg = self:Wound(r.phys, r.dist + (h.dist or 0), energy)
	local hp, head = 100, false
	pcall(function() hp = e.actor:GetHealth() end)
	pcall(function() head = h.pos.z - e:GetWorldPos().z > 1.45 end)
	return head or hp <= dmg
end

-- every frame a round moves (StepRound; done = it ended this frame)
function Deagle:KillCamStep(r, done)
	if Deagle._kcWrappers.FreecamState ~= Deagle.FreecamState or Deagle._kcWrappers.OnGameplayStarted ~= Deagle.OnGameplayStarted then
		self:KillCamInstall()
	end
	local kc = self.kc
	if kc then
		if kc.r == r then kc.roundDone = done end   -- the cam's own frame loop (KillCamFrame) reads it
		return
	end
	local c = self.cfg.killCam
	if not c or not c.on or done or r.kcNo or (r.hops or 0) > 0 or r.tumble or self.slowOn or self.fc then return end
	if r.dist < c.minDist then return end
	local speed = vlen(r.vel)
	if speed < 50 then return end
	r.kcFrame = (r.kcFrame or 0) + 1
	if r.kcFrame % 2 == 0 then return end                  -- every other frame: the look-ahead is longer than two frames' flight
	if r.vel.z > 0.2 * speed then return end              -- climbing: a shot at the sky has nobody to look for
	-- the stretch the camera will have: the speed the bullet will fly at, over the lead time
	local reach = speed * c.speed.pre * c.lead * 1.25 + speed / 60   -- + a frame of flight (it looks every other frame)
	local p, hits = self:Player(), {}
	local ok, n = pcall(Physics.RayWorldIntersection, r.pos, vscale(r.vel, reach / speed), 1, ent_all, p and p.id, r.skip, hits)
	local h = ok and n and n > 0 and hits[1]
	local e = h and h.entity
	if not (e and e.soul and e ~= p) then return end
	local dead = false
	pcall(function() dead = e.actor:IsDead() or e.actor:GetHealth() <= 0 end)
	if dead then return end
	if c.killOnly and not self:KillCamLethal(e, h, r) then r.kcNo = true return end
	local fov0
	pcall(function() fov0 = System.GetViewCameraFov() end)
	self.kc = { r = r, target = e, phase = "pre", t = 0, last = System.GetCurrTime(), fov0 = fov0, max0 = 500,
		speed = c.speed.pre, dir = vnorm(r.vel), camSent = true }
	self:KillCamSpeed(c.speed.pre)
	self:Native("freecam max 2000")
	self:Native("freecam on")
	local token = {}
	self.kc.token = token
	Script.SetTimer(0, function() Deagle:KillCamFrame(token) end)
	log(string.format("kill cam: %s at %.0f m, speed x%g", tostring(e.GetName and e:GetName() or e.id), r.dist + h.dist, c.speed.pre))
end

-- every frame while the cam runs (the round itself stops being stepped once it has landed)
function Deagle:KillCamFrame(token)
	local kc = self.kc
	if not kc or kc.token ~= token then return end
	self:KillCamTick(kc, kc.r, kc.roundDone)
	if self.kc == kc then Script.SetTimer(0, function() Deagle:KillCamFrame(token) end) end
end

function Deagle:KillCamTick(kc, r, done)
	local c = self.cfg.killCam
	local now = System.GetCurrTime()
	local dt = now - kc.last
	kc.last = now
	if dt > 0 then kc.t = kc.t + dt / math.max(0.01, kc.speed) end   -- cam seconds: game time over the game's speed
	if kc.phase == "internal" and kc.t - kc.hitT >= c.internalTime then
		kc.phase, kc.exitT, kc.speed = "post", kc.t, c.speed.post
		self:KillCamSpeed(kc.speed)
	end
	if kc.phase == "post" and kc.t - kc.exitT >= c.after then return self:KillCamEnd("done") end
	if done and not kc.hit then return self:KillCamEnd("the round ended without a hit") end
	if kc.t > c.lead + c.internalTime + c.after + 4 then return self:KillCamEnd("timeout") end
	if not kc.camOn then return end   -- until the plugin says its free camera is on
	local u = math.min(1, kc.t / (c.lead + c.internalTime + c.after))
	local off = c.offset[1] + (c.offset[2] - c.offset[1]) * u
	if vlen(r.vel) > 1 then kc.dir = vnorm(r.vel) end
	local want = vadd(r.pos, vscale(kc.dir, off))
	local d = vsub(want, kc.pos)
	self:Native(string.format("freecam nudge %.4f %.4f %.4f", d.x, d.y, d.z))
	kc.pos = want
	self:Native(string.format("freecam rot %.5f %.5f", math.asin(math.max(-1, math.min(1, kc.dir.z))), math.atan2(-kc.dir.x, kc.dir.y)))
	pcall(System.SetViewCameraFov, (c.fov[1] + (c.fov[2] - c.fov[1]) * u) * RAD)
end

-- the round hit something (MoveRound, after ApplyHit)
function Deagle:KillCamHit(hit, r)
	local kc = self.kc
	if not kc or kc.r ~= r or kc.hit then return end
	local c = self.cfg.killCam
	kc.hit, kc.hitT, kc.phase, kc.speed = true, kc.t, "internal", c.speed.internal
	self:KillCamSpeed(kc.speed)
	-- only on a person: the organs are a human's, at human size and places (an animal has none of them)
	if c.organs and self.ShowOrgans and kc.target and hit.entity == kc.target and kc.target.human then
		local n = self:ShowOrgans(kc.target, hit.bodyPart or "torso")
		if n > 0 then
			kc.organs = true
			pcall(kc.target.Hide, kc.target, 1)   -- the body away, the organs in its place
			log(string.format("kill cam: %d organs shown on %s", n, tostring(kc.target.GetName and kc.target:GetName() or kc.target.id)))
		end
	end
end

-- Two of deagle_main's functions are wrapped (no edit of that file). Wrapped at load and again at the first round's step,
-- so it holds whichever order the files load in; a dev reload that makes new originals is wrapped anew.
Deagle._kcWrappers = Deagle._kcWrappers or {}
local function wrap(name, make)
	local cur = Deagle[name]
	if type(cur) ~= "function" or Deagle._kcWrappers[name] == cur then return end   -- not defined yet, or already ours
	local w = make(cur)
	Deagle[name], Deagle._kcWrappers[name] = w, w
end
function Deagle:KillCamInstall()
	-- the plugin tells Lua when its free camera is on / off (deagle_freecam_state); while a kill cam drives it the free-look
	-- code (and its aim line) stays out of it
	wrap("FreecamState", function(orig)
		return function(self, on)
			local kc = self.kc
			if not kc then return orig(self, on) end
			if Deagle.Arg(on) ~= "0" then
				local e = System.GetViewCameraPos()
				kc.camOn, kc.pos = true, { x = e.x, y = e.y, z = e.z }   -- it starts at Henry's eye
			else
				kc.camSent = false                 -- it went off by itself (a key, a load): nothing to switch off
				self:KillCamEnd("free camera off")
			end
		end
	end)
	-- a save load: its timers and rounds are gone, the game's speed must not stay at 3 %
	wrap("OnGameplayStarted", function(orig)
		return function(self, ...)
			self:KillCamAbort()
			return orig(self, ...)
		end
	end)
end
Deagle:KillCamInstall()
