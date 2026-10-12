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
	-- the pyramid must be gone as soon as the save loads, not only once the gun is drawn
	if ours() ~= 0 then error(ours() .. " stale entities left after load") end
end)
step("native handshake", function() M.console("deagle_native_ok") end)
step("draw (toggle)", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.2) end)
-- KCD2's maps on our keys are off while the gun is out (R took the torch out, left click punched)
if M.maps.draw_holster_torch ~= false or M.maps.combat ~= false then table.insert(M.errors, "game maps on our keys still on while drawn") end
print("   anims:", lastAnims(3), " native:", table.concat(M.native, " ; "):sub(1, 160))
print("   deagle_* entities in world:", ours(), "(no-save:", select(2, ours()) .. ")")
step("idle frames + HUD", function() M.advance(0.5) end)
print("   HUD draw calls:", M.drawn_text, " first person:", tostring(Deagle.vmInFp), " DOF:", tostring(M.cvars and M.cvars.r_DepthOfField))
step("hip fire x7", function() for i = 1, 7 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end end)
print("   ammo:", Deagle.state.ammo, " anims:", lastAnims(4), " fx:", M.fx, M.lastFx)
local fx = M.fxNames or {}
print("   flash (" .. Deagle.cfg.flashMode .. "): on the muzzle attachment", M.attachFx, " attachments made", M.boneAttach, " in the world", fx["deagle.muzzle"], " smoke", fx["deagle.muzzle_smoke"])
if (M.attachFx or 0) ~= 7 or (M.boneAttach or 0) ~= 1 or (fx["deagle.muzzle"] or 0) + (fx["deagle.muzzle_large"] or 0) ~= 0 or (fx["deagle.muzzle_smoke"] or 0) ~= 7 then table.insert(M.errors, "flash not on the muzzle attachment every shot") end
step("flash left in the world", function()   -- flashMode world: spawned where it was fired
	Deagle.cfg.flashMode = "world"; Deagle.state.ammo = 7; M.advance(0.5)
	local function flashes() local f = M.fxNames or {} return (f["deagle.muzzle"] or 0) + (f["deagle.muzzle_large"] or 0) end
	local w0 = flashes()
	Deagle:OnAction("deagle_fire", "press"); M.advance(0.1)
	print("   world flashes:", flashes() - w0)
	if flashes() - w0 ~= 1 then error("world flash not spawned") end
	Deagle.cfg.flashMode = "gun"; Deagle.state.ammo = 0; Deagle:VmRounds(Deagle:MagRounds()); M.advance(1.0)
end)
step("slow motion (F9)", function()
	Deagle:OnAction("deagle_slowmo", "press"); local on = M.cvars and M.cvars.t_Scale
	Deagle:OnAction("deagle_slowmo", "press"); local off = M.cvars and M.cvars.t_Scale
	print("   t_Scale on/off:", on, off)
	if on ~= 0.1 or off ~= 1 then error("slow motion toggle") end
end)
step("casings come from a pool", function()   -- 20 shots, at most 12 casings ever exist
	Deagle.state.ammo = 7
	for i = 1, 20 do if Deagle.state.ammo == 0 then Deagle.state.ammo = 7 end Deagle:OnAction("deagle_fire", "press"); M.advance(0.12) end
	local n = 0
	for k, e in pairs(M.entities()) do if type(k) ~= "string" and e.name and e.name:find("^deagle_case_") then n = n + 1 end end
	print("   casings in the world after 20 more shots:", n)
	if n > 12 then error(n .. " casings: the pool isn't reused") end
	Deagle.state.ammo = 0; Deagle:VmRounds(Deagle:MagRounds()); M.advance(1.0)
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
if M.maps.draw_holster_torch ~= true or M.maps.combat ~= true then table.insert(M.errors, "game maps on our keys not back on after holster") end
print("   game maps after holster: torch", tostring(M.maps.draw_holster_torch), " combat", tostring(M.maps.combat))
print("   anims:", lastAnims(2), " shown:", tostring(Deagle.vmShown), " body hidden:", tostring(Deagle.bodyHidden), " DOF:", tostring(M.cvars.r_DepthOfField))
t0 = M.t
step("draw with aim held", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(0.2); Deagle:OnAction("deagle_aim", "press"); M.advance(2.0) end)
print("   " .. since(t0), " aiming:", tostring(Deagle.state.aiming))
step("aim out + holster", function() Deagle:OnAction("deagle_aim", "release"); M.advance(0.3); Deagle:OnAction("deagle_toggle", "press"); M.advance(1.5) end)
step("console commands", function() for _, c in ipairs({ "deagle_arms", "deagle_motion", "deagle_hud", "deagle_ads", "deagle_native_test" }) do M.console(c) end end)
print(string.format("\nnative commands sent: %d (sounds %d)", #M.native, (function() local n = 0 for _, c in ipairs(M.native) do if c:find("snd") then n = n + 1 end end return n end)()))
print("TOTAL ERRORS:", #M.errors)
for i, e in ipairs(M.errors) do if i <= 15 then print("  " .. e) end end
print("game firearm shot triggers (should be 0 with the plugin linked):", M.gameShot or 0)
