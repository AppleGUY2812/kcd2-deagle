-- Gun skins (src/Scripts/Deagle/skin.lua): deagle_skin puts the skin's material on the gun's parts, keeps it, and puts
-- it back when the viewmodel is rebuilt; a gun without skins is left alone.
-- usage: luajit tests/run_skin.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
Deagle:OnGameplayStarted(); M.advance(0.5)
local function step(s) local t = 0 while t < s do M.advance(1 / 30); t = t + 1 / 30 end end
local base = "objects/manmade/weapons/guns/ron_grizzly/ron_grizzly_vm"
Deagle:UseGun("ron_grizzly"); Deagle:VmEntity(); step(0.5)
check("Grizzly: the cdf names the material deagle_skin builds on", Deagle:SkinMtl("main") == base, Deagle:SkinMtl("main"))
M.attachMtl = {}
M.console("deagle_skin", "silver"); step(0.1)
check("deagle_skin silver: the body, both mags and the rounds wear the silver set",
	M.attachMtl.vm_mesh == base .. "_silver" and M.attachMtl.vm_mag1 == base .. "_silver" and M.attachMtl.vm_round_4 == base .. "_silver", M.attachMtl.vm_mesh)
check("kept as a setting", Deagle.cfg.skin == "silver", Deagle.cfg.skin)
M.attachMtl = {}
Deagle:VmRespawn(); step(0.6)
check("a rebuilt viewmodel gets silver again", M.attachMtl.vm_mesh == base .. "_silver", M.attachMtl.vm_mesh)
M.attachMtl = {}
M.console("deagle_skin", "gold"); step(0.1)
check("an unknown skin: refused, nothing changed", Deagle.cfg.skin == "silver" and M.attachMtl.vm_mesh == nil, Deagle.cfg.skin)
M.console("deagle_skin", "main"); step(0.1)
check("deagle_skin main: back to the gun as built", M.attachMtl.vm_mesh == base, M.attachMtl.vm_mesh)
M.attachMtl = {}
Deagle:UseGun("deagle"); Deagle:VmEntity(); step(0.6)
check("the Deagle (no skins): its material left alone", M.attachMtl.vm_mesh == nil or M.attachMtl.vm_mesh:find("deagle/vm/"), M.attachMtl.vm_mesh)
print(string.format("skin: %d/%d", n - fails, n))
os.exit(fails == 0 and 0 or 1)
