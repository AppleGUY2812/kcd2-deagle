-- Hits on living things (deagle_main.lua ApplyHit) in the mock game, for both guns: a round into the skull kills (it
-- left 31 of 100 HP in game, 2026-10-14), one a helmet stops or padding drains below the bone threshold doesn't, and a
-- chicken (a Boid of a flock, no soul) dies through the game's own Boids.OnBoidHit and the round flies on through it;
-- after a save load that sets the game clock back, shots still fly a real direction.
-- usage: luajit tests/run_hits.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")

local function vec(x, y, z) return { x = x, y = y, z = z } end
local n, fails = 0, 0
local function check(name, ok, got)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. name .. (ok and "" or ("   (got " .. tostring(got) .. ")")))
end

local function newNpc()
	local e = { soul = {}, actor = {}, human = {}, class = "NPC", damage = 0, id = "npc" }
	function e:GetName() return "npc" end
	function e:GetWorldPos() return vec(100, 205, 30.1) end
	e.GetBonePos = M.skeleton
	function e:GetDirectionVector() return vec(0, -1, 0) end
	function e:GetMass() return 80 end
	function e:AddImpulse() end
	function e.soul:DealDamage(st, hp) e.damage = e.damage + hp end
	function e.soul:AddBuff() end
	function e.actor:IsDead() return e.damage >= 100 end
	function e.actor:GetHealth() return math.max(0, 100 - e.damage) end
	function e.actor:GetMaxHealth() return 100 end
	function e.actor:RagDollize() end
	function e.actor:AddBlood() end
	return e
end
-- a shot straight at the head (the mock skeleton's head bone, facing -y) from 1 m
local function headShot(e, gun)
	Deagle:UseGun(gun)
	local head = M.skeleton(e, "Bip01 Head") or vec(100, 205, 31.75)
	local from = vec(head.x, head.y - 1, head.z)
	return Deagle:ApplyHit({ entity = e, pos = vec(head.x, head.y - 0.1, head.z), normal = vec(0, -1, 0), partid = 1, dist = 1 },
		vec(0, 1, 0), Deagle:ShotPhysics(), { pos = from, vel = vec(0, 350, 0), dist = 1 })
end

for _, gun in ipairs({ "glock_17", "deagle" }) do
	local e = newNpc()
	headShot(e, gun)
	check(string.format("%s into a bare head from 1 m: dead (%.0f of 100 HP taken)", gun, e.damage), e.damage >= 100, e.damage)
end

-- a helmet that stops the round: a bruise, not a kill
local real = Deagle.ArmorHit
Deagle.ArmorHit = function(self, e, hit, dir, phys, r, part) return { dmg = 3, stam = 10, stopped = { a = { name = "helmet", layer = "plate" } }, left = 0 } end
local e = newNpc()
headShot(e, "glock_17")
check(string.format("a helmet stops it: %.0f HP, alive", e.damage), e.damage > 0 and e.damage < 100, e.damage)
-- through padding with too little left to perforate bone (9 mm: 26 J): x headMult only
Deagle.ArmorHit = function(self, e, hit, dir, phys, r, part) return { dmg = 4, stam = 10, left = 15, padded = true } end
e = newNpc()
headShot(e, "glock_17")
check(string.format("15 J into the head (under the 26 J bone threshold): %.0f HP, alive", e.damage), e.damage > 0 and e.damage < 100, e.damage)
Deagle.ArmorHit = real

-- a chicken: one Boid entity of a flock (KCD2 Scripts/Entities/Boids/Boid.lua)
local boidHits = {}
_G.Boids = { OnBoidHit = function(flock, boid, hit) boidHits[#boidHits + 1] = { flock = flock, boid = boid, hit = hit } end }
local flock = { id = "flock" }
local hen = { class = "Boid", flock_entity = flock, id = "hen" }
function hen:GetName() return "hen" end
local pushed = false
function hen:AddImpulse() pushed = true end
Deagle:UseGun("deagle")
local r = { pos = vec(100, 200, 30), vel = vec(0, 440, 0), dist = 5 }
local on = Deagle:ApplyHit({ entity = hen, pos = vec(100, 205, 30.2), normal = vec(0, -1, 0), partid = 0, dist = 5 }, vec(0, 1, 0), Deagle:ShotPhysics(), r)
local b = boidHits[1]
check(string.format("a chicken dies through Boids.OnBoidHit (%d call, push %.1f along the shot), flies on (%s, skips it: %s)",
	#boidHits, b and b.hit.damage or -1, tostring(on), tostring(r.skip)),
	#boidHits == 1 and b.flock == flock and b.boid == hen and b.hit.dir.y == 1 and b.hit.damage > 1 and on == true and r.skip == "hen" and not pushed, #boidHits)
Deagle:ApplyHit({ entity = hen, pos = vec(100, 205, 30.2), normal = vec(0, -1, 0), partid = 0, dist = 5 }, vec(0, 1, 0), Deagle:ShotPhysics())
check("a second round into the same chicken doesn't kill it again", #boidHits == 1, #boidHits)

-- a save loaded with the gun's kicks still fresh, and the game clock set back 5 minutes (in game 2026-10-14: every
-- shot after it left along NaN and hit nothing): the next shot flies a real direction
local launched = {}
local launch = Deagle.LaunchRound
Deagle.LaunchRound = function(self, pos, dir, phys, trail) launched[#launched + 1] = dir end
Deagle:UseGun("glock_17")
Deagle.state.drawn, Deagle.state.ammo, Deagle.state.lastShot = true, 17, -100
M.t = 15073
Deagle:Shoot(true); M.advance(0.05, 60); Deagle:Shoot(true)
M.t = 14751
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
Deagle.state.drawn, Deagle.state.ammo = true, 17
launched = {}
Deagle:Shoot(true)
local d = launched[1]
local finite = d and d.x == d.x and d.y == d.y and d.z == d.z and math.abs(d.x) <= 1 and math.abs(d.y) <= 1 and math.abs(d.z) <= 1
check(string.format("after a load that sets the clock back 322 s, a shot flies a real direction (%s)",
	d and string.format("%.3f %.3f %.3f", d.x, d.y, d.z) or "none"), finite, d and d.x)
Deagle.gunKicks = { { t = M.t + 300, pitch = 5, yaw = 1 } }
local kp, ky = Deagle:GunKickNow()
check(string.format("a kick from ahead of the clock is dropped (%s, %s)", tostring(kp), tostring(ky)), kp == 0 and ky == 0, kp)
Deagle.LaunchRound = launch

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("hits: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
