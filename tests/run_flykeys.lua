-- The Flight mod's keys through the plugin (Deagle:FlyLink / FlyKey, native/src/main.cpp flykeys): KCD2 sends Lua no key
-- actions while Henry carries a body, so the plugin reads Flight's keys and Lua sets them. Lua takes them over only once
-- the plugin answers (an older plugin doesn't, and Flight keeps its own keys); without the Flight mod nothing is sent.
-- usage: luajit tests/run_flykeys.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
local function sent(prefix)
	for _, l in ipairs(M.native) do if l:find("^%[DN%] " .. prefix) then return true end end
	return false
end

Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
check("no Flight mod: no flykeys sent", not sent("flykeys"), "sent")

-- the Flight mod's globals as Mods/flight's Scripts/mods/flight.lua makes them
local said = {}
Flight = { Keys = { forward = false, back = false, left = false, right = false, up = false, down = false, fast = false, slow = false },
	active = false, log = function(s) said[#said + 1] = s end }
M.console("deagle_native_ok")
check("Flight mod: the plugin is asked for its keys (flykeys 1)", sent("flykeys 1"), "not sent")
check("before the plugin answers, Flight keeps its own key actions", Deagle:OnAction("flight_forward_keyboard", "press", 1) ~= true, "taken")

M.console("deagle_flykey", "linked 1")
check("after the plugin answers, Flight's own key actions are dropped (no double F4)", Deagle:OnAction("flight_toggle_keyboard", "press", 1) == true
	and Deagle:OnAction("flight_up_keyboard", "press", 1) == true, "passed on")

M.console("deagle_flykey", "forward 1"); M.console("deagle_flykey", "up 1")
check("keys held: forward and up", Flight.Keys.forward == true and Flight.Keys.up == true and Flight.Keys.back == false, Flight.Keys.forward)
M.console("deagle_flykey", "forward 0")
check("key let go: forward off, up still held", Flight.Keys.forward == false and Flight.Keys.up == true, Flight.Keys.forward)
M.console("deagle_flykey", "toggle 1"); M.console("deagle_flykey", "toggle 0")
check("F4 press turns flight on (its release does nothing), and says so", Flight.active == true and said[#said] == "Flight On", tostring(Flight.active) .. " " .. tostring(said[#said]))
M.console("deagle_flykey", "toggle 1")
check("F4 again turns it off", Flight.active == false and said[#said] == "Flight Off", Flight.active)
M.console("deagle_flykey", "bogus 1"); M.console("deagle_flykey", "")
check("an unknown key or empty line adds nothing", Flight.Keys.bogus == nil, Flight.Keys.bogus)
M.console("deagle_flykey", "linked 0")
check("plugin lets go: Flight's own actions pass again", Deagle:OnAction("flight_forward_keyboard", "press", 1) ~= true, "taken")

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("flykeys: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
