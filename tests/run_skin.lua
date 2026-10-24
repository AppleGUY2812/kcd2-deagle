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
M.console("deagle_skin", "mirror"); step(0.1)
check("deagle_skin mirror: the polished set", M.attachMtl.vm_mesh == base .. "_mirror" and Deagle.cfg.skin == "mirror", M.attachMtl.vm_mesh)
local rig = "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid_"
local made0 = M.runtimeCgf or 0
check("the .cdf brings the 10 pieces, each on its own bone, at identity", M.cdfAttach.vm_rigid_mirror_J_Gun == "CA_BONE"
	and M.attachCgf.vm_rigid_mirror_tag_slide == rig .. "tag_slide.cgf" and M.attachCgf.vm_rigid_mirror_tag_mag_release == rig .. "tag_mag_release.cgf",
	M.attachCgf.vm_rigid_mirror_J_Gun)
check("mirror: the rigid pieces are off by default (hidden, the skinned body shows)", M.hidden.vm_rigid_mirror_J_Gun == true and M.attachMtl.vm_mesh == base .. "_mirror", M.hidden.vm_rigid_mirror_J_Gun)
M.console("deagle_spom", "on"); step(0.1)
check("deagle_spom on: the body NoDraw variant", M.attachMtl.vm_mesh == base .. "_mirror_rigid", M.attachMtl.vm_mesh)
check("deagle_spom on: the 10 pieces shown, none made at run time", M.hidden.vm_rigid_mirror_J_Gun == false and M.hidden.vm_rigid_mirror_tag_mag_release == false
	and (M.runtimeCgf or 0) == made0 and (M.boneAttach or 0) == 0, M.runtimeCgf)
check("mirror: the pieces wear plain POM by default", M.attachMtl.vm_rigid_mirror_J_Gun == "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid_pom", M.attachMtl.vm_rigid_mirror_J_Gun)
M.console("deagle_spom", "rot 0 0 90"); step(0.1)
check("deagle_spom rot 0 0 90: every piece turned live", math.abs(M.attachAngles.vm_rigid_mirror_tag_slide.z - math.pi / 2) < 1e-6, M.attachAngles.vm_rigid_mirror_tag_slide.z)
M.console("deagle_spom", "rot 0 0 0"); step(0.1)
check("deagle_spom rot 0 0 0: turned back live", M.attachAngles.vm_rigid_mirror_tag_slide.z == 0, M.attachAngles.vm_rigid_mirror_tag_slide.z)
M.console("deagle_spom", "spom"); step(0.1)
check("deagle_spom spom: every piece switched to Silhouette POM, live", M.attachMtl.vm_rigid_mirror_tag_slide == "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid", M.attachMtl.vm_rigid_mirror_tag_slide)
M.console("deagle_spom", "pom"); step(0.1)
local made = M.runtimeCgf
M.console("deagle_skin", "main"); step(0.1)
M.console("deagle_skin", "main"); step(0.1)
check("deagle_skin main: back to the gun as built", M.attachMtl.vm_mesh == base, M.attachMtl.vm_mesh)
check("main: the rigid pieces hidden", M.hidden.vm_rigid_mirror_J_Gun == true and M.hidden.vm_rigid_mirror_tag_trigger == true, M.hidden.vm_rigid_mirror_J_Gun)
M.console("deagle_skin", "mirror"); step(0.1)
check("mirror again: shown again, nothing loaded", M.hidden.vm_rigid_mirror_J_Gun == false and M.runtimeCgf == made, M.runtimeCgf)
M.console("deagle_skin", "main"); step(0.1)
M.attachMtl = {}
Deagle:UseGun("deagle"); Deagle:VmEntity(); step(0.6)
check("the Deagle (no skins): its material left alone", M.attachMtl.vm_mesh == nil or M.attachMtl.vm_mesh:find("deagle/vm/"), M.attachMtl.vm_mesh)
print(string.format("skin: %d/%d", n - fails, n))
os.exit(fails == 0 and 0 or 1)
