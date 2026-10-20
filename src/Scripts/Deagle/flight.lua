-- Flight (deagle-game-todo-notion, 2026-10-19): Henry flies, also while he carries a body; replaces the Flight mod.
-- The Flight mod moved Henry with player:SetPos every frame and crashed the game three times on 2026-10-19 (BugSplat
-- WHGame!0080eaa6: SetPos -> the entity's XFORM event -> CLivingEntity::SetParams(pe_params_pos) read Henry's collider
-- while its phys_geometry was NULL; carrying a body swaps the capsule, the carryCorpse stance of BasicActor.lua).
-- So nothing here teleports him: while flying his living physics gets no gravity and no air control
-- (PHYSICPARAM_PLAYERDYN) and his velocity is set every frame (PHYSICPARAM_VELOCITY, pe_action_set_velocity), eased
-- toward where the keys point. Turned off in the air, he first sinks to the ground and only then gets his gravity back,
-- so there's no fall. Keys: F4 on / off, W A S D where you look, Space up, Left Alt down, Shift faster, Ctrl slower;
-- they come from the plugin (the flykeys bridge in deagle_main.lua, Deagle:FlyKey: KCD2 gives scripts no keys while he
-- carries a body), which drives this global `Flight` the way it drove the Flight mod's.
-- deagle_fly toggles from the console; deagle_fly_speed <m/s> sets the speed.
local U = Deagle.U
local vadd, vscale, vsub = U.vadd, U.vscale, U.vsub

Deagle.flightCfg = Deagle.flightCfg or {
	speed = 10,         -- m/s, KCD2's sprint is ~6
	fast = 4, slow = 0.25,   -- Shift / Ctrl factors
	accel = 8,          -- 1/s: how fast the velocity eases to the keys' (about 0.3 s to full speed, no jerk)
	landSpeed = 6,      -- m/s down while landing after F4 off
	landGap = 0.25,     -- m above the ground where landing ends
	landMax = 30,       -- s: landing gives up (gravity back) after this
	-- KCD2 measures a fall by HEIGHT from the last ground contact, frozen for the whole flight (docs/whgame_re/fall_damage.md:
	-- FUN_1809d36f0 keeps det+0x14 at the take-off Z, FUN_1807243b0 scales the drop by the FallDamageMultiplier stat), so any
	-- landing 1.5 m under the take-off point hurt however slow. The game's own "fall_damage" buff (buff.xml, fdm=0) zeroes
	-- that multiplier: worn while flying and for buffLinger s after the landing. It is persistent (a save mid-flight keeps
	-- it), so it's also removed at every script load and whenever flight is off.
	fallBuff = "4bc0b081-a57e-4e6e-8297-6d9db58b39b2",
	buffLinger = 1,     -- s after gravity is back before the buff goes
}
-- What Henry's physics gets back when flight ends: read from his own pe_player_dynamics before anything changes
-- (the plugin's "flydyn <entity hex>": his living entity's set gravity, +0x18C, and GetParams' kAirControl, answered
-- by deagle_flydyn <gravity> <kAirControl> <kAirResistance>, or deagle_flydyn fail <token>). No read, no flight: no constants guessed (a stance or a mod may have changed them).
-- (k_air_resistance isn't read by KCD2's PLAYERDYN binding, 0x180B35634: air drag stays, small between the per-frame sets)
local FLYING = { gravity = 0, k_air_control = 0 }

local function fallBuff(p, on)
	local soul, id = p and p.soul, Deagle.flightCfg.fallBuff
	if not soul then return end
	if on then pcall(soul.AddBuff, soul, id) else pcall(soul.RemoveAllBuffsByGuid, soul, id) end
end

Flight = Flight or {}
Flight.Keys = Flight.Keys or { forward = false, back = false, left = false, right = false, up = false, down = false, fast = false, slow = false }
Flight.active = Flight.active or false
Flight.ours = true
-- the Flight mod (Mods/flight) if it's still installed: its loop re-arms itself by name (SetTimerForFunction
-- "Flight.Loop") and calls player:SetPos while Flight.active: the crash. A no-op under that name ends it.
if type(Flight.Loop) == "function" then Flight.Loop = function() end; System.LogAlways("[Deagle] the Flight mod's SetPos loop is off: flight.lua flies") end
-- every F4 (the bridge calls Flight.log on each toggle) and deagle_fly make sure the watch loop runs: a save load
-- drops script timers, and the loop started at file load died with them (in game 2026-10-19: "Flight On", nothing flew)
function Flight.log(s)
	System.LogAlways("[Deagle] " .. s)
	if Deagle.Notify then Deagle:Notify(s) end
	if Deagle.FlightWatchKick then Deagle:FlightWatchKick() end
end

local function dyn(p, t) pcall(p.SetPhysicParams, p, PHYSICPARAM_PLAYERDYN, t) end
local function setVel(p, v) pcall(p.SetPhysicParams, p, PHYSICPARAM_VELOCITY, { v = v }) end

-- metres from the feet down to the ground (nil: none within 200 m). No ent_water in the mask (it crashed the ray code
-- once: MODLOG 2026-10-14); water counts as ground through CryAction.GetWaterInfo.
local function groundGap(p)
	local pos = p:GetWorldPos()
	local hits = {}
	local mask = (ent_static or 1) + (ent_terrain or 0x100) + (ent_rigid or 2) + (ent_sleeping_rigid or 4)
	local ok, n = pcall(Physics.RayWorldIntersection, vadd(pos, { x = 0, y = 0, z = 0.5 }), { x = 0, y = 0, z = -200.5 }, 1, mask, p.id, nil, hits)
	local gap = (ok and n and n > 0 and hits[1]) and (hits[1].dist - 0.5) or nil
	local okw, level = pcall(CryAction.GetWaterInfo, pos)
	if okw and type(level) == "number" and level > -1000 and pos.z - level >= 0 and (not gap or pos.z - level < gap) then gap = pos.z - level end
	return gap
end

-- the velocity the keys ask for: forward / back along the view (so looking up and holding W climbs), strafe level
function Deagle:FlightTarget()
	local K, c = Flight.Keys, self.flightCfg
	local f = System.GetViewCameraDir()
	local r = { x = f.y, y = -f.x, z = 0 }   -- the view's right, level
	local rl = math.sqrt(r.x * r.x + r.y * r.y); if rl > 1e-4 then r = vscale(r, 1 / rl) end
	local v = { x = 0, y = 0, z = 0 }
	if K.forward then v = vadd(v, f) end
	if K.back then v = vsub(v, f) end
	if K.right then v = vadd(v, r) end
	if K.left then v = vsub(v, r) end
	if K.up then v.z = v.z + 1 end
	if K.down then v.z = v.z - 1 end
	local len = math.sqrt(v.x * v.x + v.y * v.y + v.z * v.z)
	if len < 1e-4 then return { x = 0, y = 0, z = 0 } end
	local s = c.speed * (K.fast and c.fast or 1) * (K.slow and c.slow or 1)
	return vscale(v, s / len)
end

function Deagle:FlightStart()
	local p = self:Player()
	if not p or self.flight or self.flightAsk then return end
	if not (self.nativeCaps and self.nativeCaps.flydyn) then
		Flight.active = false
		Flight.log("Flight needs the plugin from 2026-10-19 or later (it reads Henry's physics first)") return
	end
	self.flightAsk = System.GetCurrTime()
	self:Native("flydyn " .. U.entityHex(p.id))
end

-- the plugin's answer: Henry's own values, then the flight starts
function Deagle:FlightDyn(line)
	local a = tostring(Deagle.Arg(line) or "")
	local asked = self.flightAsk
	self.flightAsk = nil
	if not asked or not Flight.active then return end
	local g, ac, ar = a:match("^([%d.%-]+)%s+([%d.%-]+)%s+([%d.%-]+)")
	if not g then Flight.active = false; Flight.log("Flight off: the plugin couldn't read Henry's physics (" .. a .. ")") return end
	local p = self:Player()
	if not p then Flight.active = false return end
	self.flight = { v = { x = 0, y = 0, z = 0 }, token = {},
		living = { gravity = tonumber(g), k_air_control = tonumber(ac) } }   -- ar: read, not settable from Lua
	local okv, v = pcall(p.GetVelocity, p)
	if okv and type(v) == "table" and v.x then self.flight.v = { x = v.x, y = v.y, z = v.z } end   -- no jolt at the start
	dyn(p, FLYING)
	self.flightBuffOff = nil
	fallBuff(p, true)
	local tok = self.flight.token
	Script.SetTimer(0, function() Deagle:FlightTick(tok) end)
end

function Deagle:FlightStop(now)
	local p, F = self:Player(), self.flight
	if not F then return end
	local gap = p and groundGap(p)
	if not now and gap and gap > self.flightCfg.landGap then   -- in the air: sink to the ground first
		F.landing = System.GetCurrTime(); return
	end
	self.flight = nil
	if p then dyn(p, F.living) end
	-- the landing itself is the fall the game measures: keep the buff until it's over
	local tok = {}; self.flightBuffOff = tok
	Script.SetTimer(math.floor(self.flightCfg.buffLinger * 1000), function()
		if Deagle.flightBuffOff == tok and not Deagle.flight then fallBuff(Deagle:Player(), false) end
	end)
end

function Deagle:FlightTick(token)
	local F, p = self.flight, self:Player()
	if not F or F.token ~= token then return end
	if not p then self.flight = nil return end
	local c = self.flightCfg
	local now = System.GetCurrTime()   -- game time: F9 slow motion slows the flight too
	local dt = math.max(0, math.min(now - (F.last or now), 0.1)); F.last = now
	if F.landing then
		local gap = groundGap(p)
		if not gap or gap <= c.landGap or System.GetCurrTime() - F.landing > c.landMax then
			self:FlightStop(true) return
		end
		F.target = { x = 0, y = 0, z = -math.min(c.landSpeed, gap * 2 + 0.3) }   -- eases down over the last ~3 m
	else
		F.target = self:FlightTarget()
	end
	local k = 1 - math.exp(-c.accel * (F.landing and 2 or 1) * dt)   -- landing follows its target closer
	F.v = vadd(F.v, vscale(vsub(F.target, F.v), k))
	dyn(p, FLYING)        -- every frame: a stance change (picking up a body) may re-apply the actor's own params
	setVel(p, F.v)
	Script.SetTimer(0, function() Deagle:FlightTick(token) end)
end

-- scroll while flying: speed x1.25 a notch (1-200 m/s), shown on screen. KCD2 sends no scroll while Henry carries a
-- body, so set it before picking one up. Not flying: the wheel stays the game's.
function Deagle:FlightScroll(faster, activation)
	if not self.flight or self.flight.landing then return false end
	if activation ~= "press" then return true end
	local c = self.flightCfg
	c.speed = math.max(1, math.min(200, c.speed * (faster and 1.25 or 1 / 1.25)))
	self:Notify(string.format("flight %.0f m/s", c.speed))
	return true
end

-- Flight.active is flipped by the plugin bridge (F4) or deagle_fly; this follows it
function Deagle:FlightWatchKick()
	-- the loop ticks every 50 ms: no tick for 0.2 s = dead (abs: a load can put the game clock back)
	if not self.flightWatchAt or math.abs(System.GetCurrTime() - self.flightWatchAt) > 0.2 then self:FlightWatch() end
end
function Deagle:FlightWatch()
	self.flightWatchAt = System.GetCurrTime()
	local want = Flight.active == true
	if self.flightAsk and System.GetCurrTime() - self.flightAsk > 1 then   -- the plugin never answered
		self.flightAsk = nil; Flight.active = false; want = false
		Flight.log("Flight off: no answer from the plugin")
	end
	if want and (not self.flight or self.flight.landing) then
		if self.flight then self.flight.landing = nil else self:FlightStart() end
	elseif not want and self.flight and not self.flight.landing then
		self:FlightStop()
	end
	Script.SetTimer(50, function() Deagle:FlightWatch() end)
end
Deagle:FlightWatchKick()
if not Deagle.flight then fallBuff(Deagle:Player(), false) end   -- a load mid-flight brought the persistent buff along

System.AddCCommand("deagle_fly", "Flight.active = not Flight.active; Flight.log(Flight.active and 'Flight On' or 'Flight Off')", "deagle_fly: flight on / off (F4)")
System.AddCCommand("deagle_flydyn", "Deagle:FlightDyn([[%line]])", "internal: the plugin's read of Henry's pe_player_dynamics (gravity, air control, air resistance)")
System.AddCCommand("deagle_fly_speed", "local s = tonumber(Deagle.Arg([[%line]])); if s and s > 0 then Deagle.flightCfg.speed = s end; System.LogAlways('[Deagle] flight speed ' .. Deagle.flightCfg.speed .. ' m/s')", "deagle_fly_speed <m/s>: flying speed (default 10; Shift x4, Ctrl x0.25)")
