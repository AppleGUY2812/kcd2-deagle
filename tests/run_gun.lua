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
local cueCheck
step("one shot + reload", function()
	act("deagle_fire"); M.advance(0.5)
	local n0, t0 = #M.native, M.t
	act("deagle_reload"); M.advance(4)
	-- the reload's sounds land where its clip is (they follow the playhead): each at its cue time / Rate, within a frame
	-- (a gun with Ready or Not handling: each RoN event starts its click time early, "ron <vol> <event>|")
	local cues = Deagle:RonCues("reload") or g.cues.reload or {}
	local worst, heard = 0, 0
	for _, c in ipairs(cues) do
		local ev = c[2]:match("^ron:(.+)$")
		local want = ev and ("ron [%d%.]+ " .. ev:gsub("%p", "%%%0") .. "|") or ("snd " .. c[2]:gsub("%p", "%%%0"))
		for i = n0 + 1, #M.native do
			if M.native[i]:find(want) then
				heard = heard + 1
				worst = math.max(worst, math.abs((M.nativeT[i] - t0) - c[1] / Deagle:Rate("reload")))
				break
			end
		end
	end
	cueCheck = { n = #cues, heard = heard, worst = worst }
end)
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
		if g.ron and M.native[i]:find("^%[DN%] ron [%d%.]+ " .. g.ron.fire:gsub("%p", "%%%0") .. "|IsOutside=[01]$") then shots = shots + 1 end   -- Ready or Not's event
	end
	modeResults[#modeResults + 1] = { mode = mode, used = before - after, started = table.concat(started, " "), shots = shots }
	act("deagle_firemode"); M.advance(0.1)
end
-- burst during an inspect: the burst cuts it (it used to stack recoil on the inspect, firing from a sideways gun)
local burstCut
local hasBurst = false
for _, m in ipairs(Deagle:FireModes()) do if m == "burst" then hasBurst = true end end
if g.anims.shootBurst and hasBurst then
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
	local up = Deagle.cfg.physics.camPerFlip * (Deagle:FeltKick(phys))
	local p0, n0, shotN = M.view.pitch, #M.native, Deagle.sprayN or 0
	act("deagle_fire")
	local first, peak   -- a string's first frame: the kick rises (riseRate), it doesn't jump
	for f = 1, 6 do M.advance(1 / 60); local d = math.deg(M.view.pitch - p0); first = first or d; peak = math.max(peak or d, d) end
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
	recoil = { up = up, peak = peak, first = first, final = final, kicks = kicks, keep = keep, string5 = string5, kickNow = kickNow,
		flash = (function() local l for _, c in ipairs(M.native) do if c:find("^%[DN%] flasharm ") then l = c end end return l end)(),
		shot = (function() local l for i = n0 + 1, #M.native do if M.native[i]:find("^%[DN%] flashshot ") then l = M.native[i] end end return l end)(),
		light = (function() local l for i = n0 + 1, #M.native do if M.native[i]:find("^%[DN%] flashfx ") then l = M.native[i] end end return l end)(),
		wantScale = Deagle.cfg.physics.flashPerUnit * phys.blast * Deagle.cfg.muzzleFxScale }
end
-- the mesh flash (flashMode "mesh"): 4 hidden cards on the muzzle bone of this gun's size; a shot shows one in its own
-- frame, a different one the next frame, then none (1 frame under 40 fps); a fresh roll for each card it hid
local mesh
if phys then
	local fm = Deagle.flashMesh
	local function shown()
		local out = {}
		for i = 1, 4 do if M.hidden["vm_flash_" .. i] == false then out[#out + 1] = i end end
		return out
	end
	local set = (phys.blast >= Deagle.cfg.physics.largeFlashBlast) and Deagle.cfg.flashMesh.large or Deagle.cfg.flashMesh.small
	mesh = { setup = fm and #fm.names == 4 and #shown() == 0, cgf = M.attachCgf and M.attachCgf.vm_flash_3, set = set }
	Deagle.cfg.flashMode = "mesh"
	M.advance(0.5, 60)
	mesh.placed = 0                                   -- on the muzzle itself once their bone resolved (not the model's origin)
	for i = 1, 4 do
		local p = M.attachPos and M.attachPos["vm_flash_" .. i]
		if p and p.x == 0 and p.y == 0 and p.z == 0 then mesh.placed = mesh.placed + 1 end
	end
	local nf, roll0 = #M.native, M.attachAngles and M.attachAngles.vm_flash_1
	M.attachAngles = {}
	Deagle.state.ammo, Deagle.state.lastShot = g.magSize, -100
	Deagle:Shoot(true)
	Deagle.vmFrame = Deagle.vmFrame + 1; Deagle:FlashMeshTick()   -- the shot's own frame's tick (a shot comes before it)
	local f0 = shown()
	M.advance(1 / 60, 60); local f1 = shown()
	M.advance(1 / 60, 60); local f2 = shown()
	local rolled = 0
	for _ in pairs(M.attachAngles) do rolled = rolled + 1 end
	local particleFlash = false
	for i = nf + 1, #M.native do if M.native[i]:find("^%[DN%] flashfx deagle%.muzzle ") or M.native[i]:find("^%[DN%] flashfx deagle%.muzzle_large ") then particleFlash = true end end
	mesh.f0, mesh.f1, mesh.f2, mesh.rolled, mesh.roll0, mesh.particleFlash = f0, f1, f2, rolled, roll0, particleFlash
	M.advance(0.5, 30)                                -- under 40 fps: one frame
	Deagle.state.lastShot = -100
	Deagle:Shoot(true)
	mesh.s0 = shown()
	M.advance(1 / 30, 30); mesh.s1 = shown()
	M.advance(0.5, 600)                               -- F9 slow motion x0.1 at 60 fps: 1/600 s of game time a frame
	Deagle.state.lastShot = -100
	Deagle:Shoot(true)
	local seen, cards = 0, {}
	for _ = 1, 60 do
		local sh = shown()
		if #sh > 0 then seen = seen + 1; cards[sh[1]] = true end
		M.advance(1 / 600, 600)
	end
	local nc = 0
	for _ in pairs(cards) do nc = nc + 1 end
	mesh.slow, mesh.slowCards = seen, nc
	-- the "ron" look (deagle_flash ron): Ready or Not's flash baked on variants x stages cards; a shot steps one
	-- variant through its stages for flashRon.time of game time
	M.console("deagle_flash", "ron")
	M.advance(0.2, 60)
	local R = Deagle.cfg.flashRon
	local n = R.variants * R.stages
	local function ronShown()
		local out = {}
		for i = 1, n do if M.hidden["vm_flash_" .. i] == false then out[#out + 1] = i end end
		return out
	end
	local rset = (phys.blast >= Deagle.cfg.physics.largeFlashBlast) and R.large or R.small
	local hiddenAll, placedAll = #ronShown() == 0, 0
	for i = 1, n do
		local p = M.attachPos and M.attachPos["vm_flash_" .. i]
		if p and p.x == 0 and p.y == 0 and p.z == 0 then placedAll = placedAll + 1 end
	end
	mesh.ron = { cards = n, hidden = hiddenAll, placed = placedAll, last = M.attachCgf and M.attachCgf["vm_flash_" .. n],
		want = "objects/manmade/weapons/deagle/vm/" .. rset .. "_" .. R.variants .. "_" .. R.stages .. ".cgf", look = Deagle.flashMesh.look.look }
	local function ronShot(fps)
		Deagle.state.lastShot = -100
		Deagle:Shoot(true)
		local frames, seq, variant, ok = 0, {}, nil, true
		for _ = 1, 200 do
			local sh = ronShown()
			if #sh > 1 then ok = false end
			if #sh == 1 then
				frames = frames + 1; seq[#seq + 1] = sh[1]
				local v = math.floor((sh[1] - 1) / R.stages)
				variant = variant or v
				if v ~= variant or (#seq > 1 and sh[1] < seq[#seq - 1]) then ok = false end
			end
			M.advance(1 / fps, fps)
		end
		return frames, ok, seq
	end
	M.advance(0.5, 60)
	mesh.ron.f60, mesh.ron.ok60, mesh.ron.seq60 = ronShot(60)
	M.advance(0.5, 600)
	mesh.ron.fslow, mesh.ron.okslow = ronShot(600)
	M.console("deagle_flash", "ours")
	M.advance(0.2, 60)
	mesh.ron.back = Deagle.flashMesh.look.look == "ours" and #Deagle.flashMesh.names == 4
	Deagle.cfg.flashMode = "gun"
	M.advance(0.5, 60)
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
-- the profile's own modes win over CS2's (the real Glock 17 is semi-auto only; CS2's Glock-18 has a burst: the user, 2026-10-13)
if g.id == "glock_17" then check("Glock 17: semi only, like the real gun (" .. table.concat(Deagle:FireModes(), ", ") .. ")", #Deagle:FireModes() == 1 and Deagle:FireModes()[1] == "semi", nil) end
if burstCut then check("a burst cuts an inspect: " .. burstCut, burstCut:find("shoot_alt/0.30", 1, true) ~= nil, burstCut) end
local w = g.cs2 and Deagle.cs2 and Deagle.cs2[g.cs2]
if w then
	check(string.format("CS2 %s: damage %g, %d rpm, head x%.1f", g.cs2, Deagle.cfg.damage, 60 / Deagle.cfg.interval, Deagle.cfg.headshotMult * 1.5),
		Deagle.cfg.damage == w.damage and math.abs(Deagle.cfg.interval - ((Deagle.cfg.fireRate == "mod" and g.cycle) or w.cycle)) < 1e-6, Deagle.cfg.damage)
end
if recoil then
	-- after a pause the recoil clock was stale: the first frame counted as 0.1 s, half the springy kick was gone at once
	check(string.format("first shot after a pause: %.2f deg on its first frame, peak %.2f of %.2f", recoil.first, recoil.peak, recoil.up),
		recoil.first < 0.85 * recoil.peak and recoil.peak > 0.8 * recoil.up, recoil.peak)
	check(string.format("cartridge kick %.1f deg: climbs %.1f, settles at %.1f (keeps ~%d%%), gun kick sent %d", recoil.up, recoil.peak, recoil.final, recoil.keep * 100, recoil.kicks),
		recoil.peak > recoil.final and recoil.peak < 1.05 * recoil.up and math.abs(recoil.final - recoil.keep * recoil.up) < 0.1 * recoil.up + 0.02 and recoil.kicks == 1, recoil.final)
	-- flashMode "gun" (the default): the shot's flash spawned on the muzzle by the plugin at the cartridge's size; the
	-- live flash is never armed (it kept burning after a load, in game 2026-10-12)
	recoil.flashName, recoil.flashScale = string.match(recoil.light or "", "^%[DN%] flashfx (%S+) ([%d.]+)")
	recoil.flashScale = tonumber(recoil.flashScale)
	check(string.format("flash on the muzzle at the cartridge size x%.2f (%s)", recoil.wantScale, tostring(recoil.light)),
		recoil.flashScale and math.abs(recoil.flashScale - recoil.wantScale) < 1e-4, recoil.flashScale)
	check("no live flash armed or shown (" .. tostring(recoil.flash or recoil.shot) .. ")", recoil.flash == nil and recoil.shot == nil, recoil.flash)
end
if phys then   -- loudness: the blast level (times each file's own evened-out gain, or RoN's own mix); the Glock plays ~0.83x the Deagle (a quarter of 6.5 dB)
	local other = Deagle.guns[id == "deagle" and "glock_17" or "deagle"]
	local mine = Deagle:ShotGain(nil, 1)
	local saved = Deagle.gun; Deagle.gun = other
	local theirs = Deagle:ShotGain(nil, 1)
	Deagle.gun = saved
	check(string.format("blast loudness x%.2f vs %s x%.2f", mine, other.id, theirs), math.max(mine, theirs) == 1 and math.abs(math.min(mine, theirs) - 0.83) < 0.02, mine)
end
if phys then   -- the wound model: 9 mm torso 35 HP by definition, .50 AE ~1.8x (CS2 has 1.77x), little loss at 50 m
	local hp, st = Deagle:Wound(phys, 0)
	local hp50 = Deagle:Wound(phys, 50)
	local want = ({ ["9x19mm"] = 35, [".50 AE"] = 62 })[phys.caliber]
	check(string.format("%s torso hit %.1f HP (%.1f at 50 m), stamina %.0f", phys.caliber, hp, hp50, st),
		(not want or math.abs(hp - want) < 1.5) and hp50 < hp and hp50 > 0.85 * hp, hp)
end
if recoil then
	local large = phys.blast >= Deagle.cfg.physics.largeFlashBlast
	local wantFx = large and Deagle.cfg.muzzleFxLarge or Deagle.cfg.muzzleFx
	check(string.format("flash %s for blast x%.2f", tostring(recoil.flashName), phys.blast), recoil.flashName == wantFx, recoil.flashName)
	check(string.format("5 fast shots climb %.1f deg, more than 5 single shots (%.1f)", recoil.string5, 5 * recoil.final), recoil.string5 > 1.1 * 5 * recoil.final, recoil.string5)   -- fast fire climbs noticeably more (a light 9 mm barely loads the grip)
	check(string.format("the barrel is still kicked %.1f deg up 0.08 s after a shot (next bullet goes there)", recoil.kickNow or 0), (recoil.kickNow or 0) > 0.3, recoil.kickNow)
	local a1, a2 = Deagle:SprayRand(3, 2), Deagle:SprayRand(3, 2)
	check("the spray pattern is the same every time (learnable)", a1 == a2 and Deagle:SprayRand(3, 2) ~= Deagle:SprayRand(4, 2), a1)
end
if mesh then
	check(string.format("mesh flash: 4 hidden cards, %s, %d put on the muzzle bone", tostring(mesh.cgf), mesh.placed),
		mesh.setup and mesh.cgf == "objects/manmade/weapons/deagle/vm/" .. mesh.set .. "_3.cgf" and mesh.roll0 ~= nil and mesh.placed == 4, mesh.cgf)
	check(string.format("mesh flash at 60 fps: card %s on the shot's frame, %s the next, %s after; %d rolled; no particle flash",
		table.concat(mesh.f0, ","), table.concat(mesh.f1, ","), table.concat(mesh.f2, ","), mesh.rolled),
		#mesh.f0 == 1 and #mesh.f1 == 1 and mesh.f1[1] == mesh.f0[1] and #mesh.f2 == 0 and mesh.rolled == 1 and not mesh.particleFlash, mesh.rolled)
	check(string.format("mesh flash under 40 fps: card %s, then %s", table.concat(mesh.s0, ","), table.concat(mesh.s1, ",")),
		#mesh.s0 == 1 and #mesh.s1 == 0, #mesh.s1)
	check(string.format("mesh flash in F9 slow motion: %d frames at 60 fps (0.035 s of game time = 21), %d variant (one shape, no switch)", mesh.slow, mesh.slowCards),
		mesh.slow >= 19 and mesh.slow <= 23 and mesh.slowCards == 1, mesh.slow)
	local r = mesh.ron
	check(string.format("Ready or Not flash (deagle_flash ron): %d hidden cards, %d on the muzzle bone, last %s", r.cards, r.placed, tostring(r.last)),
		r.look == "ron" and r.hidden and r.placed == r.cards and r.last == r.want, r.last)
	check(string.format("Ready or Not flash at 60 fps: %d frames (0.095 s = 6), one variant stepping through its stages (%s); x0.1: %d frames (57)",
		r.f60, table.concat(r.seq60, ","), r.fslow), r.f60 >= 5 and r.f60 <= 7 and r.ok60 and r.fslow >= 54 and r.fslow <= 60 and r.okslow, r.f60)
	check("deagle_flash ours: back to our 4 cards", r.back, r.back)
end
if cueCheck then
	check(string.format("reload sounds on the clip's playhead: %d/%d played, worst %.0f ms off", cueCheck.heard, cueCheck.n, cueCheck.worst * 1000),
		cueCheck.heard == cueCheck.n and cueCheck.worst <= 1 / 60 + 1e-3, cueCheck.worst)
end
check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("%s: %d/%d checks pass", id, n - fails, n))
os.exit(fails == 0 and 0 or 1)
