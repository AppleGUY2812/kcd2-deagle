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
-- made once per gun (it was a new table ~10 times a shot): the same table again, and a change of the gun's data gives a new one
if phys then
	local again = Deagle:ShotPhysics()
	local copy = {}
	for k, v in pairs(Deagle.gun) do copy[k] = v end
	local other = Deagle:ShotPhysics(copy)
	print(string.format("ShotPhysics: the same table on a second ask %s, a copy of the gun gets its own %s with the same speed %s", tostring(again == phys),
		tostring(other ~= phys), tostring(other and other.velocity == phys.velocity)))
	if not (again == phys and other ~= phys and other.velocity == phys.velocity) then error("ShotPhysics cache") end
end
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
-- the mesh flash (flashMode "mesh"): the .cdf's cards, skins of this gun on its muzzle bone (tools/flash_cdf.py), all
-- hidden at load; the 16 of this gun's size (4 shapes x 4 turns round the barrel: the roll) are the look's; a shot shows
-- one in its own frame, the same the next frame, then none (1 frame under 40 fps). Nothing is attached, loaded, moved or
-- turned at run time (bone attachments drew the first card's model in KCD2; skins follow their bone)
local mesh
if phys then
	local fm = Deagle.flashMesh
	local function shown()
		local out = {}
		for i = 1, #Deagle.flashMesh.names do if M.hidden[Deagle.flashMesh.names[i]] == false then out[#out + 1] = i end end
		return out
	end
	local cdfCards, cdfShown = 0, 0
	for n in pairs(M.cdfAttach or {}) do
		if n:find("^deagle_vm_flash") and M.cdfAttach[n] == "CA_SKIN" then cdfCards = cdfCards + 1; if M.hidden[n] ~= true then cdfShown = cdfShown + 1 end end
	end
	local set = (phys.blast >= Deagle.cfg.physics.largeFlashBlast) and Deagle.cfg.flashMesh.large or Deagle.cfg.flashMesh.small
	local roll = Deagle.cfg.flashMesh.roll
	Deagle.cfg.flashMesh.roll = false; local unturned = Deagle:FlashLook().variants; Deagle.cfg.flashMesh.roll = roll
	mesh = { setup = fm and #fm.names == 16 and #shown() == 0, cgf = M.attachCgf and fm and M.attachCgf[fm.names[3]], set = set,
		cdfCards = cdfCards, cdfShown = cdfShown, want = #Deagle:FlashCardNames(), unturned = unturned }
	Deagle.cfg.flashMode = "mesh"
	M.advance(0.5, 60)
	local nf = #M.native
	Deagle.state.ammo, Deagle.state.lastShot = g.magSize, -100
	Deagle:Shoot(true)
	Deagle.vmFrame = Deagle.vmFrame + 1; Deagle:FlashMeshTick()   -- the shot's own frame's tick (a shot comes before it)
	local f0 = shown()
	local lights = {}
	for slot, l in pairs(M.lights or {}) do lights[slot] = l end
	M.advance(1 / 60, 60); local f1 = shown()
	M.advance(1 / 60, 60); local f2 = shown()
	M.advance(1 / 60, 60)
	local lightsLeft = 0
	for _ in pairs(M.lights or {}) do lightsLeft = lightsLeft + 1 end
	mesh.lights, mesh.lightsLeft = lights, lightsLeft
	local particleFlash = false
	for i = nf + 1, #M.native do if M.native[i]:find("^%[DN%] flashfx deagle%.muzzle ") or M.native[i]:find("^%[DN%] flashfx deagle%.muzzle_large ") then particleFlash = true end end
	mesh.f0, mesh.f1, mesh.f2, mesh.particleFlash = f0, f1, f2, particleFlash
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
		for i = 1, n do if M.hidden[Deagle.flashMesh.names[i]] == false then out[#out + 1] = i end end
		return out
	end
	local rset = (phys.blast >= Deagle.cfg.physics.largeFlashBlast) and R.large or R.small
	mesh.ron = { cards = n, hidden = #ronShown() == 0, last = M.attachCgf and M.attachCgf[Deagle.flashMesh.names[n]],
		want = "/flash/" .. rset .. "_" .. R.variants .. "_" .. R.stages .. ".skin", look = Deagle.flashMesh.look.look }
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
	mesh.ron.back = Deagle.flashMesh.look.look == "ours" and #Deagle.flashMesh.names == 16
	mesh.ron.leftOn = 0                               -- no Ready or Not card left showing after the switch back
	for n in pairs(M.cdfAttach or {}) do if n:find("_ron") and M.hidden[n] ~= true then mesh.ron.leftOn = mesh.ron.leftOn + 1 end end
	mesh.runtime = (M.boneAttach or 0) + (M.runtimeCgf or 0)
	for k in pairs(M.attachPos or {}) do if k:find("^deagle_vm_flash") then mesh.runtime = mesh.runtime + 1 end end
	for k in pairs(M.attachAngles or {}) do if k:find("^deagle_vm_flash") then mesh.runtime = mesh.runtime + 1 end end
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
	check(string.format("mesh flash: 16 hidden cards (4 with roll off: %d), card 3 %s", mesh.unturned, tostring(mesh.cgf)),
		mesh.setup and mesh.unturned == 4 and tostring(mesh.cgf):find("/flash/" .. mesh.set .. "_3%.skin$") ~= nil, mesh.cgf)
	check(string.format("mesh flash at 60 fps: card %s on the shot's frame, %s the next, %s after; no particle flash",
		table.concat(mesh.f0, ","), table.concat(mesh.f1, ","), table.concat(mesh.f2, ",")),
		#mesh.f0 == 1 and #mesh.f1 == 1 and mesh.f1[1] == mesh.f0[1] and #mesh.f2 == 0 and not mesh.particleFlash, #mesh.f2)
	local l3, l4 = mesh.lights[3], mesh.lights[4]
	-- the colour Ready or Not's light adds on screen (red-orange), at the cartridge's blast (Glock 0.48 of the Deagle)
	local function red(l) local c = l and l.props.diffuse_color return c and c.x > 0 and c.y > 0.2 * c.x and c.y < 0.35 * c.x and c.z < c.y end
	local wantI = Deagle.cfg.flashMesh.light.intensity * phys.blast / Deagle.cfg.flashMesh.light.refBlast
	check(string.format("flash light: Ready or Not's two lights on the shot (radius %s / %s m: no visible edge, red-orange at the blast %s, fog %s), gone after 0.04 s (%d left)",
		tostring(l3 and l3.props.radius), tostring(l4 and l4.props.radius), tostring(red(l3) and red(l4)),
		tostring(l3 and l3.props.volumetric_fog), mesh.lightsLeft),
		l3 and l4 and l3.props.radius >= 30 and l4.props.radius >= 30 and red(l3) and red(l4) and l3.props.volumetric_fog == false
		and l4.props.diffuse_color.x < l3.props.diffuse_color.x and math.abs(l3.props.diffuse_color.x - wantI) < 1e-6 and l3.pos ~= nil
		and mesh.lightsLeft == 0, mesh.lightsLeft)
	check(string.format("mesh flash under 40 fps: card %s, then %s", table.concat(mesh.s0, ","), table.concat(mesh.s1, ",")),
		#mesh.s0 == 1 and #mesh.s1 == 0, #mesh.s1)
	check(string.format("mesh flash in F9 slow motion: %d frames at 60 fps (0.035 s of game time = 21), %d variant (one shape, no switch)", mesh.slow, mesh.slowCards),
		mesh.slow >= 19 and mesh.slow <= 23 and mesh.slowCards == 1, mesh.slow)
	local r = mesh.ron
	check(string.format("Ready or Not flash (deagle_flash ron): %d hidden cards, last %s", r.cards, tostring(r.last)),
		r.look == "ron" and r.hidden and tostring(r.last):sub(-#r.want) == r.want, r.last)
	check(string.format("Ready or Not flash at 60 fps: %d frames (0.095 s = 6), one variant stepping through its stages (%s); x0.1: %d frames (57)",
		r.f60, table.concat(r.seq60, ","), r.fslow), r.f60 >= 5 and r.f60 <= 7 and r.ok60 and r.fslow >= 54 and r.fslow <= 60 and r.okslow, r.f60)
	check(string.format("deagle_flash ours: back to our 16 cards, %d Ready or Not cards left showing", r.leftOn), r.back and r.leftOn == 0, r.leftOn)
	check(string.format("flash card skins from the .cdf: %d (%d expected), %d showing at load; %d attached, loaded, moved or turned at run time",
		mesh.cdfCards, mesh.want, mesh.cdfShown, mesh.runtime), mesh.cdfCards == mesh.want and mesh.cdfShown == 0 and mesh.runtime == 0, mesh.runtime)
end
if cueCheck then
	check(string.format("reload sounds on the clip's playhead: %d/%d played, worst %.0f ms off", cueCheck.heard, cueCheck.n, cueCheck.worst * 1000),
		cueCheck.heard == cueCheck.n and cueCheck.worst <= 1 / 60 + 1e-3, cueCheck.worst)
end
-- worn gear is looked for when first person starts and once a second later, not every second: each search is a
-- 5-9 ms entity query (a stutter a second, the audit 2026-10-15)
do
	if not Deagle.vmShown then act("deagle_toggle"); M.advance(2.0) end
	local getEnts, n = System.GetEntitiesInSphere, 0
	System.GetEntitiesInSphere = function(...) n = n + 1 return getEnts(...) end
	M.advance(10, 60)
	System.GetEntitiesInSphere = getEnts
	check(string.format("worn gear: %d entity queries in 10 s with the gun up in first person (%s)", n, tostring(Deagle.vmInFp)),
		Deagle.vmInFp and n <= 1, n)
end

-- deagle_stress: 21 s of game time, fires only while looking up, logs every 0.5 s, puts everything back
do
	local pitch, tracers, fireWas = M.view.pitch, Deagle.cfg.tracers, Deagle.Fire
	local shots = 0
	if not Deagle.vmShown then act("deagle_toggle"); M.advance(2.0) end
	M.view.pitch = 1.2
	M.console("deagle_stress")
	local wrapped = Deagle.Fire ~= fireWas
	local f = Deagle.Fire
	Deagle.Fire = function(...) shots = shots + 1 return f(...) end
	M.advance(22, 60)
	local lines, done, phases = 0, false, 0
	for _, l in ipairs(M.logs) do
		if l:find("STRESS%s+%d") then lines = lines + 1 end
		if l:find("STRESS done") then done = true end
		if l:find("STRESS == ") then phases = phases + 1 end
	end
	check(string.format("deagle_stress: %d shots, %d log lines, %d phase summaries, done %s, functions back %s, tracers back %s", shots, lines,
		phases, tostring(done), tostring(Deagle.stress == nil), tostring(Deagle.cfg.tracers == tracers)),
		wrapped and shots >= 90 and shots <= 110 and lines >= 40 and phases == 6 and done and Deagle.stress == nil and Deagle.cfg.tracers == tracers, shots)
	-- a CIWS rate: 75 rounds a second, more than one shot in some frames, ~750 over the two 5 s fire phases
	shots = 0
	M.console("deagle_stress", "75")
	f = Deagle.Fire
	Deagle.Fire = function(...) shots = shots + 1 return f(...) end
	M.advance(22, 60)
	local started = false
	for _, l in ipairs(M.logs) do if l:find("STRESS start: 75 rounds a second") then started = true end end
	check(string.format("deagle_stress 75: %d shots in two 5 s phases (750 due), started %s, done %s", shots, tostring(started), tostring(Deagle.stress == nil)),
		started and shots >= 700 and shots <= 760 and Deagle.stress == nil, shots)
	-- the impact A/B: 9 phases, each part of a hit off in its own phase and back afterwards
	local fxWas, decalWas, soundWas = Deagle.SpawnFx, Particle.CreateMatDecal, Deagle.HitSound
	M.console("deagle_stress", "20 aim ab")
	M.advance(32, 60)
	local summaries = 0
	for _, l in ipairs(M.logs) do if l:find("STRESS == FIRE no") or l:find("STRESS == FIRE all") then summaries = summaries + 1 end end
	check(string.format("deagle_stress 20 aim ab: %d fire-phase summaries (4), effects / holes / sounds put back %s %s %s", summaries,
		tostring(Deagle.SpawnFx == fxWas), tostring(Particle.CreateMatDecal == decalWas), tostring(Deagle.HitSound == soundWas)),
		summaries == 4 and Deagle.SpawnFx == fxWas and Particle.CreateMatDecal == decalWas and Deagle.HitSound == soundWas and Deagle.stress == nil, summaries)
	-- the shot A/B: 8 fire phases, each part of the shot off in its own phase, everything back afterwards
	local names = { "MuzzleFlash", "EjectCase", "CueFire", "RonHere", "ShotHeard", "VmPlay", "VmRecoilAdd", "VmRounds" }
	local was = {}
	for _, k in ipairs(names) do was[k] = Deagle[k] end
	local flashes, flashOff = 0, nil
	local mf = Deagle.MuzzleFlash
	Deagle.MuzzleFlash = function(...) flashes = flashes + 1 return mf(...) end
	was.MuzzleFlash = Deagle.MuzzleFlash
	M.view.pitch = 1.2
	M.console("deagle_stress", "20 sky shot")
	local inNoFlash = 0
	for _ = 1, 50 * 60 do
		M.advance(1 / 60)
		if Deagle.stress and Deagle.MuzzleFlash ~= was.MuzzleFlash then inNoFlash = inNoFlash + 1 end
		if not Deagle.stress then break end
	end
	summaries = 0
	local shotStart = false
	for _, l in ipairs(M.logs) do
		if l:find("STRESS == FIRE ") then summaries = summaries + 1 end
		if l:find("STRESS start: 20 rounds a second, into the sky, shot A/B") then shotStart = true end
	end
	local back = true
	for _, k in ipairs(names) do if Deagle[k] ~= was[k] then back = false end end
	check(string.format("deagle_stress 20 sky shot: started %s, %d fire-phase summaries (8 + the 8 above), flash off for %d frames, %d flashes, parts back %s, done %s",
		tostring(shotStart), summaries, inNoFlash, flashes, tostring(back), tostring(Deagle.stress == nil)),
		shotStart and summaries == 16 and inNoFlash > 300 and flashes > 0 and back and Deagle.stress == nil, summaries)
	Deagle.MuzzleFlash = mf
	-- at 5 fps (from the first fire phase on) a fire phase ends at the 150 ms guard ("cliff") instead of the run, and a rest that never gets back under
	-- 40 ms stops the run after 10 s
	M.console("deagle_stress", "20 sky shot")
	M.advance(2.5)
	M.advance(14, 5)
	local cliff, stuck = false, false
	for _, l in ipairs(M.logs) do
		if l:find("STRESS cliff: FIRE all") then cliff = true end
		if l:find("STRESS aborted: frames still over 40 ms after 10 s of rest") then stuck = true end
	end
	check(string.format("deagle_stress shot at 5 fps: cliff ends the phase %s, stuck rest stops the run %s, done %s", tostring(cliff), tostring(stuck), tostring(Deagle.stress == nil)),
		cliff and stuck and Deagle.stress == nil and Deagle.MuzzleFlash == mf, 0)
	-- a fourth word picks the parts: the casing's body and its port smoke on their own
	local ejWas, fxWas2 = Deagle.EjectCase, Deagle.SpawnFx
	local noBodySmokes, inBody = 0, false
	local sf = Deagle.SpawnFx
	Deagle.SpawnFx = function(s, name, ...)
		if name == s.cfg.portFx and inBody then noBodySmokes = noBodySmokes + 1 end
		return sf(s, name, ...)
	end
	local sfWrapped = Deagle.SpawnFx
	local n0 = #M.logs
	M.console("deagle_stress", "20 sky shot casebody,portsmoke,nope")
	local smokeDuring = 0
	for _ = 1, 30 * 60 do
		M.advance(1 / 60)
		inBody = Deagle.stress ~= nil and Deagle.EjectCase ~= ejWas
		if Deagle.stress and Deagle.SpawnFx ~= sfWrapped then smokeDuring = smokeDuring + 1 end
		if not Deagle.stress then break end
	end
	local named, badName = 0, false
	for i = n0 + 1, #M.logs do
		local l = M.logs[i]
		if l:find("STRESS == FIRE no casebody") or l:find("STRESS == FIRE no portsmoke") then named = named + 1 end
		if l:find("no part 'nope'") then badName = true end
	end
	local keep = {}
	for _, e in ipairs(M.errors) do if not e:find("no part 'nope'") then keep[#keep + 1] = e end end
	M.errors = keep   -- the wrong name's warning was the point
	check(string.format("deagle_stress 20 sky shot casebody,portsmoke: %d named phases (2), smoke with no body %d, smoke filter on %d frames, wrong name said %s, back %s %s",
		named, noBodySmokes, smokeDuring, tostring(badName), tostring(Deagle.EjectCase == ejWas), tostring(Deagle.SpawnFx == sfWrapped)),
		named == 2 and noBodySmokes > 0 and smokeDuring > 100 and badName and Deagle.EjectCase == ejWas and Deagle.SpawnFx == sfWrapped and Deagle.stress == nil, named)
	Deagle.SpawnFx = fxWas2
	-- a fire phase that couldn't fire (the view off the sky) says so: THROTTLED, with the held frames
	n0 = #M.logs
	M.console("deagle_stress", "20")
	for _ = 1, 8 * 60 do M.view.pitch = 0; M.advance(1 / 60) end   -- held level: no recoil left over lifts it
	Deagle:StressStop("test")
	local throttled = false
	for i = n0 + 1, #M.logs do if M.logs[i]:find("STRESS == FIRE tracers on .* 0 rounds launched, %d+ frames held THROTTLED") then throttled = true end end
	check("deagle_stress with the view off the sky: the fire phase is marked THROTTLED with its held frames " .. tostring(throttled), throttled, 0)
	-- 1000 rounds a second: ~17 kicks a frame never turn the view past the pole (it flipped over to 17 deg up), so the
	-- run keeps firing, every due shot launched; and the view never ends below where it started
	M.view.pitch = 1.2
	n0 = #M.logs
	M.console("deagle_stress", "1000")
	local topPitch = 0
	for _ = 1, 60 * 7.5 do M.advance(1 / 60); topPitch = math.max(topPitch, M.view.pitch) end
	Deagle:StressStop("test")
	M.advance(4)
	local all = false
	for i = n0 + 1, #M.logs do
		local sh, due, la = M.logs[i]:match("STRESS == FIRE tracers on .*shots (%d+) of (%d+) due, (%d+) rounds launched")
		if sh and tonumber(la) >= 0.9 * tonumber(due) then all = true end
	end
	check(string.format("deagle_stress 1000 sky: view at most %.1f deg (88), after %.1f deg (from 68.8), every due shot launched %s",
		math.deg(topPitch), math.deg(M.view.pitch), tostring(all)),
		topPitch <= math.rad(88) + 1e-4 and M.view.pitch >= 1.2 - 1e-3 and all, topPitch)
	M.view.pitch = 1.2
	-- the profile: every Deagle function and the engine ones it calls timed through the fire phase, then all put back
	local fireWas3, getEntWas, spawnWas = Deagle.Fire, System.GetEntity, Particle.SpawnEffect
	n0 = #M.logs
	M.console("deagle_stress", "20 sky prof")
	for _ = 1, 60 * 9 do M.view.pitch = 1.2; M.advance(1 / 60) end
	local profOn, rows, shotsSeen, engineRow = false, 0, 0, false
	for i = n0 + 1, #M.logs do
		local l = M.logs[i]
		if l:find("PROF on: %d+ functions timed") then profOn = true end
		if l:find("PROF +%d+ ") then rows = rows + 1 end
		local sh = l:match("PROF [^:]*: %d+ frames, (%d+) shots") if sh then shotsSeen = tonumber(sh) end
		if l:find("PROF +%d+ System%.") or l:find("PROF +%d+ Physics%.") then engineRow = true end
	end
	check(string.format("deagle_stress 20 sky prof: on %s, %d rows, %d shots profiled (80), an engine call listed %s, all put back %s %s %s, done %s",
		tostring(profOn), rows, shotsSeen, tostring(engineRow), tostring(Deagle.Fire == fireWas3), tostring(System.GetEntity == getEntWas),
		tostring(Particle.SpawnEffect == spawnWas), tostring(Deagle.stress == nil)),
		profOn and rows >= 10 and shotsSeen >= 75 and shotsSeen <= 85 and engineRow and Deagle.Fire == fireWas3 and System.GetEntity == getEntWas
		and Particle.SpawnEffect == spawnWas and Deagle.stress == nil, rows)
	-- several rates in one profile run: a profiled fire phase each, its own shots
	n0 = #M.logs
	M.console("deagle_stress", "20,40 sky prof")
	if not Deagle.stress then Deagle:Stress("20,40 sky prof") end   -- (a mock %1 command takes one word)
	for _ = 1, 60 * 15 do M.view.pitch = 1.2; M.advance(1 / 60) end
	local per = {}
	for i = n0 + 1, #M.logs do
		local r, sh = M.logs[i]:match("PROF FIRE profiled (%d+): %d+ frames, (%d+) shots")
		if r then per[tonumber(r)] = tonumber(sh) end
	end
	check(string.format("deagle_stress 20,40 sky prof: shots profiled at 20/s %s (80), at 40/s %s (160), done %s", tostring(per[20]), tostring(per[40]),
		tostring(Deagle.stress == nil)), per[20] and per[40] and math.abs(per[20] - 80) <= 5 and math.abs(per[40] - 160) <= 8 and Deagle.stress == nil, per[20] or 0)
	-- spawn mode: a registered spawner N times a second at one spot, 10 s, no gun (Claude 5's plan step 4)
	local calls, shotsBefore = 0, shots
	Deagle.StressSpawners.test = function(pos, normal, i) calls = calls + 1 end
	M.console("deagle_lua", "Deagle:Stress('spawn test 30')")
	if not Deagle.stress then Deagle:Stress("spawn test 30") end
	M.advance(16, 60)
	local spawnStart = false
	for _, l in ipairs(M.logs) do if l:find("STRESS start: spawn test, 30 a second") then spawnStart = true end end
	check(string.format("deagle_stress spawn test 30: %d spawner calls in 10 s (300 due), %d shots, started %s, done %s", calls, shots - shotsBefore,
		tostring(spawnStart), tostring(Deagle.stress == nil)), spawnStart and calls >= 290 and calls <= 310 and shots == shotsBefore and Deagle.stress == nil, calls)
	Deagle.StressSpawners.test = nil
	-- deagle_stress stop ends a run at once and puts everything back
	local vtWas = Deagle.VmTick
	M.console("deagle_stress", "75")
	local running = Deagle.stress ~= nil
	M.advance(3, 60)
	M.console("deagle_stress", "stop")
	check(string.format("deagle_stress stop: was running %s, stopped %s, functions back %s", tostring(running), tostring(Deagle.stress == nil),
		tostring(Deagle.VmTick == vtWas)), running and Deagle.stress == nil and Deagle.VmTick == vtWas, running)
	Deagle.Fire = fireWas
	M.view.pitch = pitch
end

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("%s: %d/%d checks pass", id, n - fails, n))
os.exit(fails == 0 and 0 or 1)
