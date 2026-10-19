-- Ready or Not's additive animation layers on its guns (motion.lua RonLayersTick, its ANIMBP_Player_FP_Master rules):
-- breathing and the shake loop from the draw, breathing down to 0.2 aiming with the aim kick, the locomotion layer
-- (start clip, the loop for the speed band and direction at its rate, the stop clip, out), the roll scrubbed by the
-- strafe keys, jump start / loop / end, crouch in, the plugin's own jump / land / bob off, all let go at the holster.
-- Clips the gun doesn't have yet stand in under their own names.
-- usage: luajit tests/run_layers.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function last(pat) for i = #M.native, 1, -1 do local v = M.native[i]:match(pat) if v then return v end end end
local function weight(l) return tonumber(last("lweight " .. l .. " ([%-%d.]+)$") or "") end
local function act(a, x) return Deagle:OnAction(a, x or "press") end

Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
Deagle:UseGun("ron_grizzly")
local A = Deagle.cfg.vm.anims
local function clip(k, frames, loop) A[k] = { "ron_grizzly_vm_" .. k, frames, 30 } end
for _, k in ipairs({ "locoMFwd", "locoML", "locoMR", "locoMBwd" }) do clip(k, 93) end
for _, k in ipairs({ "locoFFwd", "locoFL", "locoFR", "locoFBwd" }) do clip(k, 27) end
clip("breathe", 120); clip("shake", 121); clip("moveStart", 30); clip("moveStop", 31); clip("roll", 3)
clip("jumpStart", 10); clip("jumpLoop", 20); clip("jumpEnd", 15); clip("crouchIn", 15); clip("crouchOut", 15)
clip("adsIn", 9); clip("adsOut", 9)

local p = Deagle:Player()
local vel, air = { x = 0, y = 0, z = 0 }, false
p.GetVelocity = function() return { x = vel.x, y = vel.y, z = vel.z } end
p.actor.IsFlying = function() return air end
act("deagle_toggle"); M.advance(2.0)
local vm = System.GetEntity(Deagle.vmId)
check("drawn: breathing on layer 2 at 1, the shake on 7 at 0.05 (its idle)", vm.layers[2] == A.breathe[1] and weight(2) == 1
	and vm.layers[7] == A.shake[1] and weight(7) == 0.05, tostring(vm.layers[2]) .. " " .. tostring(weight(7)))
check("the plugin's own jump / land / bob off (Ready or Not has none)", last("mparams jump (%d)") == "0"
	and last("mparams land (%d)") == "0" and last("mparams bob (%d)") == "0", last("mparams bob (%d)"))

-- aiming: breathing to 0.2 by an FInterpTo at 10, the aim kick at 0.2
act("deagle_aim"); M.advance(0.1)
local w01, kick = weight(2), vm.layers[8]
M.advance(1.0)
check(string.format("aiming: breathing %.3f after 0.1 s (want %.3f), 0.2 after 1 s; adsIn on layer 8 at 0.2 (%s)", w01,
	1 - 0.8 * (1 - (1 - 10 / 60) ^ 6), tostring(kick)),
	math.abs(w01 - (1 - 0.8 * (1 - (1 - 10 / 60) ^ 6))) < 0.01 and math.abs(weight(2) - 0.2) < 0.005 and kick == A.adsIn[1], w01)
act("deagle_aim", "release"); M.advance(1.0)
check("aim off: adsOut, breathing back to 1", M.anims[#M.anims].name == A.adsOut[1] or vm.layers[8] == A.adsOut[1], vm.layers[8])

-- walking forward at 1 m/s: the start clip (0.6 / 0.5 under 170 cm/s), then locoMFwd at 100 / 150
vel = { x = 0, y = 1, z = 0 }; M.advance(0.05)
local startKey, startRate = vm.layers[3], M.anims[#M.anims] and M.anims[#M.anims].layer == 3 and M.anims[#M.anims] or nil
M.advance(0.05)
check("walking 100 cm/s: moveStart at weight 0.5", startKey == A.moveStart[1] and weight(3) == 0.5, tostring(startKey) .. " " .. tostring(weight(3)))
M.advance(2.0)   -- the start clip runs 29 / 30 / 0.6 = 1.61 s; the loop comes in over its last 0.5 s
check(string.format("  then locoMFwd at %.3f (want %.3f), weight 1", vm.layerSpeed[3] or -1, 100 / 150),
	vm.layers[3] == A.locoMFwd[1] and math.abs((vm.layerSpeed[3] or 0) - 100 / 150) < 0.01 and weight(3) == 1, vm.layers[3])
-- running 250 cm/s: locoFFwd at 250 / 310; strafing right: locoFR
vel = { x = 0, y = 2.5, z = 0 }; M.advance(0.5)
check(string.format("running 250 cm/s: locoFFwd at %.3f", vm.layerSpeed[3] or -1), vm.layers[3] == A.locoFFwd[1]
	and math.abs((vm.layerSpeed[3] or 0) - 250 / 310) < 0.01, vm.layers[3])
vel = { x = 1.2, y = 0, z = 0 }; M.advance(0.5)
check("strafing right 120 cm/s: locoMR", vm.layers[3] == A.locoMR[1], vm.layers[3])
-- stopping: moveStop, then out
vel = { x = 0, y = 0, z = 0 }; M.advance(0.05)
local stopKey = vm.layers[3]
M.advance(1.5)
check("stopping: moveStop, then the layer out (weight 0)", stopKey == A.moveStop[1] and weight(3) == 0, tostring(stopKey) .. " " .. tostring(weight(3)))

-- roll: the right strafe key scrubs the roll clip to its right end
act("moveright"); M.advance(0.5)
check(string.format("roll: right key -> time %.3f (want ~1), weight %.3f (0.525)", vm.layerTime[4] or -1, weight(4) or -1),
	vm.layers[4] == A.roll[1] and (vm.layerTime[4] or 0) > 0.99 and math.abs((weight(4) or 0) - 0.525) < 0.005, vm.layerTime[4])
act("moveright", "release"); M.advance(0.5)
check("  let go: back to the middle (0.5)", math.abs((vm.layerTime[4] or 0) - 0.5) < 0.01, vm.layerTime[4])
-- rc kept the idle reference frame (4 frames): left, centre, right are frames 1-3 of 0-3
A.roll[2] = 4; M.advance(0.5)
check(string.format("  4-frame roll clip (reference kept): centre at %.3f (2/3)", vm.layerTime[4] or -1),
	math.abs((vm.layerTime[4] or 0) - 2 / 3) < 0.01, vm.layerTime[4])
A.roll[2] = 3

-- sprinting puts the shake at 0.1, and it stays there (Ready or Not's bug, copied)
clip("sprint", 20)
vel = { x = 0, y = 6, z = 0 }; M.advance(0.3)
vel = { x = 0, y = 0, z = 0 }; M.advance(1.0)
A.sprint = nil
check("after a sprint the shake stays at 0.1", weight(7) == 0.1, weight(7))

-- jump
air = true; M.advance(0.05)
local j1 = vm.layers[5]
M.advance(0.4)
local j2 = vm.layers[5]
air = false; M.advance(0.05)
local j3 = vm.layers[5]
M.advance(1.0)
check(string.format("jump: %s -> %s -> %s, then out (%s)", tostring(j1), tostring(j2), tostring(j3), tostring(weight(5))),
	j1 == A.jumpStart[1] and j2 == A.jumpLoop[1] and j3 == A.jumpEnd[1] and weight(5) == 0, j2)

-- crouch
local crouchEye = Deagle.cfg.accuracy.crouchEye
Deagle.cfg.accuracy.crouchEye = 1e6   -- MotionTick finds the eye under it: crouched
M.advance(0.05)
check("crouching: crouchIn", vm.layers[6] == A.crouchIn[1], vm.layers[6])
M.advance(1.0)
check("  then out", weight(6) == 0, weight(6))
Deagle.cfg.accuracy.crouchEye = crouchEye
M.advance(1.0)

-- flying (Flight.active, flight.lua): 10 m/s and off the ground, but Henry isn't on his feet: no sprint pose, no steps,
-- no jump loop, and the plugin's sway hears no velocity
Flight = Flight or {}
local flyWas = Flight.active
Flight.active = true; clip("sprint", 20)
vel = { x = 0, y = 10, z = 0 }; air = true; M.advance(1.0)
local pv = { (last("%] vel ([%-%d.]+) [%-%d.]+ [%-%d.]+$")), (last("%] vel [%-%d.]+ ([%-%d.]+) [%-%d.]+$")) }
check(string.format("flying 10 m/s in the air: no sprint pose (%s), steps %s, jump %s, plugin vel %s %s", tostring(Deagle.vmSprint),
	tostring(weight(3)), tostring(Deagle.ronLayers.jumpState), tostring(pv[1]), tostring(pv[2])),
	not Deagle.vmSprint and (weight(3) or 0) == 0 and Deagle.ronLayers.jumpState == nil
	and math.abs(tonumber(pv[1]) or 1) < 0.01 and math.abs(tonumber(pv[2]) or 1) < 0.01, Deagle.vmSprint)
Flight.active = flyWas; A.sprint = nil
vel = { x = 0, y = 0, z = 0 }; air = false; M.advance(1.0)

-- holster: every layer off, its weight let go
act("deagle_toggle"); M.advance(2.0)
check("holstered: our layers stopped and let go (lweight -1)", vm.layers[2] == nil and vm.layers[3] == nil and weight(2) == -1
	and weight(3) == -1 and Deagle.ronLayers == nil, tostring(vm.layers[2]) .. " " .. tostring(weight(2)))

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("layers: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
