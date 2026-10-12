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
local roundsAfterOne, ammoAfterOne
step("fire the magazine", function()
	for i = 1, g.magSize do act("deagle_fire"); M.advance(0.25); if i == 1 then roundsAfterOne, ammoAfterOne = Deagle.vmRoundsShown, Deagle.state.ammo end end
	M.advance(1.5) end)
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
	local before, a0, n0 = Deagle.state.ammo, #M.anims, #M.native
	if mode == "auto" then
		Deagle:OnAction("deagle_fire", "press"); M.advance(0.55); Deagle:OnAction("deagle_fire", "release"); M.advance(0.5)
	else
		act("deagle_fire"); M.advance(0.5)
	end
	local after = Deagle.state.ammo; M.advance(1.0)
	local started = {}
	for i = a0 + 1, #M.anims do started[#started + 1] = M.anims[i].name .. "@" .. M.anims[i].layer end
	local shots = 0
	for i = n0 + 1, #M.native do
		local f = M.native[i]:match("^%[DN%] snd (%S+)")
		for _, sf in ipairs(g.shotSounds or {}) do if f == sf then shots = shots + 1 end end
	end
	modeResults[#modeResults + 1] = { mode = mode, used = before - after, started = table.concat(started, " "), shots = shots }
	act("deagle_firemode"); M.advance(0.1)
end
-- burst during an inspect: the burst cuts it (it used to stack recoil on the inspect, firing from a sideways gun)
local burstCut
if g.anims.shootBurst then
	while Deagle:FireMode() ~= "burst" do act("deagle_firemode"); M.advance(0.1) end
	Deagle.state.ammo = g.magSize; M.advance(1.0)
	act("deagle_inspect"); M.advance(1.0)
	local a0 = #M.anims
	act("deagle_fire"); M.advance(0.5)
	burstCut = M.anims[a0 + 1] and (M.anims[a0 + 1].name .. "/" .. string.format("%.2f", M.anims[a0 + 1].blend or -1)) or "nothing"
	M.advance(1.0)
end
-- cartridge recoil: one shot climbs the view by the physics kick; the elastic hold takes back what fits in its range
local recoil
local phys = Deagle:ShotPhysics()
if phys then
	while Deagle:FireMode() ~= "semi" do act("deagle_firemode"); M.advance(0.1) end
	Deagle.state.ammo = g.magSize; M.advance(1.5)
	local up = Deagle.cfg.physics.camPerFlip * phys.flip
	local p0, n0, shotN = M.view.pitch, #M.native, Deagle.sprayN or 0
	act("deagle_fire"); M.advance(0.1)
	local peak = math.deg(M.view.pitch - p0)
	M.advance(1.5)
	local final = math.deg(M.view.pitch - p0)
	local kicks = 0
	for i = n0 + 1, #M.native do if M.native[i]:find("^%[DN%] kick ") then kicks = kicks + 1 end end
	local hold = Deagle.cfg.physics
	up = up * (1 + hold.vary * Deagle:SprayRand(shotN, 1))     -- this shot's kick (the learnable pattern varies it)
	local keep = 1 - math.min(hold.holdMax * up, hold.holdRange) / up
	-- a fast string (0.08 s apart) finds the hold still loaded: it climbs far more than single shots would
	M.advance(2.0)
	local p1 = M.view.pitch
	local kickNow
	for i = 1, 5 do act("deagle_fire"); M.advance(0.08); if i == 1 then kickNow = select(1, Deagle:GunKickNow()) end end
	M.advance(2.0)
	local string5 = math.deg(M.view.pitch - p1)
	recoil = { up = up, peak = peak, final = final, kicks = kicks, keep = keep, string5 = string5, kickNow = kickNow,
		flashScale = M.lastAttachScale, flashPrime = false, wantScale = Deagle.cfg.physics.flashPerUnit * phys.blast * Deagle.cfg.muzzleFxScale }
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
check("empty magazine hides its rounds", (not g.rounds) or roundsWhenEmpty == 0, roundsWhenEmpty)
check(string.format("magazine shows what it holds (%s in it after the first shot, %s modelled)", tostring(roundsAfterOne), tostring(g.rounds)),
	(not g.rounds) or g.rounds == true or roundsAfterOne == ammoAfterOne - 1, roundsAfterOne)   -- one round is in the chamber
for _, r in ipairs(modeResults) do
	local want = (r.mode == "semi" and 1) or (r.mode == "burst" and 3) or nil
	local ok = want and r.used == want or (r.mode == "auto" and r.used >= 3)
	check(string.format("%s fire: %d round(s) per trigger", r.mode, r.used), ok, r.used)
	check(string.format("%s fire: a gunshot per round (%d for %d)", r.mode, r.shots, r.used), r.shots == r.used, r.shots)
	if r.mode == "burst" and g.anims.shootBurst then
		local _, n = r.started:gsub(g.anims.shootBurst[1] .. "@0", "")
		check("burst plays its burst clip once: " .. r.started, n == 1, r.started)
	end
end
if burstCut then check("a burst cuts an inspect: " .. burstCut, burstCut:find("shoot_alt/0.30", 1, true) ~= nil, burstCut) end
local w = g.cs2 and Deagle.cs2 and Deagle.cs2[g.cs2]
if w then
	check(string.format("CS2 %s: damage %g, %d rpm, head x%.1f", g.cs2, Deagle.cfg.damage, 60 / Deagle.cfg.interval, Deagle.cfg.headshotMult * 1.5),
		Deagle.cfg.damage == w.damage and math.abs(Deagle.cfg.interval - ((Deagle.cfg.fireRate == "mod" and g.cycle) or w.cycle)) < 1e-6, Deagle.cfg.damage)
end
if recoil then
	check(string.format("cartridge kick %.1f deg: climbs %.1f, settles at %.1f (keeps ~%d%%), gun kick sent %d", recoil.up, recoil.peak, recoil.final, recoil.keep * 100, recoil.kicks),
		recoil.peak > recoil.final and recoil.peak < 1.05 * recoil.up and math.abs(recoil.final - recoil.keep * recoil.up) < 0.1 * recoil.up + 0.02 and recoil.kicks == 1, recoil.final)
	check(string.format("flash on the muzzle at the cartridge size x%.2f (got %s)", recoil.wantScale, tostring(recoil.flashScale)),
		recoil.flashScale and math.abs(recoil.flashScale - recoil.wantScale) < 1e-6 and recoil.flashPrime == false, recoil.flashScale)
end
if phys then   -- the wound model: 9 mm torso 35 HP by definition, .50 AE ~1.8x (CS2 has 1.77x), little loss at 50 m
	local hp, st = Deagle:Wound(phys, 0)
	local hp50 = Deagle:Wound(phys, 50)
	local want = ({ ["9x19mm"] = 35, [".50 AE"] = 62 })[phys.caliber]
	check(string.format("%s torso hit %.1f HP (%.1f at 50 m), stamina %.0f", phys.caliber, hp, hp50, st),
		(not want or math.abs(hp - want) < 1.5) and hp50 < hp and hp50 > 0.85 * hp, hp)
end
if recoil then
	check(string.format("5 fast shots climb %.1f deg, more than 5 single shots (%.1f)", recoil.string5, 5 * recoil.final), recoil.string5 > 1.2 * 5 * recoil.final, recoil.string5)
	check(string.format("the barrel is still kicked %.1f deg up 0.08 s after a shot (next bullet goes there)", recoil.kickNow or 0), (recoil.kickNow or 0) > 0.3, recoil.kickNow)
	local a1, a2 = Deagle:SprayRand(3, 2), Deagle:SprayRand(3, 2)
	check("the spray pattern is the same every time (learnable)", a1 == a2 and Deagle:SprayRand(3, 2) ~= Deagle:SprayRand(4, 2), a1)
end
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("%s: %d/%d checks pass", id, n - fails, n))
os.exit(fails == 0 and 0 or 1)
