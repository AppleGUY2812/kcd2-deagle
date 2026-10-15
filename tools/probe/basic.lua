-- tools/live.sh tools/probe/basic.lua (architecture step 1, Claude 5; needs the probe plugin, native/src/probe.cpp).
-- Read-only: thread ids, the typed binding (DN via IScriptTable::AddFunction), call costs, native vs Lua rays, one
-- queued ray. Results: "[Deagle] probe ..." in kcd.log, "probe ..." in deagle_native.log. Game in front (unpaused):
-- the queued ray's answer comes a frame or two later.
local clock = System.GetCurrAsyncTime
local function say(fmt, ...) System.LogAlways("[Deagle] probe " .. string.format(fmt, ...)) end
local function us(n, f) local a = clock() for i = 1, n do f(i) end return (clock() - a) * 1e6 / n end
local function v3(v) return string.format("%.3f %.3f %.3f", v.x, v.y, v.z) end

System.LogAlways("[DN] probe threads")
System.LogAlways("[DN] probe bind")
if not DN then say("bind: DN missing (see deagle_native.log)") return end
local sum = DN.add(2, 3)
local bad, msg = DN.add("x")
say("bind: DN.add(2, 3) = %s; DN.add('x') = %s, %s; DN.tid() = %s", tostring(sum), tostring(bad), tostring(msg), tostring(DN.tid()))

local empty = function() end
local n = 100000
say("calls: empty Lua function %.3f us, DN.nop %.3f us, DN.add %.3f us, [DN] text line %.3f us",
	us(n, empty), us(n, function() DN.nop() end), us(n, function(i) DN.add(i, 1) end),
	us(10000, function() System.LogAlways("[DN] probe nop") end))

local eye, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
local d40 = { x = dir.x * 40, y = dir.y * 40, z = dir.z * 40 }
local pl = Deagle:Player()
say("rays: Lua Physics.RayWorldIntersection 40 m %.2f us (native in deagle_native.log)",
	us(1000, function() local h = {} Physics.RayWorldIntersection(eye, d40, 1, ent_all, pl and pl.id, nil, h) end))
System.LogAlways(string.format("[DN] probe ray %s %s 40 1000", v3(eye), v3(dir)))
System.LogAlways(string.format("[DN] probe rayq %s %s 100", v3(eye), v3(dir)))
