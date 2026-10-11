-- Logs world position + full world frame of Henry's arm/hand/finger joints:
-- "[Deagle] CAP <tag> <bone> px py pz  ax ay az  xx xy xz  cx cy cz"
-- GetBoneDir = joint X axis. GetBoneLocal(bone, t) returns (a.t, X.t, c.t) for an orthonormal frame [a X c],
-- so probing t = unit axes yields a and c as well (mapping to joint Y/Z resolved offline by tools/hand_frames.py).
local p = g_localActor
local bones = {}
for _, side in ipairs({ "Right", "Left" }) do
	for _, b in ipairs({ "Arm", "ForeArm", "Hand", "HandSlot", "InHandPinky", "InHandRing" }) do bones[#bones + 1] = side .. b end
	for _, f in ipairs({ "Thumb", "Index", "Middle", "Ring", "Pinky" }) do
		for i = 1, 4 do bones[#bones + 1] = side .. "Hand" .. f .. i end
	end
end
bones[#bones + 1] = "Head"
local tag = Deagle and Deagle.captureTag or "capture"
local n = 0
for _, b in ipairs(bones) do
	local okp, pos = pcall(p.GetBonePos, p, b)
	local ok1, e1 = pcall(p.GetBoneLocal, p, b, { x = 1, y = 0, z = 0 })
	local ok2, e2 = pcall(p.GetBoneLocal, p, b, { x = 0, y = 1, z = 0 })
	local ok3, e3 = pcall(p.GetBoneLocal, p, b, { x = 0, y = 0, z = 1 })
	if okp and pos and ok1 and e1 and ok2 and e2 and ok3 and e3 then
		System.LogAlways(string.format("[Deagle] CAP %s %s %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f  %.6f %.6f %.6f", tag, b,
			pos.x, pos.y, pos.z, e1.x, e2.x, e3.x, e1.y, e2.y, e3.y, e1.z, e2.z, e3.z))
		n = n + 1
	end
end
local a = p:GetWorldAngles()
System.LogAlways(string.format("[Deagle] CAPDONE %s %d yaw %.5f", tag, n, a.z))
