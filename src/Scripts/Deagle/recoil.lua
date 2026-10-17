-- Recoil (Claude 4): physics recoil, felt kick, spray pattern, gun kick and camera recoil.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local try = U.try
-- A kick never turns the view past straight up or down: at 1000 rounds a second ~34 Deagle kicks a frame (~240 deg)
-- flipped it over the top to 17 deg up (deagle_stress 1000, 2026-10-17). 88 deg: just short of the pole, where yaw is lost.
local PITCH_MAX = math.rad(88)
-- (a view already past it, looking further up than that, isn't pulled back)
local function clampPitch(x, from)
	return math.max(math.min(-PITCH_MAX, from), math.min(math.max(PITCH_MAX, from), x))
end

function Deagle:Recoil()
	local phys = self:ShotPhysics()
	if phys then return self:PhysicsRecoil(phys) end
	local p = self:Player()
	local k = self.cfg.recoil
	try("view kick", function()
		local d = System.GetViewCameraDir()
		local pitch = math.asin(math.max(-1, math.min(1, d.z)))
		local yaw = math.atan2(-d.x, d.y)
		local up = math.rad(self.cfg.kickPitch + self.cfg.kickPitchVar * (math.random() * 2 - 1)) * k * (self.state.aiming and 0.7 or 1)
		local side = math.rad(self.cfg.kickYaw) * k * (math.random() * 2 - 1)
		p.actor:PlayerSetViewAngles({ x = clampPitch(pitch + up, pitch), y = 0, z = yaw + side })
	end)
	try("recoil", function()
		p.actor:SetViewShake({ x = math.rad(2.2) * k, y = math.rad(0.6) * k, z = math.rad(0.4) * k },
			{ x = 0.004 * k, y = 0.02 * k, z = 0.006 * k }, 0.16, 1 / 30, 0.2)
	end)
end

-- The gun-kick springs: each kick k at time ti holds k K e^(-zeta w (t - ti)) sin(wd (t - ti)) of it at t. Their sum is
-- K times the imaginary part of one complex number S = sum of k e^((-zeta w + i wd)(t - ti)): a kick adds k, and time
-- dt turns S by wd dt and shrinks it by e^(-zeta w dt). One number per axis instead of every kick of the last 0.6 s:
-- at 1000 rounds a second the list held ~600 kicks and GunKickNow took 197 us a shot, 6.7 ms a frame (deagle_stress
-- 1000 sky prof, 2026-10-17); the same values to 6e-5 deg over 20000 random kicks. Settled after 0.6 s (e^-15).
local KICK_W, KICK_ZETA = 28, 50 / (2 * 28)
local KICK_WD = KICK_W * math.sqrt(1 - KICK_ZETA * KICK_ZETA)
local KICK_K = KICK_W * math.exp(1) / KICK_WD
local function kickAdvance(g, now)
	local dt = now - g.t
	if dt < 0 or dt > 0.6 then g.pr, g.pi, g.yr, g.yi = 0, 0, 0, 0   -- ahead of the clock (kept over a load) / settled
	elseif dt > 0 then
		local d = math.exp(-KICK_ZETA * KICK_W * dt)
		local c, s = d * math.cos(KICK_WD * dt), d * math.sin(KICK_WD * dt)
		g.pr, g.pi = g.pr * c - g.pi * s, g.pr * s + g.pi * c
		g.yr, g.yi = g.yr * c - g.yi * s, g.yr * s + g.yi * c
	end
	g.t = now
end

-- Kick from the physics. The camera climbs by the muzzle flip and kicks sideways by a share of it (a learnable
-- pattern per gun, CS2-style: the same random numbers for the same shot of a string). The elastic hold takes back what
-- fits in its range and keeps the rest; a shot fired while the last one is still springing back finds less room, so
-- fast fire climbs. The gun model is pushed back, flipped, kicked sideways and twisted by the plugin's springs.
function Deagle:PhysicsRecoil(phys)
	local m, s = self.cfg.physics, self.state
	local ph = self.gun.physics or self.gunPhysics[self.gun.cs2]
	local aim = s.aiming and m.adsMult or 1
	local n = self.sprayN or 0; self.sprayN = n + 1
	local feltFlip, feltVel = self:FeltKick(phys)
	local up = m.camPerFlip * feltFlip * (1 + m.vary * self:SprayRand(n, 1)) * aim * self.cfg.recoil
	local side = up * ((ph.side or 0) + m.sideSigma * self:SprayRand(n, 2))
	local range = m.holdRange * m.holds[ph.hold] * (s.aiming and m.adsHold or 1)
	local r = self.camRecoil
	if not r then   -- a new string: its clock starts now (a stale one made the first frame 0.1 s: half the kick gone, no rise)
		r = { pitch = 0, yaw = 0, applied = { pitch = 0, yaw = 0 }, perm = { pitch = 0, yaw = 0 } }
		self.camRecoil, self.camRecoilLast = r, System.GetCurrTime()
	end
	r.rate = m.returnRate * math.sqrt(m.returnInertia / phys.inertia)
	local back = math.min(m.holdMax * up, math.max(0, range - r.pitch))
	r.pitch, r.perm.pitch = r.pitch + back, r.perm.pitch + up - back
	local sideBack = math.min(m.holdMax * math.abs(side), math.max(0, range * m.holdYaw - math.abs(r.yaw)))
	if side < 0 then sideBack = -sideBack end
	r.yaw, r.perm.yaw = r.yaw + sideBack, r.perm.yaw + side - sideBack
	self.camRecoilT = System.GetCurrTime()
	if self.native then
		local flip = m.gunFlipPerFlip * feltFlip * aim * self.cfg.recoil
		local yaw = up > 0 and flip * side / up or 0
		self:Native(string.format("kick %.4f %.3f %.3f %.3f", m.gunBackPerVel * feltVel * aim * self.cfg.recoil, flip, yaw, m.gunRollPerYaw * yaw))
		local g = self.gunKick or { t = self.camRecoilT, pr = 0, pi = 0, yr = 0, yi = 0 }
		self.gunKick = g
		kickAdvance(g, self.camRecoilT)
		g.pr, g.yr = g.pr + flip, g.yr + yaw
	end
end

-- the muzzle flip and recoil speed as they're felt (cfg.physics.kickExponent): the reference gun keeps its numbers,
-- smaller ones are lifted toward it
function Deagle:FeltKick(phys)
	local m = self.cfg.physics
	return m.kickRefFlip * (phys.flip / m.kickRefFlip) ^ m.kickExponent, m.kickRefVel * (phys.recoilVel / m.kickRefVel) ^ m.kickExponent
end

-- the same pseudo-random number for the same gun, shot of the string and channel: a learnable spray (CS2-style).
-- Roughly normal (sum of three uniforms), clamped to +-2.
function Deagle:SprayRand(n, ch)
	local seed = 0
	for i = 1, #self.gun.id do seed = (seed * 31 + string.byte(self.gun.id, i)) % 1000003 end
	local sum = 0
	for k = 1, 3 do
		local x = math.sin(seed * 12.9898 + n * 78.233 + ch * 37.719 + k * 4.581) * 43758.5453
		sum = sum + (x - math.floor(x))
	end
	return math.max(-2, math.min(2, (sum - 1.5) * 2))
end

-- where the barrel points right now, relative to the camera (deg up, deg right): the plugin's gun-kick springs
-- (w = 28 rad/s, damping 0.89) still hold part of each recent kick; the next bullet leaves along it
function Deagle:GunKickNow()
	local g = self.gunKick
	if not g then return 0, 0 end
	kickAdvance(g, System.GetCurrTime())
	return KICK_K * g.pi, KICK_K * g.yi
end

-- every frame: move the view by how much the recoil offset changed (never sets the aim outright)
function Deagle:CamRecoilTick()
	local r = self.camRecoil
	if not r then return end
	local now = System.GetCurrTime()
	local dt = math.min(0.1, now - (self.camRecoilLast or now)); self.camRecoilLast = now
	if dt <= 0 then return end
	local m = self.cfg.physics
	local decay = math.exp(-(r.rate or m.returnRate) * dt)
	r.pitch, r.yaw = r.pitch * decay, r.yaw * decay
	local tp, ty = r.perm.pitch + r.pitch, r.perm.yaw + r.yaw
	local k = 1 - math.exp(-m.riseRate * dt)
	local dp, dy = (tp - r.applied.pitch) * k, (ty - r.applied.yaw) * k
	if math.abs(dp) < 1e-4 and math.abs(dy) < 1e-4 then
		if math.abs(r.pitch) < 1e-3 and math.abs(r.yaw) < 1e-3 then self.camRecoil, self.sprayN = nil, 0 end   -- settled: a new string
		return
	end
	local p = self:Player()
	try("view kick", function()
		local d = System.GetViewCameraDir()
		local pitch = math.asin(math.max(-1, math.min(1, d.z)))
		local yaw = math.atan2(-d.x, d.y)
		local np = clampPitch(pitch + math.rad(dp), pitch)
		dp = math.deg(np - pitch)   -- only what the pole let through counts, or the return would take the view below its start
		p.actor:PlayerSetViewAngles({ x = np, y = 0, z = yaw + math.rad(dy) })
	end)
	r.applied.pitch, r.applied.yaw = r.applied.pitch + dp, r.applied.yaw + dy
end
