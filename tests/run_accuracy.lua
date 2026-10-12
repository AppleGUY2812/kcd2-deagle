-- CS2 accuracy for guns with a CS2 profile (Deagle:CsAccuracy): standing / crouched / moving / in the air / the fire
-- penalty and its recovery, against the gun's own CS2 numbers; shots stay inside the cone and bunch to the middle.
-- usage: luajit tests/run_accuracy.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local me = Deagle:Player()
local state = { vel = { x = 0, y = 0, z = 0 }, air = false, camDz = 0 }
me.GetVelocity = function() return state.vel end
me.actor.IsFlying = function() return state.air end
local cam = System.GetViewCameraPos
System.GetViewCameraPos = function() local p = cam(); return { x = p.x, y = p.y, z = p.z + state.camDz } end
local eyeStand = cam().z - me:GetWorldPos().z

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function near(a, b) return math.abs(a - b) < 1e-6 end

for _, id in ipairs({ "deagle", "glock_17" }) do
	Deagle:UseGun(id)
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
	local w = Deagle.cs2[Deagle.gun.cs2]
	local s = Deagle.state
	s.accFire, s.accT, s.crouched = 0, nil, false
	state.camDz, state.vel, state.air = 0, { x = 0, y = 0, z = 0 }, false
	print(string.format("-- %s (%s): stand %.1f crouch %.1f move %.1f jump %.1f fire %.1f mrad, recovery %.2f / %.2f s",
		id, Deagle.gun.cs2, w.inStand * 1000, w.inCrouch * 1000, w.inMove * 1000, w.inJump * 1000, w.inFire * 1000, w.recStand, w.recCrouch))

	local stand, spread = Deagle:CsAccuracy(w)
	check(string.format("%s: standing still = CS2 stand %.1f mrad + spread %.1f", id, stand * 1000, spread * 1000), near(stand, w.inStand) and near(spread, w.spread), stand)
	state.camDz = 1.02 - eyeStand                        -- crouched (live eye height)
	local crouch = Deagle:CsAccuracy(w)
	check(string.format("%s: crouched (eye 1.02 m) = CS2 crouch %.1f mrad, tighter than standing", id, crouch * 1000), near(crouch, w.inCrouch) and crouch < stand, crouch)
	state.camDz = 1.25 - eyeStand                        -- inside the band: stays crouched (no flicker)
	check(string.format("%s: 1.25 m eye keeps the crouch (band)", id), near(Deagle:CsAccuracy(w), w.inCrouch), s.crouched)
	state.camDz = 0
	check(string.format("%s: standing again at 1.54+ m", id), near(Deagle:CsAccuracy(w), w.inStand), s.crouched)
	state.vel = { x = 0.3 * w.maxSpeed, y = 0, z = 0 }   -- a slow walk: under 34 % of max speed, no penalty
	check(string.format("%s: walking slowly (%.1f m/s) costs nothing", id, 0.3 * w.maxSpeed), near(Deagle:CsAccuracy(w), w.inStand), nil)
	state.vel = { x = w.maxSpeed, y = 0, z = 0 }         -- running flat out
	local run = Deagle:CsAccuracy(w)
	check(string.format("%s: running (%.1f m/s) adds CS2's move %.1f mrad", id, w.maxSpeed, w.inMove * 1000), near(run, w.inStand + w.inMove), run)
	state.vel, state.air = { x = 0, y = 0, z = 0 }, true
	check(string.format("%s: in the air adds CS2's jump %.1f mrad", id, w.inJump * 1000), near(Deagle:CsAccuracy(w), w.inStand + w.inJump), nil)
	state.air = false

	-- a shot adds the fire penalty; it decays to 10 % over the recovery time, faster crouched
	Deagle.state.ammo, Deagle.state.lastShot = Deagle.gun.magSize, -100
	Deagle:Shoot(true)
	local after = Deagle:CsAccuracy(w)
	check(string.format("%s: right after a shot %.1f mrad (stand + fire)", id, after * 1000), near(after, w.inStand + w.inFire), after)
	local t0 = M.t
	M.advance(w.recStand, 60)
	local later = Deagle:CsAccuracy(w)
	local left = 0.1 ^ ((M.t - t0) / w.recStand)        -- 10 % after exactly the recovery time
	check(string.format("%s: %.2f s later %.1f mrad (%.0f %% of the fire penalty left)", id, M.t - t0, later * 1000, left * 100),
		math.abs(later - (w.inStand + left * w.inFire)) < 1e-6 and math.abs(left - 0.1) < 0.01, later)
	M.advance(3, 60)
	check(string.format("%s: settled back to standing", id), math.abs(Deagle:CsAccuracy(w) - w.inStand) < 1e-5, nil)

	-- where shots go: never outside inaccuracy + spread, more of them near the middle than at the edge
	local hits = {}
	local saved = Physics.RayWorldIntersection
	Physics.RayWorldIntersection = function(pos, dir, _, _, _, _, out) hits[#hits + 1] = { x = dir.x, y = dir.y, z = dir.z }; return 0 end
	local maxA, inner = 0, 0
	for i = 1, 400 do
		s.accFire, s.accT = 0, nil
		Deagle.state.ammo, Deagle.state.lastShot = Deagle.gun.magSize, -100
		M.view.pitch, M.view.yaw = 0, 0
		Deagle.cfg.physics.barrelAim = 0
		local n0 = #hits
		Deagle:Shoot(true)
		local d = hits[n0 + 2] or hits[n0 + 1]   -- the round's first step (after the tracer's aim ray)
		local l = math.sqrt(d.x * d.x + d.y * d.y + d.z * d.z)
		local a = math.acos(math.min(1, d.y / l))
		maxA = math.max(maxA, a)
		if a < 0.5 * (w.inStand + w.spread) then inner = inner + 1 end
	end
	Physics.RayWorldIntersection = saved
	check(string.format("%s: 400 standing first shots all within %.1f mrad (widest %.1f), %d in the inner half", id,
		(w.inStand + w.spread) * 1000, maxA * 1000, inner), maxA <= w.inStand + w.spread + 1e-4 and inner > 120, maxA)
	check(string.format("%s: first shot at 20 m lands within %.0f cm (was a 1.6 deg cone: 56 cm)", id, 20 * (w.inStand + w.spread) * 100),
		20 * (w.inStand + w.spread) < 0.2, nil)
	Deagle:OnAction("deagle_toggle", "press"); M.advance(2.0)
end
-- debug zoom (F2 / deagle_zoom): x4, x10, off; the field of view eases to the game's / the factor and back
local base = System.GetViewCameraFov()
Deagle:OnAction("deagle_zoom", "press"); M.advance(0.5)
local z4 = M.fov
Deagle:OnAction("deagle_zoom", "press"); M.advance(0.5)
local z10 = M.fov
Deagle:OnAction("deagle_zoom", "press"); M.advance(0.5)
check(string.format("F2 zoom: x4 %.3f, x10 %.3f, off %.3f (game %.3f)", z4 or -1, z10 or -1, M.fov or -1, base),
	z4 and math.abs(z4 - base / 4) < 1e-6 and math.abs(z10 - base / 10) < 1e-6 and math.abs(M.fov - base) < 1e-6, z4)
-- deagle_set: on / off settings take 1 / 0, lists take comma-separated numbers, numbers stay numbers
Deagle:Set("hitReactions 0"); local off = Deagle.cfg.hitReactions
Deagle:Set("hitReactions on"); local on = Deagle.cfg.hitReactions
Deagle:Set("flashGrow 0.6,0.9,1.1,1.1"); local grow = Deagle.cfg.flashGrow
Deagle:Set("accuracy.scale 0.5"); local sc = Deagle.cfg.accuracy.scale
check("deagle_set: hitReactions 0 -> false, on -> true; flashGrow list; accuracy.scale 0.5",
	off == false and on == true and type(grow) == "table" and #grow == 4 and grow[1] == 0.6 and sc == 0.5, tostring(off))
Deagle:Set("accuracy.scale 1"); Deagle:Set("flashGrow 0.7,1,1.1")
M.console("deagle_zoom", "6"); M.advance(0.5)
local z6 = M.fov
M.console("deagle_zoom", ""); M.advance(0.5)
check(string.format("deagle_zoom 6 through the console (quoted arg): fov %.3f, then off %.3f", z6 or -1, M.fov or -1),
	z6 and math.abs(z6 - base / 6) < 1e-6 and math.abs(M.fov - base) < 1e-6, z6)
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("accuracy: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
