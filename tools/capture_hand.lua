-- Live capture of Henry's hand/finger joint frames (world position + joint X axis) for the current pose.
-- Written to the log as one line per bone; tools/parse_capture.py turns it into JSON.
local p = g_localActor
local bones = {}
for _, side in ipairs({ "Right", "Left" }) do
	for _, b in ipairs({ "ForeArm", "Hand", "HandSlot", "InHandPinky", "InHandRing" }) do bones[#bones + 1] = side .. b end
	for _, f in ipairs({ "Thumb", "Index", "Middle", "Ring", "Pinky" }) do
		for i = 1, 4 do bones[#bones + 1] = side .. "Hand" .. f .. i end
	end
end
local tag = Deagle and Deagle.captureTag or "capture"
local n = 0
for _, b in ipairs(bones) do
	local okp, pos = pcall(p.GetBonePos, p, b)
	local okd, dir = pcall(p.GetBoneDir, p, b)
	if okp and pos and okd and dir then
		System.LogAlways(string.format("[Deagle] CAP %s %s %.6f %.6f %.6f %.6f %.6f %.6f", tag, b, pos.x, pos.y, pos.z, dir.x, dir.y, dir.z))
		n = n + 1
	end
end
System.LogAlways("[Deagle] CAPDONE " .. tag .. " " .. n)
