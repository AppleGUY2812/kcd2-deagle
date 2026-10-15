-- Sniper Elite V2's organs and bones, as KCD2 models (docs/se2_killcam.md; tools/se2/organ.py reads them out of that game's
-- files, tools/se2/build_organs.sh makes the CGFs). deagle_organ <name> puts one (or "all") 0.8 m in front of the player,
-- deagle_organ clear removes them. The kill cam shows them at the NPC's wound (Deagle:ShowOrgans, called from killcam.lua).
-- Loaded at the end of ballistics.lua.
local U = Deagle.U
local log = U.log

local DIR = "objects/se2/organs/"
-- name -> model file (without the se2_ prefix and .cgf), in the order the kill cam shows them along the bullet's path
Deagle.organList = Deagle.organList or {
	{ "windpipe", "windpipe_upper_section" }, { "windpipe_lower", "windpipe_lower_section" },
	{ "lung_left", "lung_left_collapse" }, { "lung_right", "lung_right_coll" }, { "heart", "heart_solid" },
	{ "liver_upper", "liver_upper" }, { "liver_lower", "liver_lower" },
	{ "kidney_left", "kidney_left" }, { "kidney_right", "kidney_right" },
	{ "eyeball", "bcv_eyeball" }, { "testicle", "testicle_l" },
}

-- one model, placed at pos facing yaw (radians); returns the entity id or nil
function Deagle:SpawnOrgan(model, pos, yaw)
	local ok, e = pcall(System.SpawnEntity, { class = "BasicEntity", name = "deagle_organ_" .. model, position = pos,
		flags = ENTITY_FLAG_NO_SAVE, properties = { object_Model = DIR .. "se2_" .. model .. ".cgf" } })
	if not (ok and e) then log("$4organ " .. model .. " didn't spawn: " .. tostring(e)) return nil end
	pcall(e.SetFlags, e, ENTITY_FLAG_NO_SAVE, 0)
	if yaw then pcall(e.SetAngles, e, { x = 0, y = 0, z = yaw }) end
	return e.id
end

function Deagle:RemoveOrgans()
	for _, id in ipairs(self.organIds or {}) do if System.GetEntity(id) then pcall(System.RemoveEntity, id) end end
	self.organIds = nil
end

-- deagle_organ <name | all | clear>
function Deagle:OrganCmd(arg)
	arg = tostring(arg or ""):lower():gsub("^%s+", ""):gsub("%s+$", "")
	self:RemoveOrgans()
	if arg == "clear" then self:Notify("organs removed") return end
	local pl = self:Player()
	if not pl then return end
	local p, d = pl:GetPos(), pl:GetWorldDir()
	local yaw = math.atan2(-d.x, d.y)
	local want = {}
	for _, o in ipairs(self.organList) do
		if arg == "all" or arg == o[1] or arg == o[2] then want[#want + 1] = o end
	end
	if #want == 0 then
		local names = {}
		for _, o in ipairs(self.organList) do names[#names + 1] = o[1] end
		self:Notify("deagle_organ <" .. table.concat(names, " | ") .. " | all | clear>")
		return
	end
	local ids, gap = {}, 0.3
	for i, o in ipairs(want) do
		local side = (i - (#want + 1) / 2) * gap
		local pos = { x = p.x + 0.8 * d.x + side * d.y, y = p.y + 0.8 * d.y - side * d.x, z = p.z + 1.4 }
		local id = self:SpawnOrgan(o[2], pos, yaw)
		if id then ids[#ids + 1] = id end
	end
	self.organIds = ids
	self:Notify(string.format("%d organ(s) 0.8 m ahead (deagle_organ clear)", #ids))
end

System.AddCCommand("deagle_organ", "Deagle:OrganCmd([[%line]])",
	"deagle_organ <heart | lung_left | lung_right | liver_upper | kidney_left | eyeball | ... | all | clear>: Sniper Elite V2's organs, 0.8 m ahead")
