-- tools/live.sh tools/probe/light.lua (architecture step 1, Claude 5; needs native/src/probe.cpp). An entity-free
-- light (C3DEngine::CreateLightSource), warm, radius 6 m, 3 m ahead and 1 m up: 4 s still, then moved sideways every
-- 0.5 s with SetMatrix only (4 s), then with unregister + register (4 s), then deleted. Game in front; record it
-- or watch: the ground lights up with no entity. Results in deagle_native.log ("probe light").
local eye, dir = System.GetViewCameraPos(), System.GetViewCameraDir()
local base = { x = eye.x + dir.x * 3, y = eye.y + dir.y * 3, z = eye.z + 1 }
local side = { x = -dir.y, y = dir.x }
local function at(k, mode)
	local o = (k % 2 == 0) and 1.5 or -1.5
	System.LogAlways(string.format("[DN] probe light %.3f %.3f %.3f 4 3 2 6 %d", base.x + side.x * o, base.y + side.y * o, base.z, mode))
end
System.LogAlways(string.format("[DN] probe light %.3f %.3f %.3f 4 3 2 6 0", base.x, base.y, base.z))
for k = 1, 16 do
	Script.SetTimer(4000 + k * 500, function()
		if k <= 8 then at(k, 0) elseif k <= 15 then at(k, 1) else System.LogAlways("[DN] probe lightoff") end
	end)
end
System.LogAlways("[Deagle] probe light: running for 12 s (see deagle_native.log)")
