-- Weapon-movement inputs for the native motion layer (native/src/motion.cpp; docs/weapon_movement_design.md).
-- Deagle:MotionTick() runs every frame (from VmTick) and sends the plugin what Lua knows better than the camera:
--   vel <right> <fwd> <up>   camera-relative velocity m/s (player:GetVelocity: no head bob); sent on change (> velStep)
--                            or every velRefresh s while moving, at most every velMinDt s, so kcd.log isn't flooded
--   air 0|1                  in the air (player.actor:IsFlying), on change
--   stance 0|1               crouched = eye under cfg.accuracy.crouchEye +- crouchBand (the CsAccuracy rule), on change
--   dodge <side> <strength>  KCD2 gives no dodge flag: a sideways (or backwards) speed spike, see motionCfg.dodge*
-- Nothing is sent while deagle_motion is 0. Deagle:MotionParams(gun) sends the gun's tunables (mparams <key> <value>).

Deagle.motionCfg = {
	velStep = 0.25, velRefresh = 0.3, velMinDt = 0.05, moving = 0.3,   -- m/s, s
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

function Deagle:MotionParams(gun)
	if not self.native then return end
	gun = gun or self.gun or {}
	local p = {}
	for k, v in pairs(self.motionDefaults) do p[k] = v end
	for k, v in pairs(self.motionGuns[gun.id or ""] or {}) do p[k] = v end
	for k, v in pairs(gun.motion or {}) do p[k] = v end
	for k, v in pairs(self.motionOverride) do p[k] = v end
	local keys = {}
	for k in pairs(p) do keys[#keys + 1] = k end
	table.sort(keys)
	self:Native("mparams reset")
	for _, k in ipairs(keys) do self:Native(string.format("mparams %s %.4f", k, p[k])) end
end

function Deagle:MotionTick()
	if not self.native then return end
	if (self.cfg.vm.motion or 0) <= 0 then self.motionIn = nil return end      -- off: send nothing, start fresh when on
	local p = self:Player()
	if not p then return end
	local c, now = self.motionCfg, System.GetCurrTime()
	local m = self.motionIn
	if not m then m = { velT = -1e9, hist = {}, dodgeT = -1e9 }; self.motionIn = m end
	-- camera-relative velocity (horizontal right / forward, world up)
	local ok, v = pcall(p.GetVelocity, p)
	if not ok or type(v) ~= "table" then v = { x = 0, y = 0, z = 0 } end
	local f = System.GetViewCameraDir()
	local fl = math.sqrt(f.x * f.x + f.y * f.y)
	local fx, fy = 0, 1
	if fl > 1e-4 then fx, fy = f.x / fl, f.y / fl end
	local vr, vf, vu = v.x * fy - v.y * fx, v.x * fx + v.y * fy, v.z or 0
	local speed = math.sqrt(vr * vr + vf * vf)
	local changed = not m.vr or math.abs(vr - m.vr) > c.velStep or math.abs(vf - m.vf) > c.velStep or math.abs(vu - m.vu) > c.velStep
	local refresh = (speed > c.moving or math.abs(vu) > c.moving) and now - m.velT >= c.velRefresh
	local stopped = speed <= c.moving and math.abs(vu) <= c.moving
	if (changed or refresh) and now - m.velT >= c.velMinDt and not (stopped and m.sentStop) then
		self:Native(string.format("vel %.2f %.2f %.2f", vr, vf, vu))
		m.vr, m.vf, m.vu, m.velT, m.sentStop = vr, vf, vu, now, stopped
	end
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
