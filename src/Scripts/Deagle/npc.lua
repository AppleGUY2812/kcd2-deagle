-- NPC reactions (Claude 4): XP, near misses (fly-by), AI hit and hearing events.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U
local try = U.try

-- A gun hit as KCD2's behaviour trees take an arrow (cfg.hitAI "missile"): a hitReaction message of type Bullet (6) from
-- the player. In the trees (Scripts.pak AI/npc/basic/switch/handleHitReaction.xml, AI/crime/translateHitReaction.xml,
-- handleStimulusShooter.xml) Bullet is a missile hit: the player's (attack, flee, crime) if the NPC has seen him
-- (HasSeenPlayer), else a search toward the shot; either way the player's last hit is linked and witnesses get the crime
-- hit volume. Damage itself names no attacker (DealDamage's hits are type Fall, which the trees file as "unknown"). The
-- game's own Lua messages a brain this way (Scripts/Entities/AI/Horse.lua OnStartle).
local HIT_BULLET = 6   -- enum HitReactionType.Bullet (Tables.pak Libs/Tables/ai/ai_enums.xml)
-- KCD2's own XP for a ranged hit and kill (Tables.pak rpg_param.xml: SkillXPHit 1, SkillXPKill 18), to marksmanship,
-- the skill of its bows, crossbows and guns; the kill's on top of its hit
local XP_HIT, XP_KILL = 1, 18
function Deagle:ShotXP(killed)
	local p = self:Player()
	try("XP", function() p.soul:AddSkillXP("marksmanship", XP_HIT + (killed and XP_KILL or 0)) end)
end

-- A round passing a living NPC within cfg.nearMiss m of its chest: the message KCD2's engine sends for a player's arrow
-- flying by (weapon:onPlayerArrowFlyingBy {GUID, Position, Direction}; Scripts.pak AI/npc/basic/switch/switch.xml ->
-- handleArrowFlyby -> handleStimulusShooter): it names the player if it has seen him, else looks and searches toward
-- him. Once per NPC per round, never to one the round hit. Called for every flight step (ballistics.lua StepRound).
-- Who is near a step comes from KCD2's physics grid (Physics.SamplePhysEnvironment, living entities, a sphere around
-- the step): ~0 ms at 3 m, 0.12 ms at 30 m (measured live 2026-10-15). Any entity-system query (GetEntitiesInSphere,
-- GetEntitiesByClass) costs 5-9 ms a call whatever its size: per step it dropped the frame rate while rounds flew
-- ("when the bullet falls out of range then it unlags"), and a shared 300 m list every 2 s still hitched the frame of
-- the shot. The check is plain numbers (tables per NPC per step cost 2 ms a frame in town).
local CHEST = 1.25   -- m above an NPC's feet
-- squared distance from (px, py, pz) to the segment from (fx, fy, fz) along (sx, sy, sz), and the closest point on it
local function segDist2(px, py, pz, fx, fy, fz, sx, sy, sz, len2)
	local t = ((px - fx) * sx + (py - fy) * sy + (pz - fz) * sz) / len2
	t = t < 0 and 0 or (t > 1 and 1 or t)
	local cx, cy, cz = fx + sx * t, fy + sy * t, fz + sz * t
	return (px - cx) ^ 2 + (py - cy) ^ 2 + (pz - cz) ^ 2, cx, cy, cz
end
-- One fly-by message per NPC per FLYBY_GAP s: KCD2's arrows come about one a second; a spray past someone sent ~15 a
-- second (Claude 3's audit 2026-10-15). Game time; the table is cleared at a load.
local FLYBY_GAP = 0.75
function Deagle:FlyBy(r, from)
	local near = self.cfg.nearMiss
	if not near or near <= 0 then return end
	local fx, fy, fz = from.x, from.y, from.z
	local sx, sy, sz = r.pos.x - fx, r.pos.y - fy, r.pos.z - fz
	local len2 = sx * sx + sy * sy + sz * sz
	if len2 < 1e-6 then return end
	local len = math.sqrt(len2)
	local ok, found = pcall(Physics.SamplePhysEnvironment, { x = fx + sx / 2, y = fy + sy / 2, z = fz + sz / 2 }, len / 2 + near, ent_living)
	if not ok or type(found) ~= "table" then return end
	local p, near2, now = self:Player(), near * near, System.GetCurrTime()
	if not self.flybyNext or (self.flybyAt or 0) > now then self.flybyNext = {} end   -- first use, or the clock went back (a load)
	self.flybyAt = now
	for i = 1, #found do   -- entities, each followed by two numbers (its part and physics ids)
		local e = found[i]
		if type(e) == "table" and e ~= p and e.soul and e.actor and e.this
			and not (r.flybys and r.flybys[e.id]) and not (r.hitIds and r.hitIds[e.id])
			and now >= (self.flybyNext[e.id] or 0) then
			local okD, dead = pcall(e.actor.IsDead, e.actor)
			local okP, at = pcall(e.GetWorldPos, e)
			if not (okD and dead) and okP and at then
				local d2, cx, cy, cz = segDist2(at.x, at.y, at.z + CHEST, fx, fy, fz, sx, sy, sz, len2)
				if d2 <= near2 then
					r.flybys = r.flybys or {}
					r.flybys[e.id] = true
					self.flybyNext[e.id] = now + FLYBY_GAP
					try("near miss", function()
						local msg = table.MakeFromType("weapon:onPlayerArrowFlyingBy", { GUID = e.this.id,
							Position = { x = cx, y = cy, z = cz }, Direction = { x = sx / len, y = sy / len, z = sz / len } })
						XGenAIModule.SendMessageToEntityData(e.this.id, "weapon:onPlayerArrowFlyingBy", msg)
					end)
				end
			end
		end
	end
end

function Deagle:HitAI(e, dmg, stam)
	if self.cfg.hitAI ~= "missile" then return end
	local p = self:Player()
	try("hit AI", function()
		local msg = table.MakeFromType("hitReaction", { attacker = p.this.id, hitType = HIT_BULLET, hitStrength = self:HitStrength(dmg, stam) })
		XGenAIModule.SendMessageToEntityData(e.this.id, "hitReaction", msg)
	end)
end

-- KCD2's AI hears the shot: its own gun_fired sound (rpg_sound.xml, the loudest a person makes; guards look around,
-- come to investigate, panic up close; docs/kcd2_ai_hearing.md), on the shooter, louder for a louder gun (x the blast
-- loudness over a 9 mm's, the same blast level the gunshot plays at)
function Deagle:ShotHeard(phys)
	local h = self.cfg.hearing
	if not h.on then return end
	local mult = h.shot
	if phys then
		local ref = self:ShotPhysics({ physics = { caliber = "9x19", mass = 0.9, barrel = 0.102, bore = 0.03, hold = "pistol" } })
		mult = mult * 10 ^ ((20 / 3) * math.log10(phys.gasEnergy / ref.gasEnergy) / 20)
	end
	try("AI hears the shot", function() XGenAIModule.ProduceSoundWUID(enum_sound.gun_fired, self:Player().this.id, mult) end)
end
