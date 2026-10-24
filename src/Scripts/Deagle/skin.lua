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
-- KCD2's Silhouette POM only works on static meshes (on the skinned body every ray missed: the gun went invisible).
-- Every vertex of the Grizzly's body rides one bone, so each bone's piece is a CGF in that bone's frame, attached with
-- CreateBoneAttachment(.., false) + SetAttachmentCGF: it moves exactly like the skin. That skin's .mtl draws the
-- skinned body NoDraw; any other skin hides the pieces.
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
	local n = 0
	for _, part in ipairs(PARTS) do
		local ok, r = pcall(vm.ChangeAttachmentMaterial, vm, part, mtl)
		if ok and r ~= false then n = n + 1 end
	end
	local rigid = self:SkinRigid(vm, want)
	log(string.format("skin %s on %s: %s (%d parts%s)", want, tostring(self.gun.id), mtl, n, rigid > 0 and (", " .. rigid .. " rigid pieces") or ""))
end

-- the rigid pieces of skin `want` shown (created once per viewmodel entity), every other skin's hidden; returns how
-- many are shown
function Deagle:SkinRigid(vm, want)
	local defs = self.gun and self.SKIN_RIGID[self.gun.id]
	if not defs then return 0 end
	local ent = tostring(self.vmId) .. "|" .. tostring(vm)
	if self.skinRigidEnt ~= ent then self.skinRigidEnt = ent; self.skinRigidMade = {} end
	local made, shown = self.skinRigidMade, 0
	for name, d in pairs(defs) do
		for _, bone in ipairs(d.bones) do
			local att = "vm_rigid_" .. name .. "_" .. bone
			if name == want then
				if not made[att] then
					pcall(vm.CreateBoneAttachment, vm, 0, bone, att, false)
					local ok = pcall(vm.SetAttachmentCGF, vm, 0, att, d.cgf .. string.lower(bone) .. ".cgf")   -- also shows it
					made[att] = ok
					if not ok then log("skin " .. name .. ": rigid piece " .. bone .. " failed to load") end
				else
					pcall(vm.HideAttachment, vm, 0, att, false, false)
				end
				if made[att] then shown = shown + 1 end
			elseif made[att] then
				pcall(vm.HideAttachment, vm, 0, att, true, true)
			end
		end
	end
	return shown
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
