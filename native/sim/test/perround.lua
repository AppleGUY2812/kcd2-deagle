-- usage: luajit native/sim/test/perround.lua <lua | path/to/libdeaglesim.so> [rounds] (Claude 5, 2026-10-17; mock numbers, not the game's)
-- per-round Lua cost with the native StepRound: 500 rounds alive in the mock sky, which parts of StepRound's Lua cost what
local SO, N = arg[1], tonumber(arg[2] or 500)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
if SO ~= "lua" then local NS = dofile("native/sim/sim_ffi.lua"); NS.install(Deagle, SO) end
Physics.RayWorldIntersection = function() return 0 end   -- sky: the ray costs nothing here (in game it's 2.84 us, native)
Deagle.cfg.impactCap.maxRounds = 100000
Deagle:OnAction("deagle_toggle", "press"); M.advance(0.5)
Deagle:UseGun("deagle")
local phys = Deagle:ShotPhysics()
Deagle.frameDt = 1 / 60
local T = {}
for _, name in ipairs({ "StepRound", "TracerBullet", "KillCamStep", "FlyBy" }) do
	local f = Deagle[name]
	T[name] = { 0, 0 }
	Deagle[name] = function(...) local t0 = os.clock(); local a, b = f(...); T[name][1] = T[name][1] + os.clock() - t0; T[name][2] = T[name][2] + 1; return a, b end
end
for i = 1, N do
	local a = i / N * 6.283
	Deagle:LaunchRound({ x = 0, y = 0, z = 10 }, { x = math.cos(a) * 0.7, y = math.sin(a) * 0.7, z = 0.714 }, phys, nil)
end
for k in pairs(T) do T[k] = { 0, 0 } end
local t0 = os.clock()
M.advance(1, 60)
local wall = os.clock() - t0
print(string.format("%s, %d rounds, 60 frames: %.3f ms a frame total; alive %d", SO == "lua" and "Lua sim" or "native sim", N, wall / 60 * 1000, #(Deagle.rounds or {})))
for _, k in ipairs({ "StepRound", "TracerBullet", "KillCamStep", "FlyBy" }) do
	print(string.format("  %-12s %7.3f ms a frame (incl. nested), %5.2f us a call, %d calls", k, T[k][1] / 60 * 1000, T[k][1] / math.max(1, T[k][2]) * 1e6, T[k][2]))
end
