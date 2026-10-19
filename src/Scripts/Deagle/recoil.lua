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
	local ron = self.gun.ronStats and self.ronStats and self.ronStats[self.gun.ronStats]
	if ron then return self:RonRecoil(ron) end
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

-- ---- Ready or Not's own recoil, for the guns imported from it (gun.ronStats: their blueprint's numbers, 1:1), read
-- from its code (docs/ron/ron_recoil_code.md; addresses there). The camera kick lives here; the gun's procedural
-- recoil, its mesh-space offsets and the fire camera shake are the plugin's (ronfire / ronshake).
-- On a shot (OnWeaponFired + GetRecoil): the kick = the next pattern entry (pitch up, yaw right, roll; the index never
-- resets and a wrap returns entry 0 without moving on: e0 e1 e2, e0 e0 e1 e2, ...) x FirstShotRecoil while the
-- first-shot flag is up (always while FirstShotResetTime > 0: an inverted test in its code) x (1 + n x
-- RecoilMultiplierPitch / Yaw), n = the shot's number in the burst (0 for the first, capped at AmmoMax), x
-- ADSRecoilMultiplier when aiming, x its walk-speed factor (1: KCD2 has no walk-speed setting), into a pending kick.
-- A burst ends 2 x FireRate after its last shot (+0.05 s in other fire modes): what is still pending goes, and
-- RecoilReturnPercentage of what was applied comes back at RecoilReturnInterpSpeed (the Grizzly: nothing). No
-- randomness. RecoilInterpSpeed, RecoilReturnRate and VelocityRecoilMultiplier are read by nothing in its code.
function Deagle:RonRecoil(r)
	local now, s = System.GetCurrTime(), self.state
	local k = self.ronKick
	if not k then   -- a new string: its clock starts now (a stale one would hand the first frame 0.1 s of kick)
		k = { pp = 0, py = 0, pr = 0, ap = 0, ay = 0, ar = 0, shots = 0, ret = false }
		self.ronKick, self.ronKickLast = k, now
	end
	self:RonBurstEnd(now)
	if k.shots == 0 then k.ap, k.ay, k.ar = 0, 0, 0 end
	k.ret = false
	local pat = r.RecoilPattern or {}
	self.ronPatternAt = self.ronPatternAt or {}
	local i, used = self.ronPatternAt[self.gun.id] or 0, 1
	if i < 0 or i >= #pat then i = 0 else i, used = i + 1, i + 1 end
	local e = pat[used]
	self.ronPatternAt[self.gun.id] = i
	e = e or { 0, 0, 0 }
	self.ronFirstGone = self.ronFirstGone or {}
	local first = r.FirstShotResetTime > 0 or not self.ronFirstGone[self.gun.id]
	self.ronFirstGone[self.gun.id] = true
	local fr = first and r.FirstShotRecoil or 1
	local n = math.max(0, math.min(k.shots, r.AmmoMax or r.Capacity or 0))
	local m = (s.aiming and r.ADSRecoilMultiplier or 1) * self.cfg.recoil
	local kp, ky = e[1] * fr * (1 + n * r.RecoilMultiplierPitch) * m, e[2] * fr * (1 + n * r.RecoilMultiplierYaw) * m
	k.pp, k.py, k.pr = k.pp + kp, k.py + ky, k.pr + (e[3] or 0) * fr * m
	k.shots = k.shots + 1
	k.burstEnd = now + 2 * r.FireRate + (self:FireMode() ~= "semi" and 0.05 or 0)
	self.lastRonShot = { pitch = kp, yaw = ky, n = n, entry = used }
	if self.native then self:RonFireNative(r, kp, ky, k.burstEnd - now) end
end

-- the plugin's part of a shot (native/src/ronvm.cpp): the procedural recoil's random numbers (its rand(): rx, rz in
-- +-RecoilRandomness, ry in +-RecoilRandomness / 2), the kick just added (its copy of the pending kick pushes the gun
-- offset), the burst window, weapon_right in the viewmodel's own space (the recoil turns about it) and the camera
-- shake's phases (random where the shake asks for it, else 0)
function Deagle:RonFireNative(r, kp, ky, window)
	if not (self.nativeCaps and self.nativeCaps.ronvm) then return self:RonParams(self.gun) end   -- says why, once
	if self.ronParamsFor ~= self.gun.id then self:RonParams(self.gun) end   -- the handshake ran before the plugin said it can
	local rnd = r.RecoilRandomness or 0
	local rx, ry, rz = (math.random() * 2 - 1) * rnd, (math.random() - 0.5) * rnd, (math.random() * 2 - 1) * rnd
	local pv = { x = 0, y = 0, z = 0 }
	local vm = self.vmId and System.GetEntity(self.vmId)
	local hand = self:VmBone(self.gun.bones.hand or "weapon_right")
	if vm and hand then
		local d = U.vsub(hand, vm:GetWorldPos())
		pv = { x = U.vdot(d, vm:GetDirectionVector(0)), y = U.vdot(d, vm:GetDirectionVector(1)), z = U.vdot(d, vm:GetDirectionVector(2)) }
	elseif not self.ronNoHand then
		self.ronNoHand = true
		U.log("$4Ready or Not recoil: no " .. tostring(self.gun.bones.hand or "weapon_right") .. " bone on the viewmodel: it turns about the viewmodel's origin")
	end
	local S = self.ronShakes and self.ronShakes[r.FireCameraShake or ""]
	local ph = {}
	local axes = S and { S.Rot[1], S.Rot[2], S.Rot[3], S.Loc[1], S.Loc[2], S.Loc[3], S.FOV } or {}
	for i = 1, 7 do ph[i] = axes[i] and axes[i][3] and math.random() * 2 * math.pi or 0 end
	self:Native(string.format("ronfire %.5f %.5f %.5f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f %.4f",
		rx, ry, rz, kp, ky, window, pv.x, pv.y, pv.z, ph[1], ph[2], ph[3], ph[4], ph[5], ph[6], ph[7]))
end

-- a Ready or Not gun's numbers for the plugin (ronp: ronvm.cpp's keys); any other gun turns it off. From UseGun and
-- the plugin handshake (MotionParams)
local function quatRot(q)   -- Unreal's FQuat::Rotator: (Pitch, Yaw, Roll) in degrees
	if not q then return { 0, 0, 0 } end
	local X, Y, Z, W = q.x, q.y, q.z, q.w
	local sing = Z * X - W * Y
	return { math.deg(math.asin(math.max(-1, math.min(1, 2 * sing)))), math.deg(math.atan2(2 * (W * Z + X * Y), 1 - 2 * (Y * Y + Z * Z))),
		math.deg(math.atan2(-2 * (W * X + Y * Z), 1 - 2 * (X * X + Y * Y))) }
end
function Deagle:RonParams(gun)
	if not self.native then return end
	local r = gun and gun.ronStats and self.ronStats and self.ronStats[gun.ronStats]
	if not (self.nativeCaps and self.nativeCaps.ronvm) then   -- an older plugin: only the camera kick (Lua's)
		if r and self.nativeCaps and not self.ronOldPlugin then
			self.ronOldPlugin = true
			U.log("$4Ready or Not recoil: the installed plugin is older than its recoil (no ronvm): only the camera kick, no gun recoil or shake")
		end
		return
	end
	self:Native("ronp reset")
	self.ronParamsFor = gun and gun.id
	if not r then return end
	local P, S = self.ronPlayer, self.ronShakes[r.FireCameraShake or ""]
	local function t3(tr) local t = tr and tr.Translation or {} return { t[1] or 0, t[2] or 0, t[3] or 0 } end
	local hipT, hipR = t3(r.MeshspaceTransform_Default), quatRot(r.MeshspaceTransform_Default and r.MeshspaceTransform_Default.Rotation)
	local adsT, adsR = t3(r.MeshspaceTransform_Aiming), quatRot(r.MeshspaceTransform_Aiming and r.MeshspaceTransform_Aiming.Rotation)
	local pb, rb = r.RecoilPositionBuildup, r.RecoilRotationBuildup
	local v = {
		damp = r.RecoilDampStrength, bdamp = r.RecoilBuildupDampStrength, ftime = r.bCalculateProcRecoil and r.RecoilFireTime or 0,
		fstr = r.RecoilFireStrength, astr = r.RecoilAngleStrength, pbx = pb[1], pby = pb[2], pbz = pb[3], rbp = rb[1], rby = rb[2], rbr = rb[3],
		fads = r.RecoilFireADSModifier, aads = r.RecoilAngleADSModifier, bads = r.RecoilBuildupADSModifier, hasb = r.RecoilHasBuildup and 1 or 0,
		hipf = hipT[1], hipr = hipT[2], hipu = hipT[3], hipp = hipR[1], hipy = hipR[2], hipl = hipR[3],
		adsf = adsT[1], adsr = adsT[2], adsu = adsT[3], adsp = adsR[1], adsy = adsR[2], adsl = adsR[3],
		interp = P.MeshInterp, lor = P.OffsetMin[1], lof = P.OffsetMin[2], lou = P.OffsetMin[3],
		hir = P.OffsetMax[1], hif = P.OffsetMax[2], hiu = P.OffsetMax[3],
		kmf = P.KickMove[1], kmr = P.KickMove[2], kmu = P.KickMove[3], kxf = P.KickMoveMax[1], kxr = P.KickMoveMax[2], kxu = P.KickMoveMax[3],
		move1p = P.MoveMul1P, adsback = P.AdsKickBack, adsbackmax = P.AdsKickBackMax,
		krate = P.KickRate, kret = r.RecoilReturnPercentage, kretspd = r.RecoilReturnInterpSpeed,
	}
	if self.nativeCaps.lowready then   -- near a wall (viewmodel.lua LowReadyTick sends how near)
		local bk = t3(r.MeshspaceTransform_Back)
		v.bkf, v.bkr, v.bku = bk[1], bk[2], bk[3]
		self.wallSent = nil                -- the reset forgot it: sent again by the next tick
	end
	if S then
		v.sdur, v.sin, v.sout = S.Duration, S.BlendIn, S.BlendOut
		local axes = { S.Rot[1], S.Rot[2], S.Rot[3], S.Loc[1], S.Loc[2], S.Loc[3], S.FOV }
		for i = 1, 7 do v["sa" .. (i - 1)], v["sf" .. (i - 1)] = axes[i][1], axes[i][2] end
	elseif r.FireCameraShake then
		U.log("$4Ready or Not recoil: no shake " .. tostring(r.FireCameraShake) .. " in guns/ron_stats.lua (tools/ron/ron_stats.py SHAKES)")
	end
	local keys = {}
	for key in pairs(v) do keys[#keys + 1] = key end
	table.sort(keys)
	for _, key in ipairs(keys) do self:Native(string.format("ronp %s %.5f", key, v[key])) end
	self:Native("ronp on 1")
end
function Deagle:RonBurstEnd(now)
	local k = self.ronKick
	if not (k and k.burstEnd and now >= k.burstEnd) then return end
	local r = self.ronStats[self.gun.ronStats] or {}
	local back = r.RecoilReturnPercentage or 0
	k.pp, k.py, k.pr = -back * k.ap, -back * k.ay, -back * k.ar
	k.ap, k.ay, k.ar, k.shots, k.burstEnd = 0, 0, 0, 0, nil
	k.ret = true
end
-- every frame: the pending kick goes onto the view at a constant 7 a second (its kick phase, step = pending x 7 dt),
-- or back at RecoilReturnInterpSpeed after a burst; the view turns by the step (control rotation: aim and view)
function Deagle:RonKickTick()
	local k = self.ronKick
	if not k then return end
	local now = System.GetCurrTime()
	local dt = math.min(0.1, now - (self.ronKickLast or now)); self.ronKickLast = now
	self:RonBurstEnd(now)
	if dt <= 0 then return end
	local r = self.ronStats[self.gun.ronStats] or {}
	local a = (k.ret and (r.RecoilReturnInterpSpeed or 0) or self.ronPlayer.KickRate) * dt
	local dp, dy = k.pp * a, k.py * a
	k.pp, k.py, k.pr = k.pp - dp, k.py - dy, k.pr - k.pr * a
	if not k.ret then k.ap, k.ay = k.ap + dp, math.max(k.ay + dy, 0) end
	if k.shots == 0 and not k.burstEnd and math.abs(k.pp) < 1e-5 and math.abs(k.py) < 1e-5 then self.ronKick = nil end
	if math.abs(dp) < 1e-6 and math.abs(dy) < 1e-6 then return end
	local p = self:Player()
	try("view kick", function()
		local d = System.GetViewCameraDir()
		local pitch = math.asin(math.max(-1, math.min(1, d.z)))
		local yaw = math.atan2(-d.x, d.y)
		-- Ready or Not's yaw > 0 turns right; KCD2's view yaw grows to the left
		p.actor:PlayerSetViewAngles({ x = clampPitch(pitch + math.rad(dp), pitch), y = 0, z = yaw - math.rad(dy) })
	end)
end

-- every frame: move the view by how much the recoil offset changed (never sets the aim outright)
function Deagle:CamRecoilTick()
	if self.ronKick then self:RonKickTick() end
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
