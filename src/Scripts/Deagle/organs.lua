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

-- Where the organs sit on a standing person (m): up from the feet, forward from the chest, right of the body's centre line.
-- Real human proportions (the models are at real size). The kill cam hides the NPC's body and shows these instead.
local LAYOUT = {
	windpipe = { 1.52, 0.00, 0.00 }, windpipe_lower = { 1.40, 0.00, 0.00 },
	lung_left = { 1.33, 0.00, -0.085 }, lung_right = { 1.33, 0.00, 0.085 },
	heart = { 1.30, 0.03, -0.04 },
	liver_upper = { 1.19, 0.02, 0.08 }, liver_lower = { 1.14, 0.02, 0.08 },
	kidney_left = { 1.08, -0.06, -0.07 }, kidney_right = { 1.08, -0.06, 0.07 },
}
local HEAD = { eyeball = { 1.66, 0.09, -0.032 } }   -- one each side (the second is mirrored)

-- the organs of the zone a round hit, placed in e's body; returns how many were spawned
function Deagle:ShowOrgans(e, zone)
	self:RemoveOrgans()
	local ok, p = pcall(e.GetWorldPos, e)
	if not (ok and p) then return 0 end
	local okd, d = pcall(e.GetDirectionVector, e)
	local fx, fy = 0, 1
	if okd and d and (d.x ~= 0 or d.y ~= 0) then
		local l = math.sqrt(d.x * d.x + d.y * d.y); fx, fy = d.x / l, d.y / l
	end
	local yaw = math.atan2(-fx, fy)
	local ids = {}
	local function put(model, lay, mirror)
		local up, fwd, right = lay[1], lay[2], mirror and -lay[3] or lay[3]
		local pos = { x = p.x + fx * fwd + fy * right, y = p.y + fy * fwd - fx * right, z = p.z + up }
		local id = self:SpawnOrgan(model, pos, yaw)
		if id then ids[#ids + 1] = id end
	end
	if zone == "head" then
		put("bcv_eyeball", HEAD.eyeball, false); put("bcv_eyeball", HEAD.eyeball, true)
	else
		for _, o in ipairs(self.organList) do
			local lay = LAYOUT[o[1]]
			if lay then put(o[2], lay, false) end
		end
	end
	self.organIds = ids
	return #ids
end
