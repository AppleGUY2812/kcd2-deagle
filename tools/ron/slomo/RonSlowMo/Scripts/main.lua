-- Ready or Not slow motion for recording effects (muzzle flash) frame by frame. UE4SS Lua mod; keys and speeds in config.lua.
-- Sets the world's global time dilation (UGameplayStatics::SetGlobalTimeDilation), like the engine's `slomo` command:
-- everything driven by game time slows (particles, animation, physics, AI); mouse look and sound do not.
-- Single player only: in multiplayer the server's time dilation wins.
local UEHelpers = require("UEHelpers")
local cfg = require("config")

local FROZEN = 0.0001 -- AWorldSettings::MinGlobalTimeDilation, the lowest the engine accepts
local speeds = cfg.speeds
local idx = 1
for i, s in ipairs(speeds) do
	if math.abs(s - cfg.start_speed) < math.abs(speeds[idx] - cfg.start_speed) then idx = i end
end
local resume = speeds[idx] -- the speed a freeze goes back to

local function log(msg) print("[RonSlowMo] " .. msg .. "\n") end

local function current() -- game thread only
	local ws = UEHelpers.GetWorldSettings()
	if not ws:IsValid() then return nil end
	return ws.TimeDilation
end

local function set(v) -- game thread only
	local world = UEHelpers.GetWorldContextObject()
	if not world:IsValid() then log("no world yet") return end
	UEHelpers.GetGameplayStatics():SetGlobalTimeDilation(world, v)
	log(string.format("speed %g (asked %g)", current() or -1, v))
end

local function bind(name, fn)
	local key = Key[cfg.keys[name]]
	if not key then log("config.lua: unknown key '" .. tostring(cfg.keys[name]) .. "' for " .. name) return end
	RegisterKeyBind(key, function() ExecuteInGameThread(fn) end)
end

bind("toggle", function()
	local t = current()
	if not t then return end
	if t < 0.999 then set(1.0) else set(speeds[idx]) end
end)

bind("slower", function()
	idx = math.min(idx + 1, #speeds); resume = speeds[idx]; set(speeds[idx])
end)

bind("faster", function()
	idx = math.max(idx - 1, 1); resume = speeds[idx]; set(speeds[idx])
end)

bind("freeze", function()
	local t = current()
	if not t then return end
	if t <= FROZEN * 1.1 then set(resume) else resume = t; set(FROZEN) end
end)

bind("step", function()
	local t = current()
	if not t then return end
	if t > FROZEN * 1.1 then resume = t; set(FROZEN) return end -- not frozen: freeze first
	set(cfg.step_speed)
	if EngineTickAvailable then
		ExecuteInGameThreadAfterFrames(cfg.step_frames, function() set(FROZEN) end)
	else -- no per-frame hook: ~1 frame at 60 fps per step frame
		ExecuteInGameThreadWithDelay(math.floor(cfg.step_frames * 1000 / 60 + 0.5), function() set(FROZEN) end)
	end
end)

log(string.format("loaded: %s on/off, %s slower, %s faster, %s freeze, %s step; start speed %g",
	cfg.keys.toggle, cfg.keys.slower, cfg.keys.faster, cfg.keys.freeze, cfg.keys.step, speeds[idx]))
