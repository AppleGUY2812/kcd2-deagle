-- Plays every flow with one gun in the mock game: every clip started must be in that gun's table, every sound it
-- plays must be staged under assets/build/sounds, ammo must follow the gun's magazine.
-- usage: luajit tests/run_gun.lua <gun id>   (from the repo root)
local id = arg[1] or "deagle"
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
if not Deagle:UseGun(id) then print("FAIL no gun " .. id) os.exit(1) end
local g = Deagle.guns[id]
local known, n = {}, 0
for _, a in pairs(g.anims) do
	if type(a[1]) == "table" then for _, v in ipairs(a) do known[v[1]] = true end else known[a[1]] = true end
end
local function act(a, x) Deagle:OnAction(a, x or "press") end
local log = {}
local function step(label, fn) fn(); log[#log + 1] = string.format("%-26s ammo %2d  %s", label, Deagle.state.ammo, tostring(Deagle.vmAnim and Deagle.vmAnim.name)) end
step("draw", function() act("deagle_toggle"); M.advance(2.0) end)
step("fire the magazine", function() for _ = 1, g.magSize do act("deagle_fire"); M.advance(0.25) end M.advance(1.5) end)
local roundsWhenEmpty = Deagle.vmRoundsShown
step("inspect (empty)", function() act("deagle_inspect"); M.advance(6) end)
step("empty reload", function() act("deagle_reload"); M.advance(4) end)
local ammoAfterReload = Deagle.state.ammo
step("one shot + reload", function() act("deagle_fire"); M.advance(0.5); act("deagle_reload"); M.advance(4) end)
step("inspect", function() act("deagle_inspect"); M.advance(6) end)
-- fire modes: every mode the gun has (B cycles); burst = 3 rounds a press, auto = rounds while held
local modeResults = {}
local modes = Deagle:FireModes()
for i = 1, #modes do
	local mode = Deagle:FireMode()
	Deagle.state.ammo = g.magSize; M.advance(0.5)
	local before = Deagle.state.ammo
	if mode == "auto" then
		Deagle:OnAction("deagle_fire", "press"); M.advance(0.55); Deagle:OnAction("deagle_fire", "release"); M.advance(0.5)
	else
		act("deagle_fire"); M.advance(0.5)
	end
	local after = Deagle.state.ammo; M.advance(1.0)
	modeResults[#modeResults + 1] = { mode = mode, used = before - after }
	act("deagle_firemode"); M.advance(0.1)
end
step("holster", function() act("deagle_toggle"); M.advance(2) end)
for _, l in ipairs(log) do print("   " .. l) end

local fails = 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local unknown, used = {}, {}
for _, a in ipairs(M.anims) do
	if a.layer == 0 then
		used[a.name] = true
		if not known[a.name] then unknown[#unknown + 1] = a.name end
	end
end
local missing, sounds = {}, 0
for _, c in ipairs(M.native) do
	local f = c:match("^%[DN%] snd (%S+)")
	if f then
		sounds = sounds + 1
		local h = io.open("assets/build/sounds/" .. f, "rb")
		if h then h:close() else missing[#missing + 1] = f end
	end
end
local roles = 0
for _ in pairs(used) do roles = roles + 1 end
check("every clip belongs to " .. id .. " (" .. roles .. " used)", #unknown == 0, table.concat(unknown, ", "))
check("every sound is staged (" .. sounds .. " played)", #missing == 0, table.concat(missing, ", "))
check("magazine refills to " .. g.magSize, ammoAfterReload == g.magSize, ammoAfterReload)
check("empty magazine hides its rounds", (not g.rounds) or roundsWhenEmpty == false, roundsWhenEmpty)
for _, r in ipairs(modeResults) do
	local want = (r.mode == "semi" and 1) or (r.mode == "burst" and 3) or nil
	local ok = want and r.used == want or (r.mode == "auto" and r.used >= 3)
	check(string.format("%s fire: %d round(s) per trigger", r.mode, r.used), ok, r.used)
end
local w = g.cs2 and Deagle.cs2 and Deagle.cs2[g.cs2]
if w then
	check(string.format("CS2 %s: damage %g, %d rpm, head x%.1f", g.cs2, Deagle.cfg.damage, 60 / Deagle.cfg.interval, Deagle.cfg.headshotMult * 1.5),
		Deagle.cfg.damage == w.damage and math.abs(Deagle.cfg.interval - w.cycle) < 1e-6, Deagle.cfg.damage)
end
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("%s: %d/%d checks pass", id, n - fails, n))
os.exit(fails == 0 and 0 or 1)
