-- Flight (src/Scripts/Deagle/flight.lua): Henry flies by velocity, never by SetPos (the Flight mod's crash); F4 through
-- the plugin bridge; no gravity while flying, put back after; F4 off in the air sinks him to the ground first.
-- usage: luajit tests/run_flight.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
_G.PHYSICPARAM_PLAYERDYN, _G.PHYSICPARAM_VELOCITY = 4, 2
dofile("src/Scripts/Deagle/deagle_main.lua")
local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
check("flight.lua makes the Flight table the plugin bridge drives", type(Flight) == "table" and Flight.ours and type(Flight.Keys) == "table", type(Flight))

-- Henry: records what the flight does to his physics, moves by the velocity it sets
local P = g_localActor
local dyn, vel, setpos = {}, nil, 0
P.pos = { x = 0, y = 0, z = 10 }   -- 10 m up
local ground = 0
local origSet = P.SetPhysicParams
function P:SetPhysicParams(kind, t)
	if kind == PHYSICPARAM_PLAYERDYN then dyn[#dyn + 1] = t
	elseif kind == PHYSICPARAM_VELOCITY then vel = t.v
	elseif origSet then return origSet(self, kind, t) end
end
local origSetPos = P.SetPos
function P:SetPos(...) setpos = setpos + 1 if origSetPos then return origSetPos(self, ...) end end
function P:SetWorldPos(...) setpos = setpos + 1 end
function P:GetVelocity() return vel and { x = vel.x, y = vel.y, z = vel.z } or { x = 0, y = 0, z = 0 } end
_G.Physics.RayWorldIntersection = function(from, dir, _, _, _, _, hits)
	local d = from.z - ground
	if dir.z < 0 and d >= 0 and d <= -dir.z then hits[1] = { dist = d, pos = { x = from.x, y = from.y, z = ground } } return 1 end
	return 0
end
local function step(sec)   -- the game moves him by the velocity he has
	local t = 0
	while t < sec do
		M.advance(1 / 60)
		if vel then P.pos = { x = P.pos.x + vel.x / 60, y = P.pos.y + vel.y / 60, z = math.max(ground, P.pos.z + vel.z / 60) } end
		t = t + 1 / 60
	end
end

Deagle:OnGameplayStarted(); M.console("deagle_native_ok"); M.console("deagle_flykey", "linked 1")
local function sentFlydyn() for i = #M.native, 1, -1 do if M.native[i]:find("^%[DN%] flydyn %x+$") then return true end end end
-- an older plugin (no flydyn caps): no flight, says why
M.console("deagle_native_caps", "files fxr smoke ronvm lowready lweight lean")
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0"); step(0.2)
check("old plugin (no flydyn): refuses, gravity untouched", not Deagle.flight and Flight.active == false and #dyn == 0, #dyn)
M.console("deagle_native_caps", "files fxr smoke ronvm lowready lweight lean flydyn")
-- the plugin never answers: off after 1 s, nothing changed
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0"); step(1.3)
check("no answer from the plugin: off after 1 s, gravity untouched", sentFlydyn() and not Deagle.flight and Flight.active == false and #dyn == 0, #dyn)
-- it answers with a failure
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0"); step(0.1)
M.console("deagle_flydyn", "fail no living entity"); step(0.1)
check("a failed read: off, gravity untouched", not Deagle.flight and Flight.active == false and #dyn == 0, #dyn)
-- the real path: his own values come first (odd ones on purpose: the restore must put back exactly these)
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0"); step(0.1)
check("nothing changes before the read comes back", #dyn == 0 and not Deagle.flight, #dyn)
M.console("deagle_flydyn", "11.5000 0.2000 0.4000")
step(0.2)
check("F4: flying; gravity and air control off", Deagle.flight and dyn[#dyn] and dyn[#dyn].gravity == 0 and dyn[#dyn].k_air_control == 0, dyn[#dyn] and dyn[#dyn].gravity)
check("hovering with no keys: velocity ~0", vel and math.abs(vel.x) + math.abs(vel.y) + math.abs(vel.z) < 0.05, vel and vel.z)

M.view.pitch, M.view.yaw = 0, 0
M.console("deagle_flykey", "forward 1"); step(1.0)
check("W: 10 m/s along the view (eased in, no jump)", vel and math.abs(vel.y - 10) < 0.5 and math.abs(vel.z) < 0.1, vel and vel.y)
M.console("deagle_flykey", "fast 1"); step(1.0)
check("Shift: x4", vel and math.abs(vel.y - 40) < 2, vel and vel.y)
M.console("deagle_flykey", "fast 0"); M.console("deagle_flykey", "forward 0"); M.console("deagle_flykey", "right 1"); step(1.5)
check("D: strafes right, level", vel and vel.x > 9 and math.abs(vel.y) < 0.6 and math.abs(vel.z) < 0.1, vel and vel.x)
M.console("deagle_flykey", "right 0"); M.console("deagle_flykey", "up 1"); step(1.0)
check("Space: up", vel and vel.z > 9, vel and vel.z)
M.console("deagle_flykey", "up 0"); M.console("deagle_flykey", "down 1"); step(1.0)
check("Left Alt: down", vel and vel.z < -9, vel and vel.z)
M.console("deagle_flykey", "down 0"); step(1.0)

-- KCD2 re-applying the actor's own params mid-flight (a stance change: picking up a body) doesn't make him fall
local before = #dyn
P:SetPhysicParams(PHYSICPARAM_PLAYERDYN, { gravity = 13, k_air_control = 0.1 })
step(0.1)
check("a stance change re-applies gravity: the next frame takes it off again", #dyn > before + 1 and dyn[#dyn].gravity == 0, dyn[#dyn] and dyn[#dyn].gravity)

-- F4 off 8 m up: he sinks to the ground, then his gravity comes back (no fall)
P.pos.z = 8
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0")
step(0.5)
check("F4 off in the air: still flying, sinking", Deagle.flight and Deagle.flight.landing and vel and vel.z < -1, vel and vel.z)
step(6)
check("landed: flight over, his own values back exactly (gravity 11.5, air control 0.2)", not Deagle.flight and dyn[#dyn].gravity == 11.5 and dyn[#dyn].k_air_control == 0.2,
	tostring(Deagle.flight) .. " " .. tostring(dyn[#dyn] and dyn[#dyn].gravity))
check("he landed slowly (under 2 m/s at the end)", vel and math.abs(vel.z) < 2, vel and vel.z)

-- F4 on the ground: gravity back at once; deagle_fly works too; speed command
M.console("deagle_fly"); step(0.1); M.console("deagle_flydyn", "11.5000 0.2000 0.4000"); step(0.1)
check("deagle_fly: on", Deagle.flight ~= nil, "off")
M.console("deagle_fly"); step(0.2)
check("deagle_fly on the ground: off at once", not Deagle.flight, "still on")
M.console("deagle_fly_speed", "25")
check("deagle_fly_speed 25", Deagle.flightCfg.speed == 25, Deagle.flightCfg.speed)
M.console("deagle_fly"); step(0.1); M.console("deagle_flydyn", "11.5000 0.2000 0.4000"); step(0.1)
local taken = Deagle:OnAction("deagle_fc_faster", "press", 1)
check("scroll up while flying: speed x1.25, the wheel is taken", taken == true and math.abs(Deagle.flightCfg.speed - 31.25) < 1e-6, Deagle.flightCfg.speed)
Deagle:OnAction("deagle_fc_slower", "press", 1); Deagle:OnAction("deagle_fc_slower", "press", 1)
check("scroll down twice: x1/1.25 each", math.abs(Deagle.flightCfg.speed - 20) < 1e-6, Deagle.flightCfg.speed)
M.console("deagle_fly"); step(0.2)
check("not flying: the wheel is the game's again", Deagle:OnAction("deagle_fc_faster", "press", 1) ~= true, "taken")
check("never SetPos / SetWorldPos on Henry", setpos == 0, setpos)

-- a save load drops every script timer: the next F4 must still fly (in game 2026-10-19 it didn't)
M.timers = {}; Deagle.flight = nil; Flight.active = false; step(1.0)
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0"); step(0.2)
local asked = false; for i = #M.native, 1, -1 do if M.native[i]:find("^%[DN%] flydyn") then asked = true break end end
M.console("deagle_flydyn", "11.5000 0.2000 0.4000"); step(0.2)
check("after a load wiped the timers, F4 still flies", asked and Deagle.flight ~= nil, tostring(asked) .. " " .. tostring(Deagle.flight))
print(string.format("flight: %d/%d", n - fails, n))
os.exit(fails == 0 and 0 or 1)
