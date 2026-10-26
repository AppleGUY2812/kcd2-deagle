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
local POM, SPOM = rig .. "pom", "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid"
-- the SPOM gun pieces: world entities (CGF attachments never draw on the viewmodel), placed by the plugin every frame
local function pieces()
	local out, n = {}, 0
	for k, e in pairs(M.entities()) do
		if type(k) ~= "string" and type(e.name) == "string" and e.name:find("^deagle_vmpiece_") then out[e.name:sub(16)] = e; n = n + 1 end
	end
	return out, n
end
local function sent(pat) local c = 0 for i = 1, #M.native do if M.native[i]:find(pat) then c = c + 1 end end return c end
check("SPOM is off by default (the skinned mirror body), Silhouette POM when on", Deagle.skinRigidOn == false and Deagle.skinRigidLook == "spom", Deagle.skinRigidLook)
M.console("deagle_spom", "on"); M.console("deagle_spom", "pom"); step(0.1)
check("no plugin: the SPOM pieces wait, the skinned body stays (no pieces to show it)", select(2, pieces()) == 0 and M.attachMtl.vm_mesh == base .. "_mirror", M.attachMtl.vm_mesh)
local before = #M.native
M.console("deagle_native_ok"); step(0.4)
local p, np = pieces()
check("deagle_spom on: the body NoDraw variant", M.attachMtl.vm_mesh == base .. "_mirror_rigid", M.attachMtl.vm_mesh)
check("deagle_spom on: 10 world pieces, each its bone's CGF, no physics, plain POM",
	np == 10 and p.tag_slide and p.tag_slide.model == rig .. "tag_slide.cgf" and p.J_Gun.model == rig .. "j_gun.cgf"
	and p.tag_slide.physics == false and p.J_Gun.material == POM, np)
local lines = {} for i = before + 1, #M.native do lines[#lines + 1] = M.native[i] end
local joined = table.concat(lines, "\n")
check("the plugin is told each piece and its bone", sent("^%[DN%] rigid 0x%x+ tag_slide$") >= 1 and sent("^%[DN%] rigid 0x%x+ J_Gun$") >= 1
	and joined:find("rigid clear") ~= nil, joined)
check("the .cdf's attachment copies stay hidden", M.hidden.vm_rigid_mirror_J_Gun == true and M.hidden.vm_rigid_mirror_tag_slide == true, M.hidden.vm_rigid_mirror_J_Gun)
local vm = System.GetEntity(Deagle.vmId)
Deagle:VmSetHidden(vm, 1); step(0.1)
check("gun hidden: the pieces hide with it", p.tag_slide.hidden == true and p.J_Gun.hidden == true, p.tag_slide.hidden)
Deagle:VmSetHidden(vm, 0); step(0.1)
check("gun shown: the pieces show", p.tag_slide.hidden == false, p.tag_slide.hidden)
M.console("deagle_spom", "spom"); step(0.1)
check("deagle_spom spom: every piece switched to Silhouette POM, live", p.tag_slide.material == SPOM and p.tag_trigger.material == SPOM, p.tag_slide.material)
M.console("deagle_spom", "pom"); step(0.1)
M.console("deagle_skin", "main"); step(0.1)
M.console("deagle_skin", "main"); step(0.1)
check("deagle_skin main: back to the gun as built", M.attachMtl.vm_mesh == base, M.attachMtl.vm_mesh)
check("main: the pieces removed", select(2, pieces()) == 0, select(2, pieces()))
M.console("deagle_skin", "mirror"); step(0.1)
check("mirror again: 10 pieces again", select(2, pieces()) == 10, select(2, pieces()))
M.console("deagle_spom", "off"); step(0.1)
check("deagle_spom off: pieces gone, the skinned body back", select(2, pieces()) == 0 and M.attachMtl.vm_mesh == base .. "_mirror", M.attachMtl.vm_mesh)
M.console("deagle_skin", "main"); step(0.1)
M.attachMtl = {}
Deagle:UseGun("deagle"); Deagle:VmEntity(); step(0.6)
check("the Deagle (no skins): its material left alone", M.attachMtl.vm_mesh == nil or M.attachMtl.vm_mesh:find("deagle/vm/"), M.attachMtl.vm_mesh)
print(string.format("skin: %d/%d", n - fails, n))
os.exit(fails == 0 and 0 or 1)
