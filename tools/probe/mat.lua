-- tools/live.sh tools/probe/mat.lua (architecture step 1, Claude 5; needs native/src/probe.cpp). Aim at the campfire
-- cauldron (or any static object), then run: one Lua ray along the view gives the physics surface and hit.renderNode;
-- the plugin ray-tests the drawn mesh there and logs the sub-material and its surface type ("probe mat").
local eye, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
local pl = Deagle:Player()
local hits = {}
local n = Physics.RayWorldIntersection(eye, { x = dir.x * 30, y = dir.y * 30, z = dir.z * 30 }, 1, ent_all, pl and pl.id, nil, hits)
local h = n and n > 0 and hits[1]
if not h then System.LogAlways("[Deagle] probe mat: nothing within 30 m") return end
local surf = Deagle.U.surfaceName(h.surface)
local node = h.renderNode and ("0x" .. tostring(h.renderNode):match("(%x+)$"))   -- "userdata: 0000020A..." (%p, no 0x on Windows)
System.LogAlways(string.format("[Deagle] probe mat: physics surface %s at %.2f m, entity %s, renderNode %s",
	tostring(surf), h.dist or -1, tostring(h.entity and h.entity:GetName()), tostring(h.renderNode)))
if not node then return end
System.LogAlways(string.format("[DN] probe mat %s %.3f %.3f %.3f %.4f %.4f %.4f", node, h.pos.x, h.pos.y, h.pos.z, dir.x, dir.y, dir.z))
