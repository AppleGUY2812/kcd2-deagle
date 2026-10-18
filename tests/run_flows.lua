-- usage: luajit tests/run_flows.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
local function step(label, fn)
	local before = #M.errors
	local ok, err = pcall(fn)
	if not ok then table.insert(M.errors, label .. ": " .. tostring(err)) end
	print(string.format("%-28s %s", label, (#M.errors == before) and "ok" or ("ERRORS: " .. table.concat({ unpack(M.errors, before + 1) }, " | "))))
end
local function lastAnims(n)
	local out = {}
	for i = math.max(1, #M.anims - n + 1), #M.anims do out[#out + 1] = M.anims[i].name:gsub("deagle_vm_", "") .. "@" .. M.anims[i].layer end
	return table.concat(out, " ")
end
step("load script", function() dofile("src/Scripts/Deagle/deagle_main.lua") end)
step("stale save entities", function()   -- an older build's viewmodel restored from a save (no character: the pyramid)
	System.SpawnEntity({ class = "BasicEntity", name = "deagle_viewmodel", properties = { object_Model = "objects/default/primitive_pyramid.cgf" } })
	System.SpawnEntity({ class = "BasicEntity", name = "deagle_vm_probe", properties = {} })
end)
local function ours() local n, ns = 0, 0 for k, e in pairs(M.entities()) do if type(k) ~= "string" and e.name and e.name:find("^deagle_") then n = n + 1; if bit.band(e.flags or 0, 32768) ~= 0 then ns = ns + 1 end end end return n, ns end
step("gameplay started", function()
	Deagle:OnGameplayStarted()
	Deagle.cfg.flashChance = 1   -- the flash checks count one per shot; Ready or Not's 65 % roll is checked in run_gun
	-- the pyramid must be gone as soon as the save loads, not only once the gun is drawn
	if ours() ~= 0 then error(ours() .. " stale entities left after load") end
end)
step("native handshake", function() M.console("deagle_native_ok") end)
step("draw (toggle)", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.2) end)
-- KCD2's combat map is off while the gun is out (left click punched); R's torch toggle is taken by our OnAction
-- (the game delivers it there; draw_holster_torch is an include map, switching it off did nothing)
if M.maps.combat ~= false then table.insert(M.errors, "combat map still on while drawn") end
if Deagle:OnAction("toggle_torch", "press") ~= true then table.insert(M.errors, "R took the torch out with the gun drawn") end
print("   anims:", lastAnims(3), " native:", table.concat(M.native, " ; "):sub(1, 160))
print("   deagle_* entities in world:", ours(), "(no-save:", select(2, ours()) .. ")")
step("idle frames + HUD", function() M.advance(0.5) end)
print("   HUD draw calls:", M.drawn_text, " first person:", tostring(Deagle.vmInFp), " DOF:", tostring(M.cvars and M.cvars.r_DepthOfField))
step("hip fire x7", function() for i = 1, 7 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end end)
print("   ammo:", Deagle.state.ammo, " anims:", lastAnims(4), " fx:", M.fx, M.lastFx)
local fx = M.fxNames or {}
-- the flash: the plugin spawns it on the muzzle every shot (flashfx), follows it while it burns (flashpos) and lets go
-- (flashoff); no particle is loaded onto an entity, nothing but smoke in the world
M.advance(0.1)   -- the last flash burns out
local function count(pat) local n = 0 for _, l in ipairs(M.native) do if l:find(pat) then n = n + 1 end end return n end
local num = "[%-%d.]+"
local fxCmds = count("^%[DN%] flashfx deagle%.muzzle[_a-z]* " .. string.rep(num, 7, " ") .. "$")
local posCmds, offCmds = count("^%[DN%] flashpos "), count("^%[DN%] flashoff")
print("   flash (" .. Deagle.cfg.flashMode .. "): plugin flashfx/flashpos/flashoff", fxCmds, posCmds, offCmds, " entity loads", M.slotFx,
	" in the world", fx["deagle.muzzle"], " smoke", fx[Deagle.cfg.muzzleSmokeFx])
if fxCmds ~= 7 or posCmds < 7 or offCmds < 1 or (M.slotFx or 0) ~= 0 or (fx["deagle.muzzle"] or 0) + (fx["deagle.muzzle_large"] or 0) ~= 0
	or (fx[Deagle.cfg.muzzleSmokeFx] or 0) ~= 7 then table.insert(M.errors, "flash not spawned on the muzzle by the plugin every shot") end
step("flash left in the world", function()   -- flashMode world: spawned where it was fired
	Deagle.cfg.flashMode = "world"; Deagle.state.ammo = 7; M.advance(0.5)
	local function flashes() local f = M.fxNames or {} return (f["deagle.muzzle"] or 0) + (f["deagle.muzzle_large"] or 0) end
	local w0 = flashes()
	Deagle:OnAction("deagle_fire", "press"); M.advance(0.1)
	print("   world flashes:", flashes() - w0)
	if flashes() - w0 ~= 1 then error("world flash not spawned") end
	Deagle.cfg.flashMode = "gun"; Deagle.state.ammo = 0; Deagle:VmRounds(Deagle:MagRounds()); M.advance(1.0)
end)
step("live effect reload (deagle_fxreload)", function()   -- re-reads the dev copy of the impacts library, never right after a shot
	M.advance(3.5)
	local n0 = #M.native
	M.console("deagle_fxreload")
	local l = M.native[#M.native]
	if #M.native ~= n0 + 1 or not l:find("^%[DN%] fxreload E:/Tools/deagle%-dev/Libs/Particles/deagle_impacts%.xml deagle_impacts$") then error("no fxreload: " .. tostring(l)) end
	Deagle.state.ammo = 3; Deagle:OnAction("deagle_fire", "press"); M.advance(0.5)
	n0 = #M.native
	M.console("deagle_fxreload")
	for i = n0 + 1, #M.native do if M.native[i]:find("^%[DN%] fxreload") then error("fxreload right after a shot (live emitters)") end end
	M.advance(3.5)
end)
step("slow motion (F9)", function()
	Deagle:OnAction("deagle_slowmo", "press"); local on = M.cvars and M.cvars.t_Scale
	Deagle:OnAction("deagle_slowmo", "press"); local off = M.cvars and M.cvars.t_Scale
	print("   t_Scale on/off:", on, off)
	if on ~= 0.1 or off ~= 1 then error("slow motion toggle") end
end)
step("barrel smoke after a burst (Ready or Not's heat)", function()
	-- 3 shots at the Deagle's rate stay under the threshold, 4 smoke 0.5 s after the last; a shot while it smokes stops it
	Deagle.nativeCaps = Deagle.nativeCaps or {}; Deagle.nativeCaps.smoke = true
	Deagle.barrelHeat = nil
	M.advance(12)   -- any earlier heat gone
	local function n(pat) local k = 0 for _, l in ipairs(M.native) do if l:find(pat) then k = k + 1 end end return k end
	local function burst(k) for i = 1, k do Deagle.state.ammo = 7; Deagle:Fire(true); M.advance(0.3) end end
	local s0 = n("^%[DN%] smokefx ")
	burst(3); M.advance(1.0)
	local after3 = n("^%[DN%] smokefx ") - s0
	M.advance(10); Deagle.barrelHeat = nil
	burst(4); M.advance(0.1)
	local early = n("^%[DN%] smokefx ") - s0
	M.advance(0.6)
	local after4 = n("^%[DN%] smokefx ") - s0
	local stops0 = n("^%[DN%] smokestop")
	Deagle.state.ammo = 7; Deagle:Fire(true); M.advance(0.05)
	local stops = n("^%[DN%] smokestop") - stops0
	print(string.format("   smokefx after 3 shots %d, after 4 shots: %d at +0.1 s, %d at +0.7 s; smokestop on the next shot %d (heat %.2f)",
		after3, early, after4, stops, Deagle.barrelHeat.v))
	if after3 ~= 0 or early ~= 0 or after4 ~= 1 or stops ~= 1 then error("barrel smoke isn't Ready or Not's heat rule") end
	local line = M.native[#M.native]
	for i = #M.native, 1, -1 do if M.native[i]:find("^%[DN%] smokefx ") then line = M.native[i] break end end
	if not line:find("^%[DN%] smokefx deagle%.barrel_smoke_ron ") then error("barrel smoke effect: " .. line) end
end)
-- the casing thrown last, from the current gun's pool (one pool per casing model)
local function lastCase()
	local p = Deagle.casePools[Deagle.gun.caseModel or Deagle.cfg.modelDir .. "deagle_case.cgf"]
	return System.GetEntity(p[p.next])
end
step("casings come from a pool", function()   -- 100 shots, at most 80 casings ever exist
	Deagle.state.ammo = 7
	for i = 1, 100 do if Deagle.state.ammo == 0 then Deagle.state.ammo = 7 end Deagle:OnAction("deagle_fire", "press"); M.advance(0.25) end
	local n = 0
	for k, e in pairs(M.entities()) do if type(k) ~= "string" and e.name and e.name:find("^deagle_case_") then n = n + 1 end end
	print("   casings in the world after 100 more shots:", n)
	if n ~= 80 then error(n .. " casings after 100+ throws: the pool of 80 isn't filled and reused") end
	-- Ready or Not's throw: out of the gun's right side 40 deg up, 2-6 m/s standing (base 2 x 1-3), the same spin on
	-- all three world axes cut to 62.83 rad/s; damping 1
	local ve = M.velEx
	if not ve or not ve.name:find("^deagle_case_") then error("casing not thrown with SetVelocityEx") end
	local function dot(a, b) return a.x * b.x + a.y * b.y + a.z * b.z end
	-- the mock's muzzle bone points the viewmodel's +Y (world +Y here): right = +X, up = +Z
	local sp = math.sqrt(dot(ve.v, ve.v))
	local dirOk = math.abs(ve.v.x / sp - math.cos(math.rad(40))) < 1e-3 and math.abs(ve.v.y / sp) < 1e-3 and math.abs(ve.v.z / sp - math.sin(math.rad(40))) < 1e-3
	local spin = math.sqrt(dot(ve.w, ve.w))
	print(string.format("   last casing: %.2f m/s along (%.3f %.3f %.3f), spin (%.1f %.1f %.1f) = %.2f rad/s", sp, ve.v.x / sp, ve.v.y / sp, ve.v.z / sp, ve.w.x, ve.w.y, ve.w.z, spin))
	if sp < 2 or sp > 6 or not dirOk then error("casing throw isn't Ready or Not's (2-6 m/s, 40 deg up to the gun's right)") end
	if spin > 62.83 + 1e-6 or ve.w.x ~= ve.w.y or ve.w.y ~= ve.w.z then error("casing spin isn't Ready or Not's (r, r, r) cut to 62.83 rad/s") end
	local thrown = lastCase()
	if not (thrown.sim and thrown.sim.damping == 1 and thrown.sim.freefall_damping == 1) then error("casing damping isn't 1") end
	local capped = false   -- the physics' 20 rad/s spin cap set to Unreal's 62.83 by the plugin before the throw
	for _, l in ipairs(M.native) do if l:find("^%[DN%] casespin %x+ 62%.83$") then capped = true end end
	if not capped then error("casing spin cap not set (casespin 62.83)") end
	-- its physics stops at 4 s, it is put away at 6 s, and thrown again it shows and moves again
	M.advance(4.1)
	if not thrown.physOff or thrown.hidden then error("casing: physics should stop at 4 s, still shown") end
	M.advance(2.0)
	if not thrown.hidden then error("casing not put away at 6 s") end
	-- in flight casings pass through each other (one class bit for casings, ignored); landed, both bits go
	local GHOST = 1073741824
	Deagle.state.ammo = 7; Deagle:Fire(true)   -- a fresh one, before any frame reads it
	local case = lastCase()
	local flying = Deagle.casesFlying[case.id]
	if not flying or bit.band(case.ccType, GHOST) == 0 or bit.band(case.ccIgnore, GHOST) == 0 then error("thrown casing doesn't ignore other casings") end
	local vels = { { x = 1, y = 0, z = -3 }, { x = 0.5, y = 0, z = 0.4 } }   -- falling, then up off the ground: a bounce
	local k = 0
	case.GetVelocity = function() k = math.min(k + 1, #vels) return vels[k] end
	Deagle:CaseTick(); Deagle:CaseTick()
	print(string.format("   casing class %d, ignore %d after its first bounce (%d)", case.ccType, case.ccIgnore, flying.bounces))
	if flying.bounces ~= 1 or case.ccType ~= 0 or case.ccIgnore ~= 0 then error("landed casing still ignored by / ignoring casings") end
	-- the pair rule (CryEngine skips a pair when either one's type is in the other's ignore): flying + flying skip,
	-- flying + landed collide
	local function skips(a, b) return bit.band(a.ccType, b.ccIgnore) ~= 0 or bit.band(b.ccType, a.ccIgnore) ~= 0 end
	local fly = { ccType = GHOST, ccIgnore = GHOST }
	if not skips(fly, fly) or skips(fly, case) then error("casing pairs: flying ones must pass, a falling one must hit a landed one") end
	case.GetVelocity = nil
	Deagle.cfg.eject.ghost = false
	Deagle.state.ammo = 7; Deagle:Fire(true)
	local case2 = lastCase()
	if case2.ccIgnore ~= 0 then error("eject.ghost 0: a casing still ignores casings") end
	Deagle.cfg.eject.ghost = true
	Deagle.state.ammo = 0; Deagle:VmRounds(Deagle:MagRounds()); M.advance(1.0)
end)
step("a gun with its own casing throws it (gun.caseModel), from its own pool", function()
	local own = Deagle.gun.caseModel
	Deagle.gun.caseModel = "objects/manmade/weapons/guns/ron_grizzly/50ae_casing.cgf"
	Deagle.state.ammo = 7
	Deagle:OnAction("deagle_fire", "press"); M.advance(0.25)
	local c = lastCase()
	local pools = 0
	for _ in pairs(Deagle.casePools) do pools = pools + 1 end
	Deagle.gun.caseModel = own
	print("   casing:", c and c.name, "pools:", pools)
	if not c or not c.name:find("^deagle_case_50ae_casing_%d+$") or pools ~= 2 then error("the gun's own casing model wasn't thrown from its own pool") end
end)
step("empty idle", function() M.advance(1.0) end)
print("   rest:", lastAnims(1))
print("   mag rounds shown:", tostring(Deagle.vmRoundsShown))
step("empty reload", function()
	Deagle:OnAction("deagle_reload", "press"); M.advance(0.5)
	print("   0.5 s (old mag dropping) rounds:", tostring(Deagle.vmRoundsShown)); M.advance(0.4)
	print("   0.9 s (new mag in hand) rounds:", tostring(Deagle.vmRoundsShown)); M.advance(2.3) end)
print("   ammo:", Deagle.state.ammo, " anims:", lastAnims(3))
step("inspect when empty", function() Deagle.state.ammo = 0; Deagle:OnAction("deagle_inspect", "press"); M.advance(0.2) end)
print("   anims:", lastAnims(1))
M.advance(6.6); Deagle.state.ammo = 7
step("right click, ADS off (default)", function() Deagle:OnAction("deagle_aim", "press"); M.advance(0.3) end)
print("   aiming:", tostring(Deagle.state.aiming), " fov:", tostring(M.fov), " anims:", lastAnims(1))
step("right click release", function() Deagle:OnAction("deagle_aim", "release"); M.advance(0.2) end)
Deagle.cfg.vm.ads = true   -- the rest exercises ADS (deagle_ads 1)
step("aim in", function() Deagle:OnAction("deagle_aim", "press"); M.advance(0.3) end)
print("   aiming:", tostring(Deagle.state.aiming), " anims:", lastAnims(2))
step("aimed fire x2", function() for i = 1, 2 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end end)
print("   anims:", lastAnims(3))
local function layers() local vm = System.GetEntity(Deagle.vmId) return tostring(vm.layers[0]):gsub("deagle_vm_", "") .. " | layer1=" .. tostring(vm.layers[1]) end
M.advance(2.0)
print("   after recoil: base=" .. layers())
step("aimed fire to empty", function() for i = 1, 5 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.35) end M.advance(1.0) end)
print("   ammo:", Deagle.state.ammo, " base=" .. layers())
step("reload while aiming", function() Deagle:OnAction("deagle_reload", "press"); M.advance(3.2) end)
print("   ammo:", Deagle.state.ammo, " aiming:", tostring(Deagle.state.aiming), " base=" .. layers())
step("aim out", function() Deagle:OnAction("deagle_aim", "release"); M.advance(0.3) end)
print("   base=" .. layers())
-- ADS feel: aim cuts a hip shot at once; aim held through reload / draw hands over before the clip ends (no idle between)
local function since(t0)
	local out = {}
	for _, a in ipairs(M.anims) do if a.t >= t0 and a.layer == 0 then out[#out + 1] = string.format("%s@+%.2fs/b%.2f", a.name:gsub("deagle_vm_", ""), a.t - t0, a.blend or -1) end end
	return table.concat(out, " ")
end
local t0 = M.t
step("aim mid hip shot", function() Deagle:OnAction("deagle_fire", "press"); M.advance(0.3); Deagle:OnAction("deagle_aim", "press"); M.advance(0.5) end)
print("   " .. since(t0), " aiming:", tostring(Deagle.state.aiming), " fov:", string.format("%.3f", M.fov or -1))
t0 = M.t
step("reload with aim held", function() Deagle:OnAction("deagle_reload", "press"); M.advance(0.5)
	print("   during reload: aiming", tostring(Deagle.state.aiming), " fov", string.format("%.3f", M.fov or -1)); M.advance(3.0) end)
print("   " .. since(t0), " aiming:", tostring(Deagle.state.aiming), " fov:", string.format("%.3f", M.fov or -1), " ammo:", Deagle.state.ammo)
step("aim out", function() Deagle:OnAction("deagle_aim", "release"); M.advance(0.4) end)
print("   fov back:", string.format("%.4f", M.fov or -1))
step("inspect", function() Deagle:OnAction("deagle_inspect", "press"); M.advance(7.0) end)
step("inspect again", function() Deagle:OnAction("deagle_inspect", "press"); M.advance(1.0) end)
print("   anims:", lastAnims(3))
step("dialogue camera", function()
	local real = System.GetViewCameraPos
	System.GetViewCameraPos = function() return { x = 103, y = 203, z = 32 } end
	M.advance(0.5)
	print("   in first person:", tostring(Deagle.vmInFp))
	System.GetViewCameraPos = real
	M.advance(0.5)
	print("   back in first person:", tostring(Deagle.vmInFp))
end)
step("holster", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.5) end)
if M.maps.combat ~= true then table.insert(M.errors, "combat map not back on after holster") end
if Deagle:OnAction("toggle_torch", "press") == true then table.insert(M.errors, "torch blocked with the gun holstered") end
print("   after holster: combat map", tostring(M.maps.combat), " torch key passes:", tostring(Deagle:OnAction("toggle_torch", "press") ~= true))
print("   anims:", lastAnims(2), " shown:", tostring(Deagle.vmShown), " body hidden:", tostring(Deagle.bodyHidden), " DOF:", tostring(M.cvars.r_DepthOfField))
t0 = M.t
step("draw with aim held", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(0.2); Deagle:OnAction("deagle_aim", "press"); M.advance(2.0) end)
print("   " .. since(t0), " aiming:", tostring(Deagle.state.aiming))
step("aim out + holster", function() Deagle:OnAction("deagle_aim", "release"); M.advance(0.3); Deagle:OnAction("deagle_toggle", "press"); M.advance(1.5) end)
step("console commands", function() for _, c in ipairs({ "deagle_arms", "deagle_motion", "deagle_hud", "deagle_ads", "deagle_native_test" }) do M.console(c) end end)
print(string.format("\nnative commands sent: %d (sounds %d)", #M.native, (function() local n = 0 for _, c in ipairs(M.native) do if c:find("snd") then n = n + 1 end end return n end)()))
step("dev reload keeps every gun and the one in hand", function()   -- deagle_dev_reload re-runs the main script
	Deagle:UseGun("glock_17"); M.advance(1.0)
	dofile("src/Scripts/Deagle/deagle_main.lua")
	local ids = {}
	for k in pairs(Deagle.guns) do ids[#ids + 1] = k end
	table.sort(ids)
	print("   guns after reload:", table.concat(ids, ","), " in hand:", Deagle.gun.id)
	if not Deagle.guns.glock_17 then error("the Glock is gone after a dev reload") end
	if Deagle.gun.id ~= "glock_17" then error("the gun in hand changed to " .. tostring(Deagle.gun.id)) end
end)
print("TOTAL ERRORS:", #M.errors)
for i, e in ipairs(M.errors) do if i <= 15 then print("  " .. e) end end
print("game firearm shot triggers (should be 0 with the plugin linked):", M.gameShot or 0)
