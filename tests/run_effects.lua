-- Impact effects (effects.lua) in the mock game: the impact light pool stays at cfg.impactLight.max, its entities have
-- no physics (solid ones shoved the player at 75 rounds/s, 2026-10-15), and the effect budget thins a CIWS burst.
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
local fails = 0
local function check(name, ok, got) print((ok and "PASS " or "FAIL ") .. name .. (ok and "" or ("  " .. tostring(got)))) if not ok then fails = fails + 1 end end
local spawned, destroyed = {}, 0
local spawn = System.SpawnEntity
System.SpawnEntity = function(p)
	local e = spawn(p)
	if p.name and p.name:find("^deagle_impact_light_") then
		spawned[#spawned + 1] = p
		rawset(e, "DestroyPhysics", function() destroyed = destroyed + 1 end)
	end
	return e
end
local D = Deagle
local pos, n = { x = 10, y = 0, z = 1 }, { x = -1, y = 0, z = 0 }
for i = 1, 20 do D:ImpactLight("deagle_impacts.metal", { x = 10, y = i * 0.01, z = 1 }, n, 1) end
check("impact lights: the pool stays at its cap", #spawned == D.cfg.impactLight.max, #spawned)
local phys = true
for _, p in ipairs(spawned) do phys = phys and p.properties and p.properties.Physics and p.properties.Physics.bPhysicalize == false end
check("impact lights: spawned without physics", phys, phys)
check("impact lights: physics destroyed once per light", destroyed == #spawned, destroyed)
D:ImpactLight("deagle_impacts.soil", pos, n, 1)
check("impact lights: soil gives no light", #spawned == D.cfg.impactLight.max, #spawned)
-- the budget: 75 hits at one spot in one game second -> far fewer effects
local before = #M.native
local t0 = System.GetCurrTime()
for i = 1, 75 do D:SpawnFx("deagle_impacts.stone", { x = 10, y = (i % 5) * 0.05, z = 1 }, n, 1) end
local fxs = 0
for i = before + 1, #M.native do if M.native[i]:find("^%[DN%] fxs deagle_impacts%.stone") then fxs = fxs + 1 end end
local direct = 0
for _, f in ipairs(M.fxLog or {}) do if f.n == "deagle_impacts.stone" then direct = direct + 1 end end
local spawnedFx = math.max(fxs, direct)
check("effect budget: 75 hits on one spot at once spawn at most the burst", spawnedFx >= 1 and spawnedFx <= D.cfg.fxBudget.burst, spawnedFx)
-- a light goes out by its own timer even when the game clock reads coarse (a long save: 3.9 ms steps, 2026-10-17):
-- clock frozen one step short of the light's end, timers still run
local freed = 0
local getTime = System.GetCurrTime
local frozen = getTime()
System.GetCurrTime = function() return frozen end
Deagle.impactLights = nil
local spawn2 = System.SpawnEntity
System.SpawnEntity = function(p)
	local e = spawn2(p)
	rawset(e, "FreeSlot", function() freed = freed + 1 end)
	return e
end
D:ImpactLight("deagle_impacts.metal", { x = 30, y = 0, z = 1 }, n, 1)
M.advance(0.2)
check("impact lights: off by their timer with a coarse game clock", freed >= 1, freed)
System.GetCurrTime, System.SpawnEntity = getTime, spawn2
-- the stress spawners: every impact effect raw (no budget), and the light pool
local S = Deagle.StressSpawners or {}
check("stress spawners: fx_metal, fx_soil, fx_water_v3 and light registered", S.fx_metal and S.fx_soil and S.fx_water_v3 and S.light, S.fx_metal)
local b2 = #M.native
for i = 1, 40 do S.fx_soil({ x = 20, y = 0, z = 0 }, { x = 0, y = 0, z = 1 }, i) end
local raw = 0
for i = b2 + 1, #M.native do if M.native[i]:find("^%[DN%] fxs deagle_impacts%.soil ") then raw = raw + 1 end end
local rawLua = 0
for _, f in ipairs(M.fxLog or {}) do if f.n == "deagle_impacts.soil" then rawLua = rawLua + 1 end end
check("stress spawner fx_soil: raw, all 40 spawned (no budget)", math.max(raw, rawLua) == 40, math.max(raw, rawLua))
print(fails == 0 and "EFFECTS OK" or ("EFFECTS FAILED: " .. fails))
os.exit(fails == 0 and 0 or 1)
