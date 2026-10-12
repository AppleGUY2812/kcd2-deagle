-- the plugin never answers: Lua placement + game shot sound must carry the mod
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted()
Deagle:OnAction("deagle_toggle", "press"); M.advance(1.5)
local vm = System.GetEntity(Deagle.vmId)
for i = 1, 3 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end
Deagle:OnAction("deagle_reload", "press"); M.advance(3)
print("native linked:", tostring(Deagle.native), " vm placed by Lua at camera:", vm and string.format("%.2f %.2f %.2f", vm.pos.x, vm.pos.y, vm.pos.z) or "nil",
	" game shot sounds:", M.gameShot or 0, " plugin sound cmds:", (function() local n = 0 for _, c in ipairs(M.native) do if c:find("snd") then n = n + 1 end end return n end)())
-- the flash on the gun needs the plugin: without it that's said once (an error, never swapped for another kind of flash)
local flashSaid, other = 0, {}
for _, e in ipairs(M.errors) do
	if e:find("muzzle flash: the native plugin isn't running", 1, true) then flashSaid = flashSaid + 1 else other[#other + 1] = e end
end
if flashSaid ~= 1 then other[#other + 1] = "missing-plugin flash error said " .. flashSaid .. " times (want once)" end
print("FALLBACK ERRORS:", #other); for _, e in ipairs(other) do print("  " .. e) end
