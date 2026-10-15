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
-- who makes the garbage: KB allocated inside each Deagle function (inclusive: a caller counts its callees too; the
-- mock's Physics.RayWorldIntersection builds its hit tables in Lua, which the engine doesn't)
local gc, wrapped = {}, {}
local function finish(k, a, ...) gc[k] = (gc[k] or 0) + collectgarbage("count") - a return ... end   -- no table of its own
for k, f in pairs(Deagle) do
	if type(f) == "function" and k ~= "Native" then
		wrapped[k] = f
		Deagle[k] = function(...) return finish(k, collectgarbage("count"), f(...)) end
	end
end
collectgarbage("collect"); collectgarbage("stop")
kb0 = collectgarbage("count")
for _ = 1, 30 do Deagle:Shoot(true); M.advance(0.1) end
local roundFrames = 0
for _ = 1, 180 do roundFrames = roundFrames + #(Deagle.rounds or {}); M.advance(1 / 60) end
local shootKb = (collectgarbage("count") - kb0) / 360
for k, f in pairs(wrapped) do Deagle[k] = f end
local top = {}
for k, v in pairs(gc) do top[#top + 1] = { k, v / 360 } end
table.sort(top, function(a, b) return a[2] > b[2] end)
local tl = {}
for i = 1, math.min(8, #top) do tl[#tl + 1] = string.format("%s %.1f", top[i][1], top[i][2]) end
print("   shooting garbage by function, KB a frame (inclusive): " .. table.concat(tl, ", "))
local perRound = roundFrames > 0 and (collectgarbage("count") - kb0) / roundFrames or 0
collectgarbage("restart")
print(string.format("   shooting: %.1f KB garbage a frame, ~%.2f KB per round in the air per frame (%d round-frames after the burst)",
	shootKb, perRound, roundFrames))
local spawns = native(n0, "^%[DN%] fxs [%w_]*trail")
print(string.format("   shooting 30 rounds: %d entity queries, %d tracer emitters spawned, %d tracer moves, %.0f rays",
	queries(), spawns, native(n0, "^%[DN%] fxr "), counts.RayWorldIntersection or 0))
check("shooting: no entity-system query per frame or per round", queries() <= 1, queries())
check(string.format("shooting: under 100 KB of Lua garbage a frame with ~30 rounds up (%.1f KB; 75.8 on 2026-10-15, the budget is 15)", shootKb),
	shootKb < 100, shootKb)
check(string.format("shooting: no new tracer emitter per round per frame (%d spawns for 30 rounds)", spawns), spawns <= 30, spawns)

-- an NPC hit plays armour + flesh (+ a ricochet) at one spot in one frame: the AI hears it once
reset()
local at = { x = 1, y = 2, z = 3 }
Deagle:HitSound("armor_body_block", at, 1); Deagle:HitSound(16, at, 1); Deagle:HitSound("ricochet", at, 1, 1)
check("one AI sound per hit, not one per layer", (counts.ProduceSound or 0) <= 1 or not Deagle.cfg.hearing.on, counts.ProduceSound)

-- an impact too far to see spawns no effect (Claude 2, a34cb45): a 600 m hit sends nothing to the plugin, a 20 m one does
local function fxsAt(dy)
	local k = #M.native
	Deagle:SpawnFx("deagle_impacts.stone", { x = 100, y = 200 + dy, z = 31 }, { x = 0, y = -1, z = 0 }, 1)
	return native(k, "^%[DN%] fxs ")
end
local near, far = fxsAt(20), fxsAt(600)
check(string.format("an impact 600 m away spawns no effect (%d), one 20 m away does (%d)", far, near), far == 0 and near >= 1, far)

-- sounds under heavy fire (sound.lua SoundBudget): 75 rounds/s made 150-290 hit sounds a second and 8-12 fps in game
local function ron3(from) return native(from, "^%[DN%] ron3 ") end
Deagle.soundBudgetState = nil
local cp = System.GetViewCameraPos()
local k0 = #M.native
for i = 1, 20 do Deagle:HitSound(8, { x = cp.x, y = cp.y + 5, z = cp.z + i * 0.001 }, 1) end   -- one spot, one frame
local same = ron3(k0)
k0 = #M.native
for i = 1, 300 do Deagle:HitSound(8, { x = cp.x + (i % 20) - 10, y = cp.y + 5, z = cp.z }, 1); M.advance(1 / 300, 300) end   -- near, 1 s
local spread = ron3(k0)
local H = Deagle.cfg.soundBudget.hit
check(string.format("hit sounds: 20 at one spot at once play 1 (%d); 300 near you over 1 s play at most burst + rate (%d of %d)",
	same, spread, H.burst + H.rate), same == 1 and spread <= H.burst + H.rate + 1, spread)
-- far hits leave the reserve to close ones; hits on people always play
Deagle.soundBudgetState = nil
for i = 1, 40 do Deagle:HitSound(8, { x = cp.x + i, y = cp.y + 200, z = cp.z }, 1) end      -- far: drain to the reserve
k0 = #M.native
for i = 1, 3 do Deagle:HitSound(8, { x = cp.x + i, y = cp.y + 4, z = cp.z }, 1) end        -- close: the reserve plays them
local close = ron3(k0)
k0 = #M.native
for i = 1, 30 do Deagle:HitSound(16, { x = cp.x, y = cp.y + 4, z = cp.z }, 1) end           -- flesh, all at one spot
check(string.format("after far hits drained the budget, close hits still play (%d of 3); 30 flesh hits at one spot all play (%d)", close, ron3(k0)),
	close == 3 and ron3(k0) == 30, close)
-- ricochets have their own, smaller budget
Deagle.soundBudgetState = nil
k0 = #M.native
for i = 1, 100 do Deagle:HitSound("ricochet", { x = cp.x + i * 2, y = cp.y + 5, z = cp.z }, 1, 1); M.advance(1 / 100, 100) end
local R = Deagle.cfg.soundBudget.ricochet
check(string.format("ricochets: 100 over 1 s play at most burst + rate (%d of %d)", ron3(k0), R.burst + R.rate), ron3(k0) <= R.burst + R.rate + 1, ron3(k0))

-- the stress sampler's sound spawners (deagle_stress spawn <name>): raw hit sounds skip the budget, the budgeted one doesn't
Deagle.soundBudgetState = nil
k0 = #M.native
for _ = 1, 20 do Deagle.StressSpawners.hitsound({ x = cp.x, y = cp.y + 5, z = cp.z }) end
local raw = ron3(k0)
k0 = #M.native
for _ = 1, 20 do Deagle.StressSpawners.hitsound_budget({ x = cp.x, y = cp.y + 5, z = cp.z }) end
check(string.format("stress spawners: raw hit sounds all play (%d of 20), budgeted ones merge (%d)", raw, ron3(k0)), raw == 20 and ron3(k0) == 1, raw)

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("perf: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
