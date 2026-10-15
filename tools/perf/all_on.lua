-- tools/live.sh tools/perf/all_on.lua, play idle a few s, then shoot, then tools/live.sh tools/perf/all_dump.lua.
-- The wrapper itself costs time (a table per call), so read the ranking and the idle->shooting difference, not
-- absolute ms. (Claude 4, 2026-10-14; found the near-miss GetEntitiesInSphere and the tracer emitters.)
-- Profiles every function reachable from the game's global tables (the engine's and every script's): calls and time
-- per frame while idle vs while shooting. KCD2 has no debug library (rawget below), so: a timing wrapper on each.
local D = Deagle
local clock, gtime = System.GetCurrAsyncTime, System.GetCurrTime
local SKIP = { _G = true, string = true, table = true, math = true, coroutine = true, os = true, io = true, package = true,
	debug = true, bit = true, jit = true }
local A = { orig = {}, st = { idle = {}, shoot = {} }, frames = { idle = 0, shoot = 0 }, frameMs = { idle = 0, shoot = 0 },
	phase = "idle", shot0 = D.state.lastShot }
D._all = A
local n = 0
for tn, t in pairs(_G) do
	if type(t) == "table" and not SKIP[tn] and t ~= A then
		for k, f in pairs(t) do
			if type(f) == "function" and f ~= clock and f ~= gtime then
				local label = tn .. "." .. tostring(k)
				A.orig[#A.orig + 1] = { t, k, f }
				t[k] = function(...)
					local a = clock()
					local r = { f(...) }
					local s = A.st[A.phase]
					local e = s[label]
					if not e then e = { 0, 0 }; s[label] = e end
					e[1] = e[1] + 1; e[2] = e[2] + clock() - a
					return unpack(r)
				end
				n = n + 1
			end
		end
	end
end
local vt = D.VmTick   -- (already wrapped above)
A.vt = vt
local lastA, lastG
D.VmTick = function(self, ...)
	local a, g = clock(), gtime()
	if A.phase == "idle" and D.state.lastShot ~= A.shot0 then A.phase = "shoot" end
	if lastA and lastG and g > lastG then
		A.frames[A.phase] = A.frames[A.phase] + 1
		A.frameMs[A.phase] = A.frameMs[A.phase] + (a - lastA) * 1000
	end
	lastA, lastG = a, g
	return vt(self, ...)
end
System.LogAlways("[Deagle] CHECK4 all on: " .. n .. " functions wrapped; debug library " .. type(rawget(_G, "debug")))
