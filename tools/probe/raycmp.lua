-- tools/live.sh tools/probe/raycmp.lua (Claude 5, 2026-10-17; needs the plugin from 9ec7e9b): do the plugin's rays
-- (flags 0x1000F, docs/whgame_re/sim_binding.md) hit what Lua's do, and what does one cost? 24 rays in a fan around the
-- view (8 across x 3 heights), each cast by Lua (as FlightHit: ent_all, the player skipped) and by the plugin from the
-- same point (0.6 m ahead of the eye, clear of Henry), 100 m long. Lua's answers: "[Deagle] probe raycmp i: dist surface"
-- in kcd.log; the plugin's: "probe ray: first returned ... (dist, surface_idx); ... us each" in deagle_native.log (200
-- rays each, timed). Compare line by line: same dist (to 1 mm) and surface on every row. Read-only.
local eye, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
local pl = Deagle:Player()
local yaw = math.atan2(dir.y, dir.x)
local i = 0
for _, pitch in ipairs({ -0.35, -0.1, 0.15 }) do
	for k = 0, 7 do
		i = i + 1
		local a = yaw + (k - 3.5) * 0.12
		local d = { x = math.cos(a) * math.cos(pitch), y = math.sin(a) * math.cos(pitch), z = math.sin(pitch) }
		local o = { x = eye.x + d.x * 0.6, y = eye.y + d.y * 0.6, z = eye.z + d.z * 0.6 }
		local hits = {}
		local ok, n = pcall(Physics.RayWorldIntersection, o, { x = d.x * 100, y = d.y * 100, z = d.z * 100 }, 1, ent_all,
			pl and pl.id, nil, hits)
		local h = ok and n and n > 0 and hits[1]
		System.LogAlways(string.format("[Deagle] probe raycmp %d: %s", i,
			h and string.format("dist %.3f surface %s entity %s", h.dist, tostring(h.surface), tostring(h.entity and h.entity:GetName()))
			or "nothing"))
		System.LogAlways(string.format("[DN] probe ray %.3f %.3f %.3f %.4f %.4f %.4f 100 200", o.x, o.y, o.z, d.x, d.y, d.z))
	end
end
