-- Where the player is, for per-place gunshots (docs/mk5_sound_spec.md): the classifier on made-up ray results for an
-- open field, a forest, a porch, a cottage room, a wooden tavern hall, a corridor and a stone cellar; a shot plays a
-- take of the current place (never the same take twice running), and a class only takes over after 2 agreeing reads.
-- usage: luajit tests/run_env.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
-- a sample: roof hits (count, at m, kind), ring distances (16, nil = open) and their kind
local function sample(roofN, roofAt, roofKind, ring, kind)
	local s = { up = {}, ring = {} }
	for i = 1, 5 do s.up[i] = i <= roofN and { dist = roofAt, kind = roofKind } or {} end
	for i = 1, 16 do
		local d, k = ring, kind
		if type(ring) == "table" then d = ring[i] end
		if type(kind) == "table" then k = kind[i] end
		s.ring[i] = d and { dist = d, kind = k } or {}
	end
	return s
end
local open = {}
local forestRing, forestKind = {}, {}
for i = 1, 16 do forestRing[i] = (i % 2 == 0) and 9 or nil; forestKind[i] = (i % 4 == 0) and "veg" or "wood" end
local corridor = { 1.2, 3, 8, 3, 8, 3, 8, 3, 1.2, 3, 8, 3, 8, 3, 8, 3 }
corridor[5], corridor[13] = 8, 8
local cases = {
	{ "open field", sample(0, nil, nil, nil, "terrain"), "out_street" },
	{ "forest", sample(1, 8, "veg", forestRing, forestKind), "out_forest" },
	{ "porch (roof, open sides)", sample(5, 2.6, "wood", { 2, 2, 2, nil, nil, nil, nil, nil, nil, nil, nil, nil, nil, nil, 2, 2 }, "wood"), "out_covered" },
	{ "cottage room", sample(5, 2.4, "wood", 2.5, "other"), "in_small" },
	{ "tavern hall (wood)", sample(5, 6, "wood", 9, "wood"), "in_hall_wood" },
	{ "corridor", sample(5, 3, "other", corridor, "other"), "in_corridor" },
	{ "stone cellar", sample(5, 2.2, "stone", 4.5, "stone"), "in_cave_stone" },
}
for _, c in ipairs(cases) do
	local got = Deagle:EnvClassify(c[2])
	check(string.format("%s -> %s", c[1], got), got == c[3], c[3])
end
-- the class follows reads only after two agree; a shot plays the place's take
Deagle:UseGun("deagle")
local reads = { cases[4][2], cases[7][2], cases[7][2] }
local k = 0
Deagle.EnvSample = function() k = k + 1 return reads[math.min(k, #reads)] end
Deagle.envT, Deagle.envClass, Deagle.envPending = nil, nil, nil
Deagle:EnvTick(); local c1 = Deagle.envClass
M.advance(0.3); Deagle:EnvTick(); local c2 = Deagle.envClass
M.advance(0.3); Deagle:EnvTick(); local c3 = Deagle.envClass
check(string.format("first read takes the class (%s), one different read doesn't (%s), the second does (%s)", tostring(c1), tostring(c2), tostring(c3)),
	c1 == "in_small" and c2 == "in_small" and c3 == "in_cave_stone", c2)
local files, repeats, last = {}, 0, nil
for i = 1, 40 do
	local f = Deagle:ShotFile()
	files[f] = true
	if f == last then repeats = repeats + 1 end
	last = f
end
local nfiles = 0
for f in pairs(files) do nfiles = nfiles + 1; if not f:find("^mk5/in_cave_stone/shot%-%d+%.wav$") then nfiles = -99 end end
check(string.format("shots play the cellar's takes (%d different), never the same twice running (%d)", nfiles, repeats), nfiles >= 6 and repeats == 0, nfiles)
check("every take has a gain in shot_gain.lua", Deagle.shotGain["mk5/in_cave_stone/shot-1.wav"] ~= nil and Deagle.shotGain["mk5/out_forest/shot-8.wav"] ~= nil, nil)
Deagle:UseGun("glock_17")
check("a gun without per-place shots plays its own", (Deagle:ShotFile() or ""):find("^mk5/") == nil, Deagle:ShotFile())
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("env: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
