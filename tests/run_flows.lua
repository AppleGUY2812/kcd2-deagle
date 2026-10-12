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
step("gameplay started", function() Deagle:OnGameplayStarted() end)
step("native handshake", function() M.console("deagle_native_ok") end)
step("draw (toggle)", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.2) end)
print("   anims:", lastAnims(3), " native:", table.concat(M.native, " ; "):sub(1, 160))
local function ours() local n, ns = 0, 0 for k, e in pairs(M.entities()) do if type(k) ~= "string" and e.name and e.name:find("^deagle_") then n = n + 1; if bit.band(e.flags or 0, 32768) ~= 0 then ns = ns + 1 end end end return n, ns end
print("   deagle_* entities in world:", ours(), "(no-save:", select(2, ours()) .. ")")
step("idle frames + HUD", function() M.advance(0.5) end)
print("   HUD draw calls:", M.drawn_text, " first person:", tostring(Deagle.vmInFp), " DOF:", tostring(M.cvars and M.cvars.r_DepthOfField))
step("hip fire x7", function() for i = 1, 7 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end end)
print("   ammo:", Deagle.state.ammo, " anims:", lastAnims(4), " fx:", M.fx, M.lastFx)
step("empty idle", function() M.advance(1.0) end)
print("   rest:", lastAnims(1))
step("empty reload", function() Deagle:OnAction("deagle_reload", "press"); M.advance(3.2) end)
print("   ammo:", Deagle.state.ammo, " anims:", lastAnims(3))
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
print("   anims:", lastAnims(2), " shown:", tostring(Deagle.vmShown), " body hidden:", tostring(Deagle.bodyHidden), " DOF:", tostring(M.cvars.r_DepthOfField))
step("console commands", function() for _, c in ipairs({ "deagle_arms", "deagle_motion", "deagle_hud", "deagle_predict", "deagle_native_test" }) do M.console(c) end end)
print(string.format("\nnative commands sent: %d (sounds %d)", #M.native, (function() local n = 0 for _, c in ipairs(M.native) do if c:find("snd") then n = n + 1 end end return n end)()))
print("TOTAL ERRORS:", #M.errors)
for i, e in ipairs(M.errors) do if i <= 15 then print("  " .. e) end end
print("game firearm shot triggers (should be 0 with the plugin linked):", M.gameShot or 0)
