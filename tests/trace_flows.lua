-- Records every engine effect of one scripted play session into a file, to compare two versions of the script.
-- usage: luajit tests/trace_flows.lua <script.lua> <out.txt> [native 1|0]
local script, out, native = arg[1], arg[2], arg[3] ~= "0"
local M = dofile("tests/mock_engine.lua")
local function run(label, fn) M.trace[#M.trace + 1] = "== " .. label; local ok, e = pcall(fn); if not ok then M.trace[#M.trace + 1] = "!! " .. tostring(e) end end
run("stale entities", function()
	System.SpawnEntity({ class = "BasicEntity", name = "deagle_viewmodel", properties = {} })
	System.SpawnEntity({ class = "BasicEntity", name = "deagle_vm_probe", properties = {} })
end)
run("load", function() dofile(script) end)
run("gameplay started", function() Deagle:OnGameplayStarted(); Deagle.cfg.vm.ads = true end)
if native then run("handshake", function() M.console("deagle_native_ok") end) end
run("draw", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.6) end)
run("hip fire x7", function() for i = 1, 7 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end M.advance(1.0) end)
run("dry fire", function() Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end)
run("empty reload", function() Deagle:OnAction("deagle_reload", "press"); M.advance(3.2) end)
run("aim in", function() Deagle:OnAction("deagle_aim", "press"); M.advance(0.3) end)
run("aimed fire x2", function() for i = 1, 2 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end M.advance(2.0) end)
run("aimed fire to empty", function() for i = 1, 5 do Deagle:OnAction("deagle_fire", "press"); M.advance(0.35) end M.advance(1.0) end)
run("inspect while aiming (ignored)", function() Deagle:OnAction("deagle_inspect", "press"); M.advance(0.2) end)
run("reload while aiming", function() Deagle:OnAction("deagle_reload", "press"); M.advance(3.2) end)
run("aim out", function() Deagle:OnAction("deagle_aim", "release"); M.advance(0.3) end)
run("partial reload", function() Deagle:OnAction("deagle_fire", "press"); M.advance(0.5); Deagle:OnAction("deagle_reload", "press"); M.advance(3.0) end)
run("inspect", function() Deagle:OnAction("deagle_inspect", "press"); M.advance(7.0) end)
run("inspect, fire mid-way", function() Deagle:OnAction("deagle_inspect", "press"); M.advance(1.0); Deagle:OnAction("deagle_fire", "press"); M.advance(2.0) end)
run("dialogue camera", function()
	local real = System.GetViewCameraPos
	System.GetViewCameraPos = function() return { x = 103, y = 203, z = 32 } end
	M.advance(0.5)
	System.GetViewCameraPos = real
	M.advance(0.5)
end)
run("holster", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.5) end)
run("fire while holstered", function() Deagle:OnAction("deagle_fire", "press"); M.advance(0.3) end)
run("draw again", function() Deagle:OnAction("deagle_toggle", "press"); M.advance(1.6) end)
run("holster mid-reload", function() Deagle:OnAction("deagle_fire", "press"); M.advance(0.4); Deagle:OnAction("deagle_reload", "press"); M.advance(0.5); Deagle:OnAction("deagle_toggle", "press"); M.advance(3.0) end)
run("console", function() for _, c in ipairs({ "deagle_arms", "deagle_motion", "deagle_hud", "deagle_native_test", "deagle_status" }) do M.console(c) end M.advance(0.2) end)
run("errors", function() for _, e in ipairs(M.errors) do M.trace[#M.trace + 1] = "ERR " .. e end end)
local f = io.open(out, "w"); f:write(table.concat(M.trace, "\n"), "\n"); f:close()
print(#M.trace .. " trace lines -> " .. out .. ", errors " .. #M.errors)
