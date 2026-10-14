-- Where the player is, for Ready or Not's live room sound (docs/ron_live_spec.md): the classifier on made-up ray results
-- for an open field, a forest, a porch, a cottage room, a wooden tavern hall, a corridor, a stone cellar and a stone room;
-- a class only takes over after 2 agreeing reads; its room / material weights fade in; a shot fires RoN's event.
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
	{ "stone room, wooden roof", sample(5, 3, "wood", 5, "stone"), "in_mid_stone" },
	{ "smithy (metal)", sample(5, 3, "wood", 2.5, "metal"), "in_small_metal" },
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
-- the cellar's weights: CAVE + CONCRETE at 1, everything else 0, inside
local mix, outside = Deagle:EnvMix("in_cave_stone")
local on = {}
for g, v in pairs(mix) do if v > 0 then on[#on + 1] = g .. "=" .. v end end
table.sort(on)
check("cellar = " .. table.concat(on, " "), table.concat(on, " ") == "ReverbCAVE=1 ReverbMaterialCONCRETE=1" and not outside, table.concat(on, " "))
local pm = Deagle:EnvMix("out_covered")
check("porch = MID + CONCRETE, outside", pm.ReverbMID == 1 and pm.ReverbMaterialCONCRETE == 1 and select(2, Deagle:EnvMix("out_covered")), nil)
-- fading: from the small room (set at once) to the cellar over cfg.env.fade s, sent as rong lines
local function sentG(from)
	local got = {}
	for i = from + 1, #M.native do
		local g, v = M.native[i]:match("^%[DN%] rong (%S+) ([%d%.]+)$")
		if g then got[g] = tonumber(v) end
	end
	return got
end
Deagle.envClass, Deagle.envMixNow, Deagle.envFadeT = "in_small", nil, nil
local n0 = #M.native
Deagle:EnvFade(10)
local g0 = sentG(n0)
check("first class is set at once (SMALL 1, CAVE 0)", g0.ReverbSMALL == 1 and g0.ReverbCAVE == 0, g0.ReverbSMALL)
Deagle.envClass = "in_cave_stone"
n0 = #M.native
Deagle:EnvFade(10 + Deagle.cfg.env.fade / 2)
local g1 = sentG(n0)
check(string.format("halfway through the fade: SMALL %.2f CAVE %.2f", g1.ReverbSMALL or -1, g1.ReverbCAVE or -1),
	math.abs((g1.ReverbSMALL or 0) - 0.5) < 0.01 and math.abs((g1.ReverbCAVE or 0) - 0.5) < 0.01, g1.ReverbSMALL)
Deagle:EnvFade(10 + Deagle.cfg.env.fade + 0.1)
check("after the fade: the cellar's weights", Deagle.envMixNow.ReverbSMALL == 0 and Deagle.envMixNow.ReverbCAVE == 1 and Deagle.envMixNow.ReverbMaterialCONCRETE == 1, Deagle.envMixNow.ReverbSMALL)
-- a shot: Ready or Not's MK V event with IsOutside from the place
n0 = #M.native
Deagle:CueFire("shoot", "shot")
local line = M.native[#M.native]
check("a shot fires RoN's event, inside: " .. tostring(line), #M.native == n0 + 1 and line:find("^%[DN%] ron [%d%.]+ event:/Weapons/Grizzly/1P Grizzly Fire|IsOutside=0$") ~= nil, line)
Deagle.envClass = "out_forest"
Deagle:CueFire("shoot", "shot")
check("outside: IsOutside=1", M.native[#M.native]:find("|IsOutside=1$") ~= nil, M.native[#M.native])
check("the handshake loads the event", (function() for i = 1, #M.native do if M.native[i] == "[DN] ronload event:/Weapons/Grizzly/1P Grizzly Fire" then return true end end end)(), nil)
-- every surface a round stops on has a Ready or Not Surface index (none of RoN's silent ones)
local missing, silent = {}, { [4] = 1, [11] = 1, [12] = 1, [22] = 1, [23] = 1, [25] = 1, [30] = 1, [31] = 1, [37] = 1, [42] = 1, [43] = 1 }
for name, sf in pairs(Deagle.surfaces) do
	if sf.fx ~= "pass" and (not sf.ron or silent[sf.ron]) then missing[#missing + 1] = name end
end
table.sort(missing)
check("see-through hulls (tree branch boxes, fences) let rounds through, like KCD2's arrows",
	Deagle.surfaces.mat_railing_wood_seethrough.fx == "pass" and Deagle.surfaces.mat_metal_seethrough.fx == "pass" and Deagle.surfaces.mat_wood.fx == "wood", nil)
check("every hit surface has an audible RoN Surface: missing " .. table.concat(missing, " "), #missing == 0, #missing)
-- steel plays the real plate files once the plugin says it can ("deagle_native_caps files"), RoN's Steel before that
local hit = { x = 1, y = 2, z = 3 }
Deagle.nativeCaps = nil
Deagle:HitSound(Deagle.surfaces.mat_metal.ron, hit, 1)
check("an older plugin: steel stays RoN's Steel: " .. M.native[#M.native], M.native[#M.native]:find("|Surface=44 ", 1, true) ~= nil, nil)
local nLoad = #M.native
Deagle:NativeCaps('"files"')
check("the plugin can play files: the plate takes load at once", #M.native == nLoad + 3 and M.native[#M.native]:find("^%[DN%] ronload file:steel/plate_0%d%.wav$") ~= nil, M.native[#M.native])
Deagle:HitSound(Deagle.surfaces.mat_metal.ron, hit, 1)
check("then steel plays a plate take with the distance law: " .. M.native[#M.native],
	M.native[#M.native]:find("^%[DN%] ron3 6%.740 1%.00 2%.00 3%.00 file:steel/plate_0%d%.wav|Falloff=3$") ~= nil, nil)
Deagle.nativeCaps = nil
-- the empty click and aiming play the gun's RoN events
n0 = #M.native
Deagle.state.ammo = 0; Deagle:Shoot(true)
check("dry fire: " .. M.native[#M.native], M.native[#M.native]:find("^%[DN%] ron [%d%.]+ event:/Weapons/FN57/FN57 Dry Fire|$") ~= nil, M.native[#M.native])
-- RoN mixes its dry clicks 37-44 dB under the shot (measured): raised to cfg.vm.dryDb of its mix (FN57 -40 dB -> -22: x7.9)
local dryVol = tonumber(M.native[#M.native]:match("^%[DN%] ron ([%d%.]+) "))
check(string.format("dry fire raised to be heard: x%.2f", dryVol or 0), dryVol and math.abs(dryVol - 10 ^ ((-22 + 40) / 20)) < 0.05, dryVol)
-- every Ready or Not handling cue can start early enough for its click to land on its moment, and every event has a click time
local late, unknown = {}, {}
for id, g in pairs(Deagle.guns) do
	for key, list in pairs(g.ronCues or {}) do
		for _, c in ipairs(list) do
			local click = Deagle.ronClick[c[2]]
			if not click then unknown[#unknown + 1] = c[2]
			else
				local r = g.rate and (g.rate[key] or g.rate.default) or 1   -- clip seconds play faster than real ones
				local need = (c.start and 0 or click / 1000) + (Deagle.cfg.vm.cueLead or 0)
				-- a sound at the clip's first frame fires at once
				if c[1] > 0 and c[1] / r < need - 0.005 then late[#late + 1] = string.format("%s %s %s %.0f ms", id, key, c[2], 1000 * (need - c[1] / r)) end
			end
		end
	end
end
check("RoN handling clicks all land on time: " .. table.concat(late, "; "), #late == 0, #late)
-- the lead is real time: on a clip played at Rate it's that many real seconds early (it was taken as clip seconds: 20 %
-- short on the Deagle's 30/24), plus the engine's output latency (cfg.vm.cueLead); the mag-in lands on the seat (frame 37)
do
	local saved, ev, cue = Deagle.gun, "event:/Weapons/Grizzly/Grizzly MagIn", nil
	Deagle.gun = Deagle.guns.deagle
	for _, c in ipairs(Deagle:RonCues("reload") or {}) do if c[2] == "ron:" .. ev then cue = c[1] end end
	local r = Deagle:Rate("reload")
	local want = 37 / 24 / r - Deagle.ronClick[ev] / 1000 - (Deagle.cfg.vm.cueLead or 0)
	check(string.format("Deagle mag-in starts %.3f s into the reload (want %.3f: seat less its click and the output latency)", (cue or 0) / r, want),
		cue and math.abs(cue / r - want) < 0.002 and (Deagle.cfg.vm.cueLead or 0) > 0, cue)
	Deagle.gun = saved
end
check("every RoN handling event has a measured click: " .. table.concat(unknown, "; "), #unknown == 0, #unknown)
-- an impact: RoN's event, its distance left to the plugin (from the listener, the free camera too) as PropagateDistance=@
n0 = #M.native
local cp = System.GetViewCameraPos()
Deagle:HitSound(8, { x = cp.x, y = cp.y + 12, z = cp.z }, 1)
local imp = M.native[#M.native]
check("impact: " .. tostring(imp), #M.native == n0 + 1 and imp:find("^%[DN%] ron3 2%.000 [%-%d%.]+ [%-%d%.]+ [%-%d%.]+ event:/Surfaces/Impacts/Bullet Impacts/Bullet Impact Surfaces|Surface=8 Occlusion=0 PropagateDistance=@ Falloff=3$") ~= nil, imp)
-- ... with our distance law (Falloff=<m>: full level within it, -6 dB per doubling) instead of RoN's own, which fades
-- impacts out by 12 m; the plugin handles the marker
local cpp = io.open("native/src/ron.cpp"):read("*a")
check("the plugin applies Falloff=", cpp:find('ps.find("Falloff=")', 1, true) ~= nil, nil)
-- free camera on: the listener is the camera, so the gun's sounds play where Henry is (they sat on the camera): the shot
-- as RoN's 3D third-person event fading with distance, handling / dry fire as 3D events at him
Deagle.fc = { camP = 0, camY = 0, token = {} }
n0 = #M.native
Deagle:CueFire("shoot", "shot")
local fcShot = M.native[#M.native]
check("free camera: the shot is the 3P event at Henry (" .. tostring(fcShot) .. ")",
	fcShot:find("^%[DN%] ron3 [%d%.]+ [%-%d%.]+ [%-%d%.]+ [%-%d%.]+ event:/Weapons/Grizzly/3P Grizzly Fire|IsOutside=%d Occlusion=0 PropagateDistance=@ Falloff=3$") ~= nil, fcShot)
Deagle:RonHere(Deagle.gun.ron.dry, 2)
local fcDry = M.native[#M.native]
check("free camera: the dry click is at Henry (" .. tostring(fcDry) .. ")", fcDry:find("^%[DN%] ron3 2%.000 [%-%d%.]+ [%-%d%.]+ [%-%d%.]+ event:/Weapons/FN57/FN57 Dry Fire|$") ~= nil, fcDry)
Deagle.fc = nil
Deagle:CueFire("shoot", "shot")
check("free camera off: the 1P shot again", M.native[#M.native]:find("^%[DN%] ron [%d%.]+ event:/Weapons/Grizzly/1P Grizzly Fire|IsOutside=%d$") ~= nil, M.native[#M.native])
-- deagle_sounds original: the gun mod's own shot and animation sounds, no RoN events from the gun
M.console("deagle_sounds", "original")
n0 = #M.native
Deagle:CueFire("shoot", "shot")
local shotLine = M.native[#M.native]
check("original sounds: the shot is the MWII Deagle's (" .. tostring(shotLine) .. ")", shotLine:find("^%[DN%] snd decho_orig/decho%-%d%.wav") ~= nil, shotLine)
local rc = Deagle:PlayCues("reload")
local first = Deagle.cuesPending and Deagle.cuesPending.list[1] and Deagle.cuesPending.list[1].file
check("original sounds: reload cues are the MWII files (" .. tostring(first) .. ")", first and first:find("^decho/p25_") ~= nil, first)
M.console("deagle_sounds", "ron")
check("deagle_sounds ron: back to Ready or Not", Deagle.cfg.sounds == "ron" and Deagle:RonSounds() ~= nil, Deagle.cfg.sounds)
Deagle:UseGun("glock_17")
check("a gun without RoN events plays its own recordings", (Deagle:ShotFile() or ""):find("%.wav$") ~= nil, Deagle:ShotFile())
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("env: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
