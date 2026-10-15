-- The kill cam (src/Scripts/Deagle/killcam.lua): a long shot at a living NPC that will die slows the game to 3 %, the
-- camera rides 1.1 -> 2 m behind the bullet, the hit plays at 10 %, then the game's speed is back; near shots, dead
-- targets and survivable hits get none; a load puts the speed back.
-- usage: luajit tests/run_killcam.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local function vec(x, y, z) return { x = x, y = y, z = z } end

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end

-- the world: one NPC at y = npcY, a wall of nothing else
local npc = { id = "npc1", soul = {}, actor = {}, health = 100 }
function npc:GetName() return "npc1" end
function npc:GetWorldPos() return vec(100, self.y, 30.3) end
function npc:GetDirectionVector() return vec(0, -1, 0) end
function npc:Hide(v) self.hidden = v end
function npc.actor:IsDead() return npc.health <= 0 end
function npc.actor:GetHealth() return npc.health end
local landed, cam
Physics.RayWorldIntersection = function(pos, dir, _, mask, skip1, skip2, hits)
	if mask and mask % 0x400 >= 0x200 then error("a ray asked for ent_water") end
	if dir.y == 0 then return 0 end
	local t = (npc.y - pos.y) / dir.y
	local x, z = pos.x + dir.x * t, pos.z + dir.z * t
	if t >= 0 and t <= 1 and math.abs(x - 100) < 0.5 and z > 29 and z < 32.5 then
		hits[1] = { entity = npc, pos = vec(x, pos.y + dir.y * t, z), normal = vec(0, -1, 0), dist = t * math.sqrt(dir.x ^ 2 + dir.y ^ 2 + dir.z ^ 2), surface = 1 }
		return 1
	end
	return 0
end
function Deagle:ApplyHit(hit, dir, phys, r)   -- the hit itself is tested elsewhere (run_penetration, run_hits)
	landed = { pos = hit.pos, camY = cam and cam.y, t = M.t }
	return false
end

-- the plugin's side of the free camera: it starts at the eye, nudge moves it, and Lua is told it is on / off
local scales, fovs = {}, {}
local setCVar = System.SetCVar
System.SetCVar = function(k, v) if k == "t_Scale" then scales[#scales + 1] = v end return setCVar(k, v) end
local setFov = System.SetViewCameraFov
System.SetViewCameraFov = function(f) fovs[#fovs + 1] = f return setFov(f) end
local seen = 0
local function plugin()
	for i = seen + 1, #M.native do
		local l = M.native[i]
		if l == "[DN] freecam on" then
			local e = System.GetViewCameraPos()
			cam = { x = e.x, y = e.y, z = e.z }
			M.console("deagle_freecam_state", "1")
		elseif l == "[DN] freecam off" then
			cam = nil
			M.console("deagle_freecam_state", "0")
		else
			local dx, dy, dz = l:match("^%[DN%] freecam nudge (%S+) (%S+) (%S+)")
			if dx and cam then cam.x, cam.y, cam.z = cam.x + dx, cam.y + dy, cam.z + dz end
		end
	end
	seen = #M.native
end
local function frames(count)   -- real 1/60 s frames: the game's own time runs at t_Scale of it
	for _ = 1, count do
		local s = tonumber((M.cvars or {}).t_Scale) or 1
		M.advance(s / 60, 60 / s)
		plugin()
	end
end

local function shoot(dist, health)
	npc.y, npc.health, landed, cam = 200 + dist, health, nil, nil
	scales, fovs = {}, {}
	Deagle.state.ammo, Deagle.state.lastShot = Deagle.gun.magSize, -100
	Deagle.cfg.spreadHip, Deagle.cfg.spreadAim, Deagle.cfg.heatPerShot, Deagle.state.heat = 0, 0, 0, 0
	Deagle.cfg.physics.barrelAim = 0
	Deagle.cfg.accuracy.scale = 0
	M.view.pitch, M.view.yaw = 0, 0
	local n0 = #M.native
	Deagle:Shoot(true)
	frames(1)
	return n0
end

-- 1. a long shot that kills
local n0 = shoot(120, 5)
frames(900)
local first, last = scales[1], scales[#scales]
check(string.format("120 m kill: the game slows to %s, after the hit %s, ends at %s (%d changes)", tostring(first),
	tostring(scales[3]), tostring(last), #scales), first == 0.03 and scales[2] == 0.03 and scales[3] == 0.1 and last == 1, #scales)
local onSent, offSent = false, false
for i = n0 + 1, #M.native do
	onSent = onSent or M.native[i] == "[DN] freecam on"
	offSent = offSent or M.native[i] == "[DN] freecam off"
end
check("the free camera went on and back off, and the cam ended", onSent and offSent and Deagle.kc == nil, tostring(Deagle.kc))
check(string.format("the camera was %.2f m behind the bullet when it hit (1.1 - 2, plus the bullet's last frame)", landed and (landed.pos.y - landed.camY) or -1),
	landed and landed.camY and landed.pos.y - landed.camY > 1.05 and landed.pos.y - landed.camY < 2.3, landed and landed.camY)
local fmin, fmax = math.huge, 0
for i = 1, #fovs - 1 do fmin, fmax = math.min(fmin, fovs[i]), math.max(fmax, fovs[i]) end
check(string.format("field of view eased %.1f -> %.1f degrees, then back to the game's (%.1f)", math.deg(fmin), math.deg(fmax), math.deg(fovs[#fovs] or 0)),
	math.abs(math.deg(fmin) - 10) < 0.5 and math.abs(math.deg(fmax) - 30) < 5 and math.abs(fovs[#fovs] - 1.1011) < 1e-3, fovs[#fovs])

-- the organs: at the hit the body is hidden and the organs are there; at the end both are back as they were
do
	local spawned = {}
	local spawn = System.SpawnEntity
	System.SpawnEntity = function(t)
		if t.name and t.name:find("^deagle_organ_") then spawned[#spawned + 1] = t.name end
		return spawn(t)
	end
	local ids0 = Deagle.organIds
	shoot(120, 5)
	local hiddenAtHit, nAtHit
	for _ = 1, 900 do
		frames(1)
		if landed and not hiddenAtHit then hiddenAtHit, nAtHit = npc.hidden, #spawned end
		if not Deagle.kc and landed then break end
	end
	System.SpawnEntity = spawn
	check(string.format("at the hit the body is hidden (%s) and %d organs are shown; at the end the body is back (%s) and they're removed (%s)",
		tostring(hiddenAtHit), nAtHit or -1, tostring(npc.hidden), tostring(Deagle.organIds)),
		hiddenAtHit == 1 and (nAtHit or 0) >= 6 and npc.hidden == 0 and Deagle.organIds == nil, npc.hidden)
end
-- 2. nothing for a near shot, a survivable hit, a dead target, or with the cam off
local cases = {
	{ "a 10 m shot", function() return shoot(10, 5) end },
	{ "a hit that doesn't kill (100 health, 9 mm)", function() return shoot(120, 100) end },
	{ "a dead target", function() return shoot(120, 0) end },
}
for _, c in ipairs(cases) do
	c[2]()
	frames(300)
	check(c[1] .. ": no kill cam, speed untouched", #scales == 0 and Deagle.kc == nil, #scales)
end
Deagle.cfg.killCam.on = false
shoot(120, 5); frames(300)
check("killCam.on false: none", #scales == 0, #scales)
Deagle.cfg.killCam.on = true

-- 3. a load in the middle: the speed goes back
shoot(120, 5)
for _ = 1, 300 do if Deagle.kc then break end frames(1) end
frames(3)
local mid = (M.cvars or {}).t_Scale
Deagle:OnGameplayStarted()
check(string.format("a save load mid-cam puts the speed back (x%s -> x%s)", tostring(mid), tostring(M.cvars.t_Scale)),
	mid == 0.03 and M.cvars.t_Scale == 1 and Deagle.kc == nil, M.cvars.t_Scale)

-- 4. deagle_reset keeps the settings (cfgDefaults) and the speed sets are real settings
M.console("deagle_set", "killCam.minDist 60")
local v60 = Deagle.cfg.killCam.minDist
M.console("deagle_reset")
check(string.format("killCam.minDist is a setting (60 -> reset %s)", tostring(Deagle.cfg.killCam.minDist)),
	v60 == 60 and Deagle.cfg.killCam and Deagle.cfg.killCam.minDist == 40, Deagle.cfg.killCam)

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("killcam: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
