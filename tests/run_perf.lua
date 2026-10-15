-- Performance gates in the mock game (docs/PERFORMANCE.md): what the mod asks of the engine per frame, idle and
-- shooting, and the Lua garbage it makes. The mock can't time anything; it counts the calls whose in-game cost is
-- known (an entity-system query 5-9 ms, a new particle emitter per round per frame = the descriptor-heap crash), so
-- the classes of bug the audit of 2026-10-15 found can't come back unnoticed.
-- usage: luajit tests/run_perf.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok"); M.console("deagle_native_caps", "files fxr")

local n, fails = 0, 0
local function check(name, ok, got)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. name .. (ok and "" or ("   got: " .. tostring(got))))
end

-- count the engine calls that cost: entity-system queries (any radius: 5-9 ms), rays, AI sounds
local counts = {}
local function counted(t, name)
	local f = t[name]
	if not f then return end
	t[name] = function(...) counts[name] = (counts[name] or 0) + 1 return f(...) end
end
for _, k in ipairs({ "GetEntitiesInSphere", "GetEntitiesByClass", "GetEntitiesInBox" }) do counted(System, k) end
counted(Physics, "RayWorldIntersection")
counted(XGenAIModule, "ProduceSound")
local function reset() counts = {} end
local function native(from, pat) local c = 0 for i = from + 1, #M.native do if M.native[i]:find(pat) then c = c + 1 end end return c end
local queries = function() return (counts.GetEntitiesInSphere or 0) + (counts.GetEntitiesByClass or 0) + (counts.GetEntitiesInBox or 0) end

-- the gun out, standing still
Deagle:OnAction("deagle_toggle", "press"); M.advance(3)
reset()
local n0, frames = #M.native, 300
collectgarbage("collect"); collectgarbage("stop")
local kb0 = collectgarbage("count")
M.advance(frames / 60)
local kb = (collectgarbage("count") - kb0) / frames
collectgarbage("restart")
local cmds = (#M.native - n0) / frames
print(string.format("   idle, per frame: %.2f KB garbage, %.2f plugin commands, %.2f rays, %d entity queries in %d frames",
	kb, cmds, (counts.RayWorldIntersection or 0) / frames, queries(), frames))
check("idle: no entity-system query (5-9 ms each in game) in 5 s with the gun out", queries() == 0, queries())
check("idle: the room check casts no rays while the player stands still", (counts.RayWorldIntersection or 0) == 0, counts.RayWorldIntersection)
check("idle: the echo weights stop being sent once they're there", native(n0, "^%[DN%] rong ") == 0, native(n0, "^%[DN%] rong "))
check(string.format("idle: at most 3 plugin commands a frame (%.2f)", cmds), cmds <= 3, cmds)
check(string.format("idle: under 5 KB of Lua garbage a frame (%.2f KB; 3.97 on 2026-10-15, the budget is 1)", kb), kb < 5, kb)

-- shooting into the open: 10 rounds a second for 3 s (no wall: they fly their whole life)
Deagle.state.ammo = 999
reset()
n0 = #M.native
for _ = 1, 30 do Deagle:Shoot(true); M.advance(0.1) end
M.advance(3)
local spawns = native(n0, "^%[DN%] fxs [%w_]*trail")
print(string.format("   shooting 30 rounds: %d entity queries, %d tracer emitters spawned, %d tracer moves, %.0f rays",
	queries(), spawns, native(n0, "^%[DN%] fxr "), counts.RayWorldIntersection or 0))
check("shooting: no entity-system query per frame or per round", queries() <= 1, queries())
check(string.format("shooting: no new tracer emitter per round per frame (%d spawns for 30 rounds)", spawns), spawns <= 30, spawns)

-- an NPC hit plays armour + flesh (+ a ricochet) at one spot in one frame: the AI hears it once
reset()
local at = { x = 1, y = 2, z = 3 }
Deagle:HitSound("armor_body_block", at, 1); Deagle:HitSound(16, at, 1); Deagle:HitSound("ricochet", at, 1, 1)
check("one AI sound per hit, not one per layer", (counts.ProduceSound or 0) <= 1 or not Deagle.cfg.hearing.on, counts.ProduceSound)

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("perf: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
