-- Settings kept across game starts: every deagle_set and on / off command tells the plugin (cfgsave <key> <value>,
-- native/src/settings.cpp), which replays them as deagle_set lines at the next load; replaying sets the same values
-- again; deagle_reset puts every setting back as shipped and tells the plugin to forget them.
-- usage: luajit tests/run_settings.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function sent(from)   -- cfgsave lines the plugin got since index from, as "<key> <value>"
	local out = {}
	for i = from + 1, #M.native do
		local kv = M.native[i]:match("^%[DN%] cfgsave (.+)$")
		if kv then out[#out + 1] = kv end
	end
	return out
end

local n0 = #M.native
M.console("deagle_set", "tracers 0")
M.console("deagle_tracers", "1")
M.console("deagle_tracers", "0")
M.console("deagle_motion", "0.5")
M.console("deagle_ads", "1")
M.console("deagle_hud", "0")
M.console("deagle_arms", "medieval")
M.console("deagle_set", "flashGrow 0.7 1 1.1")
M.console("deagle_set", "accuracy.scale 0.5")
M.console("deagle_set", "hearing.on off")
M.console("deagle_set", "tracer.mode flying")
M.console("deagle_paths", "1")
M.console("deagle_gun", "glock_17")
local got = sent(n0)
local want = { "tracers 0", "tracers 1", "tracers 0", "vm.motion 0.5", "vm.ads 1", "hud.enabled 0", "vm.arms medieval",
	"flashGrow 0.7,1,1.1", "accuracy.scale 0.5", "hearing.on 0", "tracer.mode flying", "debugPaths 1", "gun glock_17" }
check("every setting command is kept: " .. table.concat(got, " | "), table.concat(got, "|") == table.concat(want, "|"), #got)
local c = Deagle.cfg
check("and applied (tracers off, motion 0.5, ads on, hud off, medieval arms, flashGrow, accuracy 0.5, hearing off, flying, paths, Glock)",
	c.tracers == false and c.vm.motion == 0.5 and c.vm.ads == true and c.hud.enabled == false and c.vm.arms == "medieval"
	and #c.flashGrow == 3 and c.accuracy.scale == 0.5 and c.hearing.on == false and c.tracer.mode == "flying"
	and c.debugPaths == true and Deagle.gun.id == "glock_17", Deagle.gun.id)

-- a load: the plugin replays what it kept as deagle_set lines (settings.cpp SettingsReplay), after the handshake
M.console("deagle_reset")
check("deagle_reset: the plugin forgets them", M.native[#M.native] ~= nil and (function()
	for i = n0 + 1, #M.native do if M.native[i] == "[DN] cfgreset" then return true end end end)(), M.native[#M.native])
check("deagle_reset: shipped values back (Deagle, tracers on, motion 1, ads off, hud on, mw arms, accuracy 1, trail, no paths)",
	Deagle.gun.id == "deagle" and c.tracers == true and c.vm.motion == 1 and c.vm.ads == false and c.hud.enabled == true and c.vm.arms == "mw"
	and c.accuracy.scale == 1 and c.tracer.mode == "trail" and c.debugPaths == false and #c.flashGrow == 0, c.vm.motion)
Deagle:OnGameplayStarted()
local hello = M.native[#M.native]
for _, kv in ipairs(want) do M.console("deagle_set", kv) end
check("replayed at a load: " .. tostring(hello) .. ", then the same values again",
	hello == "[DN] hello" and c.tracers == false and c.vm.motion == 0.5 and c.vm.arms == "medieval" and c.accuracy.scale == 0.5
	and c.tracer.mode == "flying" and Deagle.gun.id == "glock_17" and table.concat(c.flashGrow, ",") == "0.7,1,1.1", c.vm.motion)
M.console("deagle_reset")
-- the free camera's look mode: the plugin says it's on; Lua aims the camera every frame (freecam rot) and stops at off
local nf = #M.native
M.console("deagle_freecam_state", "1"); M.advance(0.1)
local rots = 0
for i = nf + 1, #M.native do if M.native[i]:find("^%[DN%] freecam rot ") then rots = rots + 1 end end
M.console("deagle_freecam_state", "0"); M.advance(0.1)
local after = 0
for i = nf + 1, #M.native do if M.native[i]:find("^%[DN%] freecam rot ") then after = after + 1 end end
check(string.format("freecam look: %d frames aimed while on, none after off, rotoff sent", rots),
	rots >= 5 and after == rots and M.native[#M.native] == "[DN] freecam rotoff" or (function()
		for i = #M.native, nf + 1, -1 do if M.native[i] == "[DN] freecam rotoff" then return after == rots and rots >= 5 end end end)(), rots)
-- the look scale is learned from Henry's own turning (free camera off), then the mouse turns the camera by it
Deagle.lookScale = { x = { sv = 0, sa = 0 }, y = { sv = 0, sa = 0 } }; Deagle.lookLast = nil
for i = 1, 30 do M.view.yaw = M.view.yaw + 0.004 * 5; Deagle:OnAction("deagle_look_x", "", 5) end   -- 0.004 rad per unit
local k = Deagle.lookScale.x.k
M.console("deagle_freecam_state", "1")
local y0 = Deagle.fc.camY
Deagle:OnAction("deagle_look_x", "", 10)
local turned = Deagle.fc.camY - y0
M.console("deagle_freecam_state", "0")
check(string.format("freecam look: learned %.4f rad per mouse unit (Henry 0.004), 10 units turn the camera %.3f rad",
	k or -1, turned), k and math.abs(k - 0.004) < 0.0005 and math.abs(turned - 0.04) < 0.005, k)

-- a misspelt name is refused (it used to become a new setting nothing reads, and was kept): nothing set, nothing
-- kept, and the close names suggested
local nT, logs0 = #M.native, #M.logs
M.console("deagle_set", "shotSounds 0")
M.console("deagle_set", "physics.camPerFlp 0.3")
local hint1, hint2
for i = logs0 + 1, #M.logs do
	hint1 = hint1 or M.logs[i]:match("no setting shotSounds.-(shotSound)")
	hint2 = hint2 or M.logs[i]:match("no setting physics%.camPerFlp.-(physics%.camPerFlip)")
end
check(string.format("misspelt settings refused with a hint (%s, %s), nothing kept", tostring(hint1), tostring(hint2)),
	Deagle.cfg.shotSounds == nil and Deagle.cfg.physics.camPerFlp == nil and #sent(nT) == 0 and hint1 and hint2, #sent(nT))
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("settings: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
