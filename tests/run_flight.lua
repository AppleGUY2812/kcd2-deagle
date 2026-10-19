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
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0")
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
check("landed: flight over, gravity back to BasicActor's 13", not Deagle.flight and dyn[#dyn].gravity == 13 and dyn[#dyn].k_air_control == 0.1,
	tostring(Deagle.flight) .. " " .. tostring(dyn[#dyn] and dyn[#dyn].gravity))
check("he landed slowly (under 2 m/s at the end)", vel and math.abs(vel.z) < 2, vel and vel.z)

-- F4 on the ground: gravity back at once; deagle_fly works too; speed command
M.console("deagle_fly"); step(0.2)
check("deagle_fly: on", Deagle.flight ~= nil, "off")
M.console("deagle_fly"); step(0.2)
check("deagle_fly on the ground: off at once", not Deagle.flight, "still on")
M.console("deagle_fly_speed", "25")
check("deagle_fly_speed 25", Deagle.flightCfg.speed == 25, Deagle.flightCfg.speed)
check("never SetPos / SetWorldPos on Henry", setpos == 0, setpos)

print(string.format("flight: %d/%d", n - fails, n))
os.exit(fails == 0 and 0 or 1)
