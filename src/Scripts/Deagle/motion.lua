-- Weapon-movement inputs for the native motion layer (native/src/motion.cpp; docs/weapon_movement_design.md).
-- Deagle:MotionTick() runs every frame (from VmTick) and sends the plugin what Lua knows better than the camera:
--   vel <x> <y> <z>          WORLD velocity m/s (player:GetVelocity: no head bob), lightly smoothed (velSmooth s), sent
--                            EVERY frame; the plugin turns it camera-relative with the current camera every sub-step
--                            ([DN] lines are swallowed by the plugin's LogAlways hook, so this doesn't touch kcd.log)
--   air 0|1                  in the air (player.actor:IsFlying), on change
--   stance 0|1               crouched = eye under cfg.accuracy.crouchEye +- crouchBand (the CsAccuracy rule), on change
--   dodge <side> <strength>  KCD2 gives no dodge flag: a sideways (or backwards) speed spike, see motionCfg.dodge*
-- Nothing is sent while deagle_motion is 0. Deagle:MotionParams(gun) sends the gun's tunables (mparams <key> <value>).

Deagle.motionCfg = {
	velSmooth = 0.08,                                                    -- s (exponential smoothing of GetVelocity)
	dodgeSpeed = 3.5, dodgeRise = 3.0, dodgeWindow = 0.2, dodgeCooldown = 0.6, dodgeSprintFwd = 4.0,
}

-- per-gun tunables (motion.h MotionParams). Defaults = the plugin's (RoN G19); a gun's own `motion` table overrides.
-- The Deagle takes RoN's .357 Python numbers (heavier: more aim-location drag, softer hang, bigger landing).
Deagle.motionDefaults = {
	drag_aim_rot = 0.6, drag_aim_loc = 0.4, drag_strafe_rot = 0.4, drag_strafe_loc = 0.2,
	move_scale_x = 10, move_scale_y = 10, move_scale_z = 5, move_rot_p = 60, mesh_interp = 55,
	hang_k = 100, hang_c = 8, hang_ads = 0.3, sprint_ron = 1,
	sprint_x = -0.008, sprint_y = -0.031, sprint_z = -0.118, sprint_pitch = -8.7, sprint_roll = -35.5, sprint_yaw = 1.2,
	jump = 1, land = 1, crouch = 1, dodge = 1, bob = 1, test_roll = 0,
}
Deagle.motionGuns = {
	deagle = { drag_aim_loc = 0.875, drag_strafe_loc = 0.05, move_rot_p = 35, hang_k = 80, land = 1.2 },
	glock_17 = {},
}
Deagle.motionOverride = Deagle.motionOverride or {}   -- deagle_mparam, kept across gun switches until the game restarts

-- a Ready or Not gun's own movement numbers (gun.ronStats: guns/ron_stats.lua), 1:1 onto the keys they are in
-- motion.h (docs/weapon_movement_design.md). Not mapped yet: LazySpringStrength / ADS (how it scales the hang spring
-- isn't decoded: hang_* keep the defaults) and its sprint pose (comes from its own sprint clips, not its blueprint)
function Deagle:RonMotion(gun)
	local r = gun.ronStats and self.ronStats and self.ronStats[gun.ronStats]
	if not r then return {} end
	local m = r.MovementSpeedScale or {}
	return { drag_aim_rot = r.InertiaDragAimRotation, drag_aim_loc = r.InertiaDragAimLocation,
		drag_strafe_rot = r.InertiaDragStrafeRotation, drag_strafe_loc = r.InertiaDragStrafeLocation,
		move_scale_x = m[1], move_scale_y = m[2], move_scale_z = m[3],
		move_rot_p = r.MovementSpeedRotationScalePitchYawRoll and r.MovementSpeedRotationScalePitchYawRoll[1],
		mesh_interp = r.MeshSpaceAimInterp }
end

function Deagle:MotionParams(gun)
	if not self.native then return end
	gun = gun or self.gun or {}
	local p = {}
	for k, v in pairs(self.motionDefaults) do p[k] = v end
	for k, v in pairs(self.motionGuns[gun.id or ""] or {}) do p[k] = v end
	for k, v in pairs(self:RonMotion(gun)) do p[k] = v end
	for k, v in pairs(gun.motion or {}) do p[k] = v end
	self.sprintSent, self.motionLazy = nil, nil   -- the reset: LowReadyTick / RonLayersTick send the gun's again
	for k, v in pairs(self.motionOverride) do p[k] = v end
	local keys = {}
	for k in pairs(p) do keys[#keys + 1] = k end
	table.sort(keys)
	self:Native("mparams reset")
	for _, k in ipairs(keys) do self:Native(string.format("mparams %s %.4f", k, p[k])) end
	if self.RonParams then self:RonParams(gun) end   -- a Ready or Not gun's own recoil in the plugin (recoil.lua)
end

function Deagle:MotionTick()
	if not self.native then return end
	if (self.cfg.vm.motion or 0) <= 0 then self.motionIn = nil return end      -- off: send nothing, start fresh when on
	local p = self:Player()
	if not p then return end
	local c, now = self.motionCfg, System.GetCurrTime()
	local m = self.motionIn
	if not m then m = { hist = {}, dodgeT = -1e9 }; self.motionIn = m end
	-- world velocity, smoothed, every frame
	local flying = self:Flying()
	local ok, v = pcall(p.GetVelocity, p)
	if not ok or type(v) ~= "table" or flying then v = { x = 0, y = 0, z = 0 } end
	local dt = m.t and math.max(0, math.min(0.25, now - m.t)) or 0
	m.t = now
	local k = m.vx and (1 - math.exp(-dt / c.velSmooth)) or 1
	m.vx = (m.vx or 0) + ((v.x or 0) - (m.vx or 0)) * k
	m.vy = (m.vy or 0) + ((v.y or 0) - (m.vy or 0)) * k
	m.vz = (m.vz or 0) + ((v.z or 0) - (m.vz or 0)) * k
	self:Native(string.format("vel %.3f %.3f %.3f", m.vx, m.vy, m.vz))
	-- camera-relative (horizontal right / forward) for the dodge detector
	local f = System.GetViewCameraDir()
	local fl = math.sqrt(f.x * f.x + f.y * f.y)
	local fx, fy = 0, 1
	if fl > 1e-4 then fx, fy = f.x / fl, f.y / fl end
	local vr, vf = (v.x or 0) * fy - (v.y or 0) * fx, (v.x or 0) * fx + (v.y or 0) * fy
	-- air
	local air = false
	if not flying then pcall(function() air = p.actor:IsFlying() and true or false end) end
	if air ~= m.air then self:Native("air " .. (air and "1" or "0")); m.air = air end
	-- stance: the same eye-height band as CsAccuracy (a bob can't flicker it)
	local a = self.cfg.accuracy
	local eye = System.GetViewCameraPos().z - p:GetWorldPos().z
	local crouched = eye < a.crouchEye + (m.crouched and a.crouchBand or -a.crouchBand)
	if crouched ~= m.crouched then self:Native("stance " .. (crouched and "1" or "0")); m.crouched = crouched end
	-- dodge: sideways (or backwards) speed rising by dodgeRise within dodgeWindow, on the ground, not sprinting
	local h = m.hist
	h[#h + 1] = { t = now, vr = vr, vf = vf }
	while #h > 1 and now - h[1].t > c.dodgeWindow do table.remove(h, 1) end
	if not air and vf < c.dodgeSprintFwd and now - m.dodgeT > c.dodgeCooldown then
		local minSide, maxBack = math.huge, -math.huge
		for _, e in ipairs(h) do minSide = math.min(minSide, math.abs(e.vr)); maxBack = math.max(maxBack, e.vf) end
		local side = math.abs(vr) >= c.dodgeSpeed and math.abs(vr) - minSide >= c.dodgeRise
		local back = not side and -vf >= c.dodgeSpeed and maxBack - vf >= c.dodgeRise
		if side or back then
			local s = side and (vr > 0 and 1 or -1) or 0
			local k = math.max(0.5, math.min(1.5, (side and math.abs(vr) or -vf) / 5))
			self:Native(string.format("dodge %d %.2f", s, k))
			m.dodgeT = now
		end
	end
end

System.AddCCommand("deagle_mparam", "local s = ([[%line]]):gsub('\"', '') local k, v = s:match('(%S+)%s+(%S+)') if k then Deagle.motionOverride[k] = tonumber(v) Deagle:MotionParams() end", "deagle_mparam <key> <value>: weapon-movement tunable (docs/DEBUG.md), e.g. test_roll 10")

-- ---------------------------------------------------------------------------
-- Ready or Not's additive animation layers on its guns (its ANIMBP_Player_FP_Master, decoded 2026-10-19 by the
-- deagle-game-todo-notion session's helper). Each on its own viewmodel layer, weighted by the plugin ("lweight":
-- KCD2's Lua has no layer weight; nothing plays without it), and only the parts whose clips the gun has:
--   breathe   its idle loop, weight AimInterp (an FInterpTo at 10 to 1 at the hip, 0.2 aiming)
--   loco      one layer, its clips crossfading: moveStart (fp_gen_start_quick_01, in 0.1 s; rate / weight 0.6 / 0.5
--             under 170 cm/s, 0.75 / 0.75 under 270, else 1 / 1), the loop (after 0.5 s) for the speed band and
--             direction (up to 150 cm/s locoM at speed / 150; to 200 locoM, backwards locoF, at speed / 250; above
--             locoF at speed / 310; a new direction over 0.3 s), moveStop on stopping (0.05 s; weight 0.4 aiming), out
--             over 0.1 s
--   roll      one clip of the left, centre and right poses (after the idle reference frame if rc kept it) scrubbed by
--             the strafe keys smoothed by an FInterpTo at 15, weight AimInterp x 0.7 x 0.75 (its bone blend from
--             weapon_right down; the clip has the arms too: ours follow the gun by baked FK, not IK)
--   jump      start (0.05 s in) -> loop with 0.1 s of start left -> end on landing (0.05 s) -> out (0.2 s), AimInterp
--   crouch    crouchIn / crouchOut on a stance change (0.25 s), out with 0.2 s left, AimInterp
--   shake     its loop at FirstPersonShakeAmount: 0.05 (DefaultIdleShake), 0.1 once sprinting, and it stays 0.1 after
--             (Ready or Not writes it only while sprinting: its bug, copied); reset at a load
--   ads       the aim kicks: adsIn at 0.2 (in 0.05 s), adsOut at 0.35 (in 0.2 s), out with 0.1 s left
-- Layers 0 (base) and 1 (aimed recoil on other guns) stay as they are.
-- ---------------------------------------------------------------------------
local AL = { breathe = 2, loco = 3, roll = 4, jump = 5, crouch = 6, shake = 7, ads = 8 }
Deagle.ronLayerCfg = { aimAds = 0.2, aimRate = 10, moving = 10, dirBlend = 0.3, rollRate = 15, rollW = 0.7 * 0.75,
	shakeIdle = 0.05, shakeSprint = 0.1, adsInW = 0.2, adsOutW = 0.35, stopAdsW = 0.4 }

local function clipLen(a) return a and math.max(1, a[2] - 1) / (a[3] or Deagle.cfg.vm.fps) or 0 end

function Deagle:RonLayersOn()
	return self.gun.ronStats and self.native and self.nativeCaps and self.nativeCaps.lweight and self.vmShown and true or false
end

-- a layer's weight, faded linearly to w over s seconds (0 = now); sent to the plugin when it changes
function Deagle:LayerFade(layer, w, s)
	local L = self.ronLayers
	local f = L.w[layer] or { now = 0 }
	f.to, f.rate = w, (s and s > 0) and math.abs(w - f.now) / s or math.huge
	L.w[layer] = f
end

-- a clip on a layer: started (a crossfade of blend s from what it plays), its rate, and when it began
function Deagle:LayerPlay(layer, key, blend, rate, loop)
	local L, a = self.ronLayers, self.cfg.vm.anims[key]
	local vm = self:VmEntity()
	if not (a and vm) then return false end
	pcall(vm.StartAnimation, vm, 0, a[1], layer, blend or 0, rate or 1, loop and true or false)
	L.clip[layer] = { key = key, t0 = System.GetCurrTime(), len = clipLen(a) / (rate or 1), rate = rate or 1 }
	return true
end

function Deagle:LayersReset()   -- the gun put away or changed: every layer of ours off and back at weight 1
	local L = self.ronLayers
	if not L then return end
	local vm = self.vmId and System.GetEntity(self.vmId)
	for _, l in pairs(AL) do
		if L.clip[l] and vm then pcall(vm.StopAnimation, vm, 0, l) end
		if L.w[l] and self.native then self:Native("lweight " .. l .. " -1") end
	end
	self.ronLayers = nil
end

-- every viewmodel frame on a Ready or Not gun
function Deagle:RonLayersTick()
	if not self:RonLayersOn() then if self.ronLayers then self:LayersReset() end return end
	local a, C, s = self.cfg.vm.anims, self.ronLayerCfg, self.state
	local now = System.GetCurrTime()
	local L = self.ronLayers
	if not L or L.gun ~= self.gun.id or L.vm ~= self.vmId then
		if L then self:LayersReset() end
		L = { gun = self.gun.id, vm = self.vmId, w = {}, clip = {}, aim = s.aiming and C.aimAds or 1, roll = 0, t = now }
		self.ronLayers = L
		if a.breathe and self:LayerPlay(AL.breathe, "breathe", 0, 1, true) then self:LayerFade(AL.breathe, L.aim, 0) end
		if a.shake and self:LayerPlay(AL.shake, "shake", 0, 1, true) then self:LayerFade(AL.shake, self.ronShake or C.shakeIdle, 0) end
	end
	local dt = math.max(0, math.min(0.1, now - L.t))
	L.t = now
	-- the plugin's procedural motion (motion.cpp) that Ready or Not hasn't: no walk bob on its guns (its WeaponBob code
	-- runs with every tuning field 0: the bob seen is the loco clips), no jump / land offsets once the jump clips play
	local want = a.jumpStart and "j0" or "j1"
	if self.motionLazy ~= want then
		self.motionLazy = want
		local j = a.jumpStart and 0 or 1
		self:Native("mparams jump " .. j); self:Native("mparams land " .. j); self:Native("mparams bob 0")
	end
	-- the shake: up to its sprint value for good once sprinting (viewmodel.lua LowReadyTick's vmSprint)
	if a.shake then
		if self.vmSprint then self.ronShake = C.shakeSprint end
		self:LayerFade(AL.shake, self.ronShake or C.shakeIdle, 0)
	end
	-- AimInterp
	L.aim = L.aim + ((s.aiming and C.aimAds or 1) - L.aim) * math.min(1, dt * C.aimRate)
	if a.breathe then self:LayerFade(AL.breathe, L.aim, 0) end
	local p, flying = self:Player(), self:Flying()
	local ok, v = pcall(p.GetVelocity, p)
	if not ok or type(v) ~= "table" or flying then v = { x = 0, y = 0, z = 0 } end
	local f = System.GetViewCameraDir()
	local fl = math.sqrt(f.x * f.x + f.y * f.y)
	local fx, fy = 0, 1
	if fl > 1e-4 then fx, fy = f.x / fl, f.y / fl end
	local vf, vr = (v.x or 0) * fx + (v.y or 0) * fy, (v.x or 0) * fy - (v.y or 0) * fx
	local speed = math.sqrt(vf * vf + vr * vr) * 100   -- cm/s, as Ready or Not's Speed
	-- locomotion
	if a.locoMFwd or a.locoFFwd then
		local lc = L.clip[AL.loco]
		local st = L.loco or "idle"
		local moving = speed > C.moving
		if moving and (st == "idle" or st == "stop") then
			local rate, w = 1, 1
			if speed < 170 then rate, w = 0.6, 0.5 elseif speed < 270 then rate, w = 0.75, 0.75 end
			if a.moveStart and self:LayerPlay(AL.loco, "moveStart", 0.1, rate, false) then st = "start"; self:LayerFade(AL.loco, w, 0.1)
			else st = "moving"; L.locoKey = nil end
		elseif st == "start" and lc and now >= lc.t0 + lc.len - 0.5 then
			st = "moving"; L.locoKey = nil
		elseif st == "moving" and not moving then
			if a.moveStop and self:LayerPlay(AL.loco, "moveStop", 0.05, 1, false) then
				st = "stop"; self:LayerFade(AL.loco, s.aiming and C.stopAdsW or 1, 0.05)
			else st = "idle"; self:LayerFade(AL.loco, 0, 0.3) end
		elseif st == "stop" and lc and now >= lc.t0 + lc.len then
			st = "idle"; self:LayerFade(AL.loco, 0, 0.1)
		end
		if st == "moving" then
			-- direction: forward / left / right / back by the velocity's angle to the view
			local dir = math.abs(vr) > math.abs(vf) and (vr > 0 and "R" or "L") or (vf >= 0 and "Fwd" or "Bwd")
			local set, rate = "M", speed / 150
			if speed > 200 then set, rate = "F", speed / 310
			elseif speed > 150 then set, rate = dir == "Bwd" and "F" or "M", speed / 250 end
			local key = "loco" .. set .. dir
			if not a[key] then key = a["locoM" .. dir] and "locoM" .. dir or "locoF" .. dir end
			if key ~= L.locoKey and a[key] then
				local blend = L.locoKey and C.dirBlend or 0.5   -- a new direction / band, or in from the start clip
				self:LayerPlay(AL.loco, key, blend, rate, true)
				L.locoKey = key
				self:LayerFade(AL.loco, 1, blend)
			end
			local vm = self:VmEntity()
			if vm then pcall(vm.SetAnimationSpeed, vm, 0, AL.loco, rate) end
		end
		L.loco = st
	end
	-- roll: the strafe keys (OnAction), smoothed
	if a.roll then
		local k = self.strafeKeys or {}
		local want = (k.right and 1 or 0) - (k.left and 1 or 0)
		L.roll = L.roll + (want - L.roll) * math.min(1, dt * C.rollRate)
		if not L.clip[AL.roll] then self:LayerPlay(AL.roll, "roll", 0, 0, true) end
		-- left, centre, right keys: the engine's normalized time 0, 1/3, 2/3 (measured live 2026-10-30: the compiled caf runs
		-- ticks 1..4 with keys at 1, 2, 3; t = 1 clamps to the right key). The old (1 + roll + 1) / 3 held the gun at the full
		-- right key at rest (weapon_right 11.4 deg, x0.525 at the hip).
		local frames = a.roll[2]
		local vm = self:VmEntity()
		if vm then pcall(vm.SetAnimationTime, vm, 0, AL.roll, (L.roll + 1) / math.max(1, frames - 1)) end
		self:LayerFade(AL.roll, C.rollW * L.aim, 0)
	end
	-- jump
	if a.jumpStart and a.jumpLoop and a.jumpEnd then
		local air = false
		if not flying then pcall(function() air = p.actor:IsFlying() and true or false end) end
		local jc, js = L.clip[AL.jump], L.jumpState
		if air and not js then
			self:LayerPlay(AL.jump, "jumpStart", 0.05, 1, false); L.jumpState = "start"; self:LayerFade(AL.jump, L.aim, 0.05)
		elseif js == "start" and jc and now >= jc.t0 + jc.len - 0.1 then
			self:LayerPlay(AL.jump, "jumpLoop", 0.05, 1, true); L.jumpState = "loop"
		elseif not air and (js == "start" or js == "loop") then
			self:LayerPlay(AL.jump, "jumpEnd", 0.05, 1, false); L.jumpState = "end"
		elseif js == "end" and jc and now >= jc.t0 + jc.len then
			L.jumpState = nil; self:LayerFade(AL.jump, 0, speed > C.moving and 0.1 or 0.2)
		elseif js then self:LayerFade(AL.jump, L.aim, 0) end
	end
	-- crouch
	if a.crouchIn and a.crouchOut then
		local crouched = self.motionIn and self.motionIn.crouched or false
		if L.crouched == nil then L.crouched = crouched end
		local cc = L.clip[AL.crouch]
		if crouched ~= L.crouched then
			L.crouched = crouched
			self:LayerPlay(AL.crouch, crouched and "crouchIn" or "crouchOut", 0.25, 1, false); L.crouchOn = true
			self:LayerFade(AL.crouch, L.aim, 0.25)
		elseif L.crouchOn and cc and now >= cc.t0 + cc.len - 0.2 then
			L.crouchOn = false; self:LayerFade(AL.crouch, 0, 0.25)
		elseif L.crouchOn then self:LayerFade(AL.crouch, L.aim, 0) end
	end
	-- aim kicks
	if a.adsIn and a.adsOut then
		if L.aimWas ~= nil and s.aiming ~= L.aimWas then
			local key = s.aiming and "adsIn" or "adsOut"
			self:LayerPlay(AL.ads, key, s.aiming and 0.05 or 0.2, 1, false); L.adsOn = true
			self:LayerFade(AL.ads, s.aiming and C.adsInW or C.adsOutW, s.aiming and 0.05 or 0.2)
		elseif L.adsOn and L.clip[AL.ads] and now >= L.clip[AL.ads].t0 + L.clip[AL.ads].len - 0.1 then
			L.adsOn = false; self:LayerFade(AL.ads, 0, 0.1)
		end
		L.aimWas = s.aiming
	end
	-- the weights: on toward their targets, sent when they move
	for layer, fw in pairs(L.w) do
		local d = fw.to - fw.now
		if d ~= 0 then
			local step = fw.rate == math.huge and math.huge or fw.rate * dt   -- (inf x 0 would be NaN)
			fw.now = math.abs(d) <= step and fw.to or fw.now + (d > 0 and step or -step)
		end
		if fw.sent == nil or math.abs(fw.now - fw.sent) > 0.005 or (fw.now == fw.to and fw.sent ~= fw.to) then
			fw.sent = fw.now
			self:Native(string.format("lweight %d %.3f", layer, fw.now))
		end
	end
end
