-- Weapon-movement inputs (src/Scripts/Deagle/motion.lua): which commands Deagle:MotionTick sends the plugin for
-- standing / walking / sprinting / jumping / landing / crouching / dodging, the crouch band (hysteresis), the rate
-- limit (kcd.log isn't flooded), nothing while deagle_motion is 0, and per-gun mparams.
-- usage: luajit tests/run_motion.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
if not Deagle.MotionTick then dofile("src/Scripts/Deagle/motion.lua") end   -- until LoadGuns loads it
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local me = Deagle:Player()
local st = { vel = { x = 0, y = 0, z = 0 }, air = false, camDz = 0 }
me.GetVelocity = function() return st.vel end
me.actor.IsFlying = function() return st.air end
local cam = System.GetViewCameraPos
System.GetViewCameraPos = function() local p = cam(); return { x = p.x, y = p.y, z = p.z + st.camDz } end
local eyeStand = cam().z - me:GetWorldPos().z
M.view.pitch, M.view.yaw = 0, 0                     -- looking down +y: right = +x

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
-- run MotionTick every frame for `sec`, return the motion commands it sent
local function run(sec, fps)
	fps = fps or 60
	local from = #M.native + 1
	for _ = 1, math.floor(sec * fps + 0.5) do M.t = M.t + 1 / fps; Deagle:MotionTick() end
	local out = {}
	for i = from, #M.native do
		local c = M.native[i]:sub(6)
		local k = c:match("^(%S+)")
		if k == "vel" or k == "air" or k == "stance" or k == "dodge" or k == "mparams" then out[#out + 1] = c end
	end
	return out
end
local function count(list, prefix) local k = 0 for _, c in ipairs(list) do if c:sub(1, #prefix) == prefix then k = k + 1 end end return k end
local function last(list, prefix) for i = #list, 1, -1 do if list[i]:sub(1, #prefix) == prefix then return list[i] end end end
local function vel(cmd) local r, f, u = (cmd or ""):match("vel (%S+) (%S+) (%S+)") return tonumber(r), tonumber(f), tonumber(u) end

Deagle:UseGun("deagle")
Deagle.cfg.vm.motion = 1
Deagle.motionIn = nil
-- standing: the first tick says air 0 / stance 0; vel goes out EVERY frame (world m/s, smoothed), 0 while standing
local c = run(0.05)
check("first tick sends air 0 + stance 0 + vel 0 0 0", count(c, "air 0") == 1 and count(c, "stance 0") == 1 and last(c, "vel") == "vel 0.000 0.000 0.000", table.concat(c, " | "))
c = run(2)
check(string.format("standing still 2 s: vel every frame (%d), all 0, nothing else", count(c, "vel")), count(c, "vel") == 120 and #c == 120 and last(c, "vel") == "vel 0.000 0.000 0.000", table.concat(c, " | "):sub(1, 200))
-- walking forward 1.8 m/s: world vel, smoothed (reaches 1.8 within a fraction of a second)
st.vel = { x = 0, y = 1.8, z = 0 }
c = run(0.05)
local x0, y0 = vel(last(c, "vel"))
check(string.format("walking: smoothed, not a jump (%.3f after 3 frames)", y0), y0 > 0.3 and y0 < 1.7, last(c, "vel"))
c = run(2)
local x, y = vel(last(c, "vel") or "vel 0 0 0")
check(string.format("walking 2 s: vel x %.3f y %.3f (world), %d commands (every frame)", x, y, count(c, "vel")), x == 0 and math.abs(y - 1.8) < 1e-3 and count(c, "vel") == 120, last(c, "vel"))
-- turned 90 deg left: Lua keeps sending WORLD velocity (the plugin makes it camera-relative itself)
M.view.yaw = math.pi / 2
c = run(0.1)
x, y = vel(last(c, "vel") or "vel 0 0 0")
check(string.format("turned 90 deg: still world x %.3f y %.3f", x, y), x == 0 and math.abs(y - 1.8) < 1e-3, last(c, "vel"))
M.view.yaw = 0
-- sprinting 5.5 m/s (the plugin's hysteresis decides the pose)
st.vel = { x = 0, y = 5.5, z = 0 }
c = run(1)
x, y = vel(last(c, "vel") or "vel 0 0 0")
check("sprinting: vel y 5.500, no dodge", math.abs(y - 5.5) < 1e-3 and count(c, "dodge") == 0, table.concat(c, " | "):sub(1, 200))
-- stopping: vel eases to 0
st.vel = { x = 0, y = 0, z = 0 }
c = run(1)
check("stopping: vel eases to 0.000", last(c, "vel") == "vel 0.000 0.000 0.000", last(c, "vel"))
-- jump: air 1 once, vel up, landing air 0 once
st.air, st.vel = true, { x = 0, y = 0, z = 3.5 }
c = run(0.3)
check("takeoff: air 1 once", count(c, "air 1") == 1 and count(c, "air 0") == 0, table.concat(c, " | "))
st.vel = { x = 0, y = 0, z = -4.5 }
c = run(0.3)
check("falling: no repeat of air 1", count(c, "air") == 0, table.concat(c, " | "):sub(1, 200))
st.air, st.vel = false, { x = 0, y = 0, z = 0 }
c = run(0.2)
check("landing: air 0 once", count(c, "air 0") == 1, table.concat(c, " | "))
-- crouch (eye 1.02 m), the band holds at 1.25 m, standing again at 1.54 m
st.camDz = 1.02 - eyeStand
c = run(0.2)
check("crouching: stance 1 once", count(c, "stance 1") == 1, table.concat(c, " | "))
st.camDz = 1.25 - eyeStand
c = run(0.5)
check("1.25 m eye (inside the band): stays crouched, nothing sent", count(c, "stance") == 0, table.concat(c, " | "))
st.camDz = 1.30 - eyeStand
c = run(0.2)
check("1.30 m: still inside the band (1.27 + 0.05)", count(c, "stance") == 0, table.concat(c, " | "))
st.camDz = 0
c = run(0.2)
check("standing up: stance 0 once", count(c, "stance 0") == 1, table.concat(c, " | "))
-- dodge right: sideways speed 0 -> 4.5 m/s within 0.1 s
local function ramp(target, sec, fps)
	fps = fps or 60
	local all, steps = {}, math.floor(sec * fps + 0.5)
	local x0, y0 = st.vel.x, st.vel.y
	for i = 1, steps do
		st.vel = { x = x0 + (target.x - x0) * i / steps, y = y0 + (target.y - y0) * i / steps, z = 0 }
		for _, cmd in ipairs(run(1 / fps, fps)) do all[#all + 1] = cmd end
	end
	return all
end
c = ramp({ x = 4.5, y = 0 }, 0.1)
for _, cmd in ipairs(run(0.3)) do c[#c + 1] = cmd end
check("dodge right (0 -> 4.5 m/s in 0.1 s): dodge 1 once", count(c, "dodge 1 ") == 1 and count(c, "dodge") == 1, table.concat(c, " | "))
st.vel = { x = 0, y = 0, z = 0 }; run(1)
c = ramp({ x = -4.5, y = 0 }, 0.1)
check("dodge left: dodge -1", count(c, "dodge -1 ") == 1, table.concat(c, " | "))
st.vel = { x = 0, y = 0, z = 0 }; run(1)
c = ramp({ x = 0, y = -4.2 }, 0.1)
check("dodge back: dodge 0", count(c, "dodge 0 ") == 1, table.concat(c, " | "))
st.vel = { x = 0, y = 0, z = 0 }; run(1)
-- a steady strafe build-up (1 s to 4 m/s) is not a dodge; neither is a dodge-like spike in the air
c = ramp({ x = 4.0, y = 0 }, 1.0)
check("strafing up to 4 m/s over 1 s: no dodge", count(c, "dodge") == 0, table.concat(c, " | "))
st.vel = { x = 0, y = 0, z = 0 }; run(1)
st.air = true; run(0.1)
c = ramp({ x = 4.5, y = 0 }, 0.1)
check("sideways spike in the air: no dodge", count(c, "dodge") == 0, table.concat(c, " | "))
st.air, st.vel = false, { x = 0, y = 0, z = 0 }; run(1)
-- cooldown: two spikes 0.3 s apart -> one dodge
c = ramp({ x = 4.5, y = 0 }, 0.1)
st.vel = { x = 0, y = 0, z = 0 }
for _, cmd in ipairs(run(0.2)) do c[#c + 1] = cmd end
for _, cmd in ipairs(ramp({ x = 4.5, y = 0 }, 0.1)) do c[#c + 1] = cmd end
check("two spikes 0.3 s apart: one dodge (cooldown 0.6 s)", count(c, "dodge") == 1, table.concat(c, " | "))
st.vel = { x = 0, y = 0, z = 0 }; run(1)
-- deagle_motion 0: nothing at all
Deagle.cfg.vm.motion = 0
st.vel, st.air, st.camDz = { x = 0, y = 5, z = 0 }, true, 1.02 - eyeStand
c = run(1)
check("deagle_motion 0: nothing sent while moving / airborne / crouched", #c == 0, table.concat(c, " | "))
Deagle.cfg.vm.motion = 1
st.vel, st.air, st.camDz = { x = 0, y = 0, z = 0 }, false, 0
c = run(0.1)
check("turned back on: the state is sent again", count(c, "air 0") == 1 and count(c, "stance 0") == 1, table.concat(c, " | "))
-- per-gun parameters: reset, then every key; the Deagle takes the .357 Python values, the Glock the defaults
local from = #M.native + 1
Deagle:MotionParams(Deagle.guns.deagle)
local sent = {}
for i = from, #M.native do local k, v = M.native[i]:match("^%[DN%] mparams (%S+) ?(%S*)") if k then sent[k] = tonumber(v) or true end end
local nkeys = 0 for _ in pairs(Deagle.motionDefaults) do nkeys = nkeys + 1 end
nkeys = nkeys + 1   -- sprint (a plugin with lowready: the sprint pose's strength, 1 for a gun without sprint clips)
local got = 0 for k in pairs(sent) do if k ~= "reset" then got = got + 1 end end
check(string.format("MotionParams(deagle): reset + %d keys, sprint %s", nkeys, tostring(sent.sprint)), sent.reset and got == nkeys and sent.sprint == 1, got)
check("deagle: drag_aim_loc 0.875, hang_k 80, land 1.2 (RoN .357 Python)", sent.drag_aim_loc == 0.875 and sent.hang_k == 80 and sent.land == 1.2, tostring(sent.drag_aim_loc))
from = #M.native + 1
Deagle:MotionParams(Deagle.guns.glock_17)
sent = {}
for i = from, #M.native do local k, v = M.native[i]:match("^%[DN%] mparams (%S+) ?(%S*)") if k then sent[k] = tonumber(v) or true end end
check("glock_17: defaults (drag_aim_loc 0.4, hang_k 100, sprint_roll -35.5)", sent.drag_aim_loc == 0.4 and sent.hang_k == 100 and sent.sprint_roll == -35.5, tostring(sent.drag_aim_loc))
-- a Ready or Not gun takes its blueprint's own movement numbers (gun.ronStats)
from = #M.native + 1
Deagle:MotionParams({ id = "ron_test", ronStats = "BP_Secondary_LARGrizzly" })
sent = {}
for i = from, #M.native do local k, v = M.native[i]:match("^%[DN%] mparams (%S+) ?(%S*)") if k then sent[k] = tonumber(v) or true end end
check(string.format("ron gun (Grizzly): drag %s/%s/%s/%s, move scale %s/%s/%s, rot %s, interp %s",
	tostring(sent.drag_aim_rot), tostring(sent.drag_aim_loc), tostring(sent.drag_strafe_rot), tostring(sent.drag_strafe_loc),
	tostring(sent.move_scale_x), tostring(sent.move_scale_y), tostring(sent.move_scale_z), tostring(sent.move_rot_p), tostring(sent.mesh_interp)),
	sent.drag_aim_rot == 0.6 and sent.drag_aim_loc == 0.4 and sent.drag_strafe_rot == 0.4 and sent.drag_strafe_loc == 0.2
	and sent.move_scale_x == 10 and sent.move_scale_y == 10 and sent.move_scale_z == 6 and sent.move_rot_p == 60 and sent.mesh_interp == 55,
	tostring(sent.move_scale_z))
-- deagle_mparam overrides and is kept
M.cmds.deagle_mparam = M.cmds.deagle_mparam and M.cmds.deagle_mparam:gsub("%%line", "test_roll 10")
from = #M.native + 1
M.console("deagle_mparam")
local hit = false
for i = from, #M.native do if M.native[i] == "[DN] mparams test_roll 10.0000" then hit = true end end
check("deagle_mparam test_roll 10: sent (the roll sign check)", hit, M.native[#M.native])
Deagle.motionOverride = {}
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("MOTION %d/%d passed", n - fails, n))
os.exit(fails == 0 and 0 or 1)
