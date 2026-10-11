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
print("FALLBACK ERRORS:", #M.errors); for _, e in ipairs(M.errors) do print("  " .. e) end
