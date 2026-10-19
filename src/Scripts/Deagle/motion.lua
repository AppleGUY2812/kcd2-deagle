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
	-- a gun with its own sprint clips (viewmodel.lua LowReadyTick) has no procedural sprint pose (a plugin with lowready)
	if self.nativeCaps and self.nativeCaps.lowready then p.sprint = gun.anims and gun.anims.sprint and 0 or 1 end
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
	local ok, v = pcall(p.GetVelocity, p)
	if not ok or type(v) ~= "table" then v = { x = 0, y = 0, z = 0 } end
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
	pcall(function() air = p.actor:IsFlying() and true or false end)
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
