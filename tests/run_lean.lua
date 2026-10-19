-- Q / E lean with a gun out (freecam.lua LeanTick, the plugin's lean.cpp): the plugin hears the lean's numbers once and
-- the target each change; a wall cuts the target short; with the gun put away it's upright and Q / E are KCD2's again
-- (its chat maps back on, no_interaction off); an older plugin (no "lean") leaves Q and E alone.
-- usage: luajit tests/run_lean.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function last(pat) for i = #M.native, 1, -1 do local v = M.native[i]:match(pat) if v then return v end end end
local function count(pat, from) local c = 0 for i = from or 1, #M.native do if M.native[i]:find(pat) then c = c + 1 end end return c end
local function act(a, x) return Deagle:OnAction(a, x or "press") end

Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local L = Deagle.cfg.lean
check("the plugin can lean (caps lean)", Deagle.nativeCaps and Deagle.nativeCaps.lean, "no caps")
check("holstered: Q / E are KCD2's", act("deagle_lean_right") ~= true, "taken")
act("deagle_lean_right", "release")

local n0 = #M.native
act("deagle_toggle"); M.advance(2.0)
check("drawn: KCD2's chat maps off, no_interaction on", M.maps.chat_init == false and M.maps.chat_init_interactor == false
	and last("nointeract (%d)") == "1", tostring(M.maps.chat_init) .. " " .. tostring(last("nointeract (%d)")))
check("Ready or Not's lean numbers sent once (in 400, out 30, cam 8, dip 0.5, cant 8, cantmax 25)", count("leanp ", n0) == 6
	and tonumber(last("leanp in ([%d.]+)")) == 400 and tonumber(last("leanp cantmax ([%d.]+)")) == 25, count("leanp ", n0))
check("upright: lean 0", tonumber(last("lean ([%-%d.]+)$")) == 0, last("lean ([%-%d.]+)$"))

check("E leans right", act("deagle_lean_right") == true, "not taken")
M.advance(0.05)
check("  plugin target 15 cm", tonumber(last("lean ([%-%d.]+)$")) == 15, last("lean ([%-%d.]+)$"))
act("deagle_lean_right", "release"); act("deagle_lean_left"); M.advance(0.05)
check("Q leans left: target -15 cm", tonumber(last("lean ([%-%d.]+)$")) == -15, last("lean ([%-%d.]+)$"))

-- a wall 25 cm to the left of the chest: the capsule (radius 5 cm) travels 20 cm, so 20 - 15 = 5 cm of lean
local ray = Physics.RayWorldIntersection
Physics.RayWorldIntersection = function(from, move, nh, flags, s1, s2, hits) hits[1] = { dist = 0.25, pos = from } return 1 end
M.advance(0.05)
local want = -5
check(string.format("a wall 25 cm to the left: target %.1f cm", want), math.abs(tonumber(last("lean ([%-%d.]+)$")) - want) < 0.01,
	last("lean ([%-%d.]+)$"))
Physics.RayWorldIntersection = ray
act("deagle_lean_left", "release"); M.advance(0.05)
check("let go: upright", tonumber(last("lean ([%-%d.]+)$")) == 0, last("lean ([%-%d.]+)$"))

act("deagle_lean_left"); M.advance(0.05)
act("deagle_toggle"); M.advance(2.0)
check("holstered while leaning: upright, chat maps back, no_interaction off", tonumber(last("lean ([%-%d.]+)$")) == 0
	and M.maps.chat_init == true and last("nointeract (%d)") == "0", tostring(last("lean ([%-%d.]+)$")) .. " " .. tostring(M.maps.chat_init))
act("deagle_lean_left", "release")

-- an older plugin: no lean, Q and E untouched
M.console("deagle_native_caps", "files fxr smoke ronvm lowready")
M.maps.chat_init = nil
local n1 = #M.native
act("deagle_toggle"); M.advance(1.0)
check("older plugin: Q / E are KCD2's, its chat maps and E's use left alone",
	act("deagle_lean_right") ~= true and M.maps.chat_init == nil and count("nointeract", n1) == 0 and count("lean ", n1) == 0,
	tostring(M.maps.chat_init) .. " " .. count("nointeract", n1))
act("deagle_lean_right", "release")

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("lean: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
