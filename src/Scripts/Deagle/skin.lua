-- Gun skins (deagle-game-todo-notion, 2026-10-19): Ready or Not's other finishes on the same model, swapped in by
-- material. The Grizzly MK V ships two in RoN: the black one (MI_PIS_GRIZZLY_Body) and Silver (MI_PIS_GRIZZLY_Body_Silver,
-- its own SK_PIS_GRIZZLY_SILVER but the same UV layout: the textures line up on our mesh). tools/ron/ron_vm_mtl.py
-- writes <slug>_vm_silver.mtl beside <slug>_vm.mtl with the same sub-material order.
--   deagle_skin            lists the current gun's skins
--   deagle_skin silver     that skin, kept (deagle_set skin silver; "main" is the gun as built)
-- The skin goes on every attachment that wears the gun's material; a watch puts it back when the viewmodel is rebuilt
-- (gun switch, respawn, load), since Lua can't hook viewmodel.lua's VmEntity from here.
local U = Deagle.U
local log = U.log

Deagle.SKINS = { ron_grizzly = { main = "", silver = "_silver", mirror = "_mirror" } }   -- mirror: the Silver polished (tools/ron/mk_grizzly_mirror.py)
local PARTS = { "vm_mesh", "vm_mag1", "vm_mag2", "vm_round_1", "vm_round_2", "vm_round_3", "vm_round_4" }
-- A skin's body as RIGID pieces on the gun's bones (2026-10-23, the gun-mirror session; tools/ron/mk_grizzly_rigid.py):
-- KCD2's Silhouette POM only works on static meshes (on the skinned body every ray missed: the gun went invisible; the
-- same piece as a world object draws with it, deagle_spom test). Every vertex of the Grizzly's body rides one bone, so
-- each bone's piece is a CGF in that bone's frame, a CA_BONE attachment of the viewmodel's .cdf (tools/ron/rigid_cdf.py,
-- RelRotation/RelPosition identity): it moves exactly like the skin. The script only shows and hides them: attachments
-- made at run time never sat on their bone on the viewmodel (model origin or rest pose, 4 tries live) and drew stale
-- models (MODLOG 2026-10-14). With the pieces on, the skin's body draws NoDraw (<skin>_rigid.mtl).
Deagle.SKIN_RIGID_MTL = { pom = "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid_pom",
	spom = "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid" }
Deagle.skinRigidLook = Deagle.skinRigidLook or "pom"
-- the rigid pieces are an experiment until they sit right and SilPOM draws on them: off by default (the skinned body
-- shows); deagle_spom on swaps the body to its NoDraw variant (<skin>_rigid.mtl) and attaches the pieces
Deagle.skinRigidOn = Deagle.skinRigidOn or false
-- each piece's offset from its bone: none (the CGFs are in their bone's frame); deagle_spom rot <x> <y> <z> turns
-- them, degrees, live, in case they show turned in game (SetAttachmentAngles keeps the .cdf's zero position)
Deagle.skinRigidRot = Deagle.skinRigidRot or { 0, 0, 0 }
function Deagle:SkinRigidAngles(vm, att)
	local r, k = self.skinRigidRot, math.pi / 180
	pcall(vm.SetAttachmentAngles, vm, 0, att, { x = r[1] * k, y = r[2] * k, z = r[3] * k })
end
Deagle.SKIN_RIGID = { ron_grizzly = { mirror = { cgf = "objects/manmade/weapons/guns/ron_grizzly/rigid/ron_grizzly_rigid_",
	bones = { "J_Gun", "tag_slide", "tag_sight", "tag_barrel", "tag_grip_safety", "tag_hammer", "tag_slidelock_safety",
		"tag_slidelock", "tag_trigger", "tag_mag_release" } } } }
if Deagle.cfg.skin == nil then Deagle.cfg.skin = "main" end

-- the material path of a skin on the current gun (nil: the gun has no such skin)
function Deagle:SkinMtl(name)
	local skins = self.gun and self.SKINS[self.gun.id]
	local cdf = self.cfg.vm and self.cfg.vm.cdf
	if not skins or skins[name] == nil or type(cdf) ~= "string" then return nil end
	return (cdf:gsub("%.cdf$", "")) .. skins[name]
end

function Deagle:SkinApply()
	local vm = self.vmId and System.GetEntity(self.vmId)
	if not vm then return end
	local want = tostring(self.cfg.skin or "main")
	-- the entity's script table too: a rebuilt viewmodel can get the old one's freed id back (UseGun always respawns it)
	local key = tostring(self.vmId) .. "|" .. tostring(vm) .. "|" .. tostring(self.gun and self.gun.id) .. "|" .. want
	if self.skinApplied == key then return end
	self.skinApplied = key
	local mtl = self:SkinMtl(want) or self:SkinMtl("main")
	if not mtl then return end   -- a gun without skins keeps its own material
	local defs = self.gun and self.SKIN_RIGID[self.gun.id]
	if self.skinRigidOn and defs and defs[want] then mtl = mtl .. "_rigid" end   -- the skinned body NoDraw: the pieces show it
	local n = 0
	for _, part in ipairs(PARTS) do
		local ok, r = pcall(vm.ChangeAttachmentMaterial, vm, part, mtl)
		if ok and r ~= false then n = n + 1 end
	end
	local rigid = self:SkinRigid(vm, self.skinRigidOn and want or "")
	log(string.format("skin %s on %s: %s (%d parts%s)", want, tostring(self.gun.id), mtl, n, rigid > 0 and (", " .. rigid .. " rigid pieces") or ""))
end

-- the rigid pieces of skin `want` shown (the .cdf's attachments), every other skin's hidden; returns how many are shown
function Deagle:SkinRigid(vm, want)
	local defs = self.gun and self.SKIN_RIGID[self.gun.id]
	if not defs then return 0 end
	local shown = 0
	self.skinRigidMade = {}
	for name, d in pairs(defs) do
		for _, bone in ipairs(d.bones) do
			local att = "vm_rigid_" .. name .. "_" .. bone
			local okb, b = pcall(vm.GetAttachmentBone, vm, 0, att)
			if okb and b ~= nil then   -- nil: this viewmodel's .cdf has no such piece (an old pak)
				if name == want then
					pcall(vm.ChangeAttachmentMaterial, vm, att, self.SKIN_RIGID_MTL[self.skinRigidLook] or self.SKIN_RIGID_MTL.pom)
					pcall(vm.HideAttachment, vm, 0, att, false, false)
					self.skinRigidMade[att] = true; shown = shown + 1
				else
					pcall(vm.HideAttachment, vm, 0, att, true, true)
				end
			end
		end
	end
	return shown
end

-- deagle_spom pom | spom: the rigid pieces' material, live; deagle_spom test: two 5x copies of the frame piece 1.5 m
-- ahead as plain world objects, Silhouette POM on the left, plain POM on the right (does SilPOM draw our mesh at all,
-- away from the viewmodel?)
function Deagle:SpomCmd(arg)
	if arg == "test" then
		for _, n in ipairs({ "deagle_spomtest_spom", "deagle_spomtest_pom" }) do local e = System.GetEntityByName(n) if e then System.RemoveEntity(e.id) end end
		local p, d = System.GetViewCameraPos(), System.GetViewCameraDir()
		local rl = math.sqrt(d.x * d.x + d.y * d.y); local r = rl > 0 and { x = d.y / rl, y = -d.x / rl } or { x = 1, y = 0 }
		local cgf = (self.SKIN_RIGID.ron_grizzly.mirror.cgf) .. "j_gun.cgf"
		local cen = { x = 0, y = 0, z = 0 }   -- bone-space pieces sit around their own origin
		for _, c in ipairs({ { "deagle_spomtest_spom", -0.4, self.SKIN_RIGID_MTL.spom }, { "deagle_spomtest_pom", 0.4, self.SKIN_RIGID_MTL.pom } }) do
			local pos = { x = p.x + d.x * 1.5 + r.x * c[2] - cen.x * 5, y = p.y + d.y * 1.5 + r.y * c[2] - cen.y * 5, z = p.z + d.z * 1.5 - cen.z * 5 }
			local e = System.SpawnEntity({ class = "BasicEntity", name = c[1], position = pos, flags = ENTITY_FLAG_NO_SAVE, properties = { object_Model = cgf } })
			if e then pcall(e.LoadObject, e, 0, cgf); pcall(e.SetScale, e, 5); pcall(e.SetMaterial, e, c[3]) end
			log(string.format("spom test: %s %s at %.1f %.1f %.1f", c[1], e and "spawned" or "FAILED", pos.x, pos.y, pos.z))
		end
		self:Notify("left: Silhouette POM, right: plain POM (deagle_spom clear removes them)")
	elseif arg == "clear" then
		for _, n in ipairs({ "deagle_spomtest_spom", "deagle_spomtest_pom" }) do local e = System.GetEntityByName(n) if e then System.RemoveEntity(e.id) end end
	elseif arg:match("^rot") then   -- deagle_spom rot <x> <y> <z>: every piece's turn about its bone, degrees, live
		local x, y, z = arg:match("^rot%s+(%-?[%d%.]+)%s+(%-?[%d%.]+)%s+(%-?[%d%.]+)")
		if x then
			self.skinRigidRot = { tonumber(x), tonumber(y), tonumber(z) }
			local vm = self.vmId and System.GetEntity(self.vmId)
			for att, ok in pairs(self.skinRigidMade or {}) do if ok and vm then self:SkinRigidAngles(vm, att) end end
		end
		local r = self.skinRigidRot
		log(string.format("spom: pieces turned %g %g %g deg", r[1], r[2], r[3])); self:Notify(string.format("pieces turned %g %g %g", r[1], r[2], r[3]))
	elseif arg == "on" or arg == "off" then
		self.skinRigidOn = arg == "on"; self.skinApplied = nil; self:SkinApply()
		self:Notify("gun pieces " .. arg .. (arg == "on" and " (" .. self.skinRigidLook .. ")" or ""))
	elseif self.SKIN_RIGID_MTL[arg] then
		self.skinRigidLook = arg
		local vm = self.vmId and System.GetEntity(self.vmId)
		local n = 0
		for att, ok in pairs(self.skinRigidMade or {}) do
			if ok and vm and pcall(vm.ChangeAttachmentMaterial, vm, att, self.SKIN_RIGID_MTL[arg]) then n = n + 1 end
		end
		log("spom: the rigid pieces wear " .. arg .. " (" .. n .. " pieces)"); self:Notify("gun pieces: " .. arg)
	else
		self:Notify("deagle_spom on | off | pom | spom | test | clear")
	end
end

function Deagle:SkinCmd(arg)
	local skins = self.gun and self.SKINS[self.gun.id]
	local names = {}
	for k in pairs(skins or {}) do names[#names + 1] = k end
	table.sort(names)
	if arg == "" then
		local msg = skins and ("skins for " .. self.gun.id .. ": " .. table.concat(names, ", ") .. " (now " .. tostring(self.cfg.skin) .. ")")
			or ("no skins for " .. tostring(self.gun and self.gun.id))
		log(msg); self:Notify(msg) return
	end
	if not skins or skins[arg] == nil then
		local msg = "no skin " .. arg .. (skins and (" (" .. table.concat(names, ", ") .. ")") or (" for " .. tostring(self.gun and self.gun.id)))
		log(msg); self:Notify(msg) return
	end
	self:Set("skin " .. arg)   -- kept, set again at every load
	self:SkinApply()
	self:Notify("skin " .. arg)
end

-- the watch: the viewmodel is rebuilt on gun switches, respawns and loads (a load drops timers: the setting replay at
-- every load restarts it through SkinWatchKick)
function Deagle:SkinWatch()
	self.skinWatchAt = System.GetCurrTime()
	pcall(self.SkinApply, self)
	Script.SetTimer(300, function() Deagle:SkinWatch() end)
end
function Deagle:SkinWatchKick()
	if not self.skinWatchAt or math.abs(System.GetCurrTime() - self.skinWatchAt) > 1 then self:SkinWatch() end
end
Deagle:SkinWatchKick()

System.AddCCommand("deagle_skin", "Deagle:SkinWatchKick(); Deagle:SkinCmd(Deagle.Arg([[%1]]))", "deagle_skin [name]: the gun's finish (Grizzly: main, silver, mirror), kept; no name lists them")
System.AddCCommand("deagle_spom", "Deagle:SpomCmd(Deagle.Arg([[%line]]))", "deagle_spom on|off|pom|spom|rot x y z|test|clear: the mirror gun as rigid pieces (on/off), their turn about the bones (rot, degrees), the pieces' material (plain or Silhouette POM), or a side-by-side SilPOM test in the world")
