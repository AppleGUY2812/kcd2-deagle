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
	local e = { soul = {}, actor = {}, human = {}, class = "NPC", damage = 0, id = "npc", this = { id = "npcWUID" } }
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

-- who the NPC thinks shot it (cfg.hitAI): "missile" = the game's arrow path, a hitReaction message of type Bullet (6)
-- from the player; the damage names no attacker and the plugin's flinch drops its AI stimulus (0x20). "direct" = the
-- player named on the damage, no message
local pl = Deagle:Player(); pl.this = pl.this or { id = "playerWUID" }
local function aiShot(mode)
	Deagle.cfg.hitAI = mode
	M.aiMessages = {}
	local e = newNpc(); e.this = { id = "npcWUID" }
	local by = "none"
	function e.soul:DealDamage(st, hp, attacker) e.damage = e.damage + hp; by = attacker end
	local n0 = #M.native
	Deagle:UseGun("glock_17")
	Deagle:ApplyHit({ entity = e, pos = vec(100, 205, 31.35), normal = vec(0, -1, 0), partid = 1, dist = 5 }, vec(0, 1, 0), Deagle:ShotPhysics())
	local flags
	for i = n0 + 1, #M.native do flags = M.native[i]:match("^%[DN%] hitreact .* (%x+)$") or flags end
	local react
	for _, m in ipairs(M.aiMessages) do if m.kind == "hitReaction" then react = react or m end end
	return by, flags, react, e
end
local by, flags, msg = aiShot("missile")
check(string.format("hitAI missile: a hitReaction message to the NPC (type %s, attacker %s, strength %s), damage names %s, flinch flags %s",
	tostring(msg and msg.msg.hitType), tostring(msg and msg.msg.attacker), tostring(msg and msg.msg.hitStrength), tostring(by), tostring(flags)),
	msg and msg.id == "npcWUID" and msg.kind == "hitReaction" and msg.msg.__type == "hitReaction" and msg.msg.hitType == 6
		and msg.msg.attacker == pl.this.id and msg.msg.hitStrength >= 5 and by == nil and flags == "1f", flags)
by, flags, msg = aiShot("direct")
check(string.format("hitAI direct: no message (%s), damage names the player (%s), flinch flags %s", tostring(msg ~= nil), tostring(by), tostring(flags)),
	msg == nil and by == pl.id and flags == "3f", flags)
Deagle.cfg.hitAI = "missile"

-- a living NPC says its "got hit" bark (KCD2's own, by metarole), once per PAIN_GAP; not a test dummy
local function barks()
	local n = 0
	for _, m in ipairs(M.aiMessages) do if m.kind == "dialog:monologRequest" and m.msg.metarole == "SKIRMISH_HITTARGET_TARGET" and m.id == "npcWUID" then n = n + 1 end end
	return n
end
Deagle.painT = nil
local _, _, _, npc = aiShot("missile")
local first = barks()
Deagle:ApplyHit({ entity = npc, pos = vec(100, 205, 31.35), normal = vec(0, -1, 0), partid = 1, dist = 5 }, vec(0, 1, 0), Deagle:ShotPhysics())
check(string.format("pain voice: the hit NPC is asked for its got-hit bark (%d), not again 0 s later (%d)", first, barks()), first == 1 and barks() == 1, barks())
Deagle.painT = nil
Deagle.testNpcs = { [tostring(npc.id)] = true }
M.aiMessages = {}
Deagle:ApplyHit({ entity = npc, pos = vec(100, 205, 31.35), normal = vec(0, -1, 0), partid = 1, dist = 5 }, vec(0, 1, 0), Deagle:ShotPhysics())
check("pain voice: a deagle_testnpc dummy says nothing", barks() == 0, barks())
Deagle.testNpcs = nil

-- KCD2's own XP: marksmanship 1 a hit, 18 more for the kill (rpg_param.xml)
M.xp = {}
local victim = newNpc()
Deagle:UseGun("glock_17")
Deagle:ApplyHit({ entity = victim, pos = vec(100, 205, 31.35), normal = vec(0, -1, 0), partid = 1, dist = 5 }, vec(0, 1, 0), Deagle:ShotPhysics())
local afterHit = M.xp.marksmanship or 0
headShot(victim, "glock_17")
check(string.format("marksmanship XP: %d for a hit, %d after the killing head shot", afterHit, M.xp.marksmanship or 0),
	afterHit == 1 and (M.xp.marksmanship or 0) == 1 + 19, M.xp.marksmanship)
-- the chicken's kill names the shooter (KCD2's animal-kill crime checks the killer)
check("a chicken's hit names the shooter", boidHits[1] and boidHits[1].hit.shooterId == Deagle:Player().id, boidHits[1] and boidHits[1].hit.shooterId)
-- near misses: a round flying past an NPC within 2.5 m of its chest sends KCD2's arrow fly-by message, once; not to one
-- it hit, nor to one 4 m away
local near, far, hitOne = newNpc(), newNpc(), newNpc()
near.id, near.this = "near", { id = "nearWUID" }; far.id, far.this = "far", { id = "farWUID" }; hitOne.id, hitOne.this = "hit", { id = "hitWUID" }
function near:GetWorldPos() return vec(101.5, 150, 30) end   -- chest 1.5 m right of a round along y at z 31.25
function far:GetWorldPos() return vec(104, 150, 30) end
function hitOne:GetWorldPos() return vec(100, 160, 30) end
local getEnts = System.GetEntitiesInSphere
System.GetEntitiesInSphere = function() return { near, far, hitOne, Deagle:Player() } end
M.aiMessages = {}
local round = { pos = vec(100, 140, 31.25), hitIds = { hit = true } }
local from = vec(100, 140, 31.25); round.pos = vec(100, 146, 31.25); Deagle:FlyBy(round, from)
from = round.pos; round.pos = vec(100, 156, 31.25); Deagle:FlyBy(round, from)
from = round.pos; round.pos = vec(100, 166, 31.25); Deagle:FlyBy(round, from)
System.GetEntitiesInSphere = getEnts
local to = {}
for _, m in ipairs(M.aiMessages) do if m.kind == "weapon:onPlayerArrowFlyingBy" then to[#to + 1] = m.id end end
check(string.format("near miss: the fly-by message to %s (only the NPC 1.5 m off, once)", table.concat(to, ",")), #to == 1 and to[1] == "nearWUID", #to)

check("no script errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("hits: %d/%d checks pass", n - fails, n))
os.exit(fails == 0 and 0 or 1)
