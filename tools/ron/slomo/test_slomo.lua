-- luajit tools/ron/slomo/test_slomo.lua: RonSlowMo's keys against a fake UE4SS (time dilation clamped like the engine's)
local dir = arg[0]:match("^(.*)/[^/]*$") .. "/RonSlowMo/Scripts/"
package.path = dir .. "?.lua;" .. package.path
local td, binds, after, fails = 1.0, {}, nil, 0
local valid = { IsValid = function() return true end }
package.loaded.UEHelpers = {
	GetWorldSettings = function() return setmetatable({ IsValid = valid.IsValid }, { __index = function(_, k) if k == "TimeDilation" then return td end end }) end,
	GetWorldContextObject = function() return valid end,
	GetGameplayStatics = function() return { SetGlobalTimeDilation = function(_, _, v) td = math.min(math.max(v, 0.0001), 20) end } end,
}
Key = setmetatable({}, { __index = function(_, k) return k end })
RegisterKeyBind = function(k, fn) binds[k] = fn end
ExecuteInGameThread = function(fn) fn() end
EngineTickAvailable = true
ExecuteInGameThreadAfterFrames = function(n, fn) after = { n = n, fn = fn } end
print = function() end
dofile(dir .. "main.lua")
local keys = dofile(dir .. "config.lua").keys
local function press(action) binds[keys[action]]() end   -- by action, so the test follows config.lua's keys
local function check(name, want) local ok = math.abs(td - want) < 1e-9
	io.write((ok and "PASS " or "FAIL ") .. name .. string.format(" (%g, want %g)\n", td, want)); if not ok then fails = fails + 1 end end

press("toggle"); check("toggle on = start speed", 0.05)
press("toggle"); check("toggle off", 1.0)
press("slower"); check("slower turns on one step down", 0.02)
press("faster"); press("faster"); check("faster twice", 0.1)
press("freeze"); check("freeze", 0.0001)
press("step"); check("step runs at step_speed", 0.1)
after.fn(); check("refreezes after the frame", 0.0001)
press("freeze"); check("unfreeze back to the slow speed", 0.1)
td = 1.0; press("toggle"); check("new map (time reset to 1): toggle turns on", 0.1)
press("step"); check("step while running freezes first", 0.0001)
for _ = 1, 20 do press("slower") end; check("slower clamps at the list end", 0.005)
for _ = 1, 20 do press("faster") end; check("faster clamps at the list start", 0.5)
print = io.write
if fails > 0 then os.exit(1) end
io.write("SLOMO PASS\n")
