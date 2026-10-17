-- tools/live.sh tools/probe/mat.lua (architecture step 1, Claude 5; needs native/src/probe.cpp). Aim at the campfire
-- cauldron (or any static object), then run: one Lua ray along the view (200 m) gives the physics surface and
-- hit.renderNode; the plugin ray-tests the drawn mesh there and logs the sub-material and its surface type ("probe mat").
-- Every field of the hit table is logged too (2026-10-17: a rock gave no renderNode, a house wall no hit at 30 m).
local eye, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
local pl = Deagle:Player()
local hits = {}
local ok, n = pcall(Physics.RayWorldIntersection, eye, { x = dir.x * 200, y = dir.y * 200, z = dir.z * 200 }, 1, ent_all,
	pl and pl.id, nil, hits)
local h = ok and n and n > 0 and hits[1]
System.LogAlways(string.format("[Deagle] probe mat: ray ok %s n %s eye %.2f %.2f %.2f dir %.3f %.3f %.3f", tostring(ok),
	tostring(n), eye.x, eye.y, eye.z, dir.x, dir.y, dir.z))
if not h then System.LogAlways("[Deagle] probe mat: nothing within 200 m") return end
local fields = {}
for k, v in pairs(h) do fields[#fields + 1] = k .. "=" .. (type(v) == "table" and "table" or tostring(v)) end
table.sort(fields)
System.LogAlways("[Deagle] probe mat: hit " .. table.concat(fields, " "))
System.LogAlways(string.format("[Deagle] probe mat: physics surface %s at %.2f m, entity %s, renderNode %s",
	Deagle.U.surfaceName(h), h.dist or -1, tostring(h.entity and h.entity:GetName()), tostring(h.renderNode)))
local node = h.renderNode and ("0x" .. tostring(h.renderNode):match("(%x+)$"))   -- "userdata: 0000020A..." (%p, no 0x on Windows)
if not node then   -- this build's hit table has no renderNode: the plugin casts its own ray (from 0.6 m ahead: clear of Henry)
	System.LogAlways(string.format("[DN] probe matray %.3f %.3f %.3f %.4f %.4f %.4f 200", eye.x + dir.x * 0.6,
		eye.y + dir.y * 0.6, eye.z + dir.z * 0.6, dir.x, dir.y, dir.z))
	return
end
System.LogAlways(string.format("[DN] probe mat %s %.3f %.3f %.3f %.4f %.4f %.4f", node, h.pos.x, h.pos.y, h.pos.z, dir.x, dir.y, dir.z))
