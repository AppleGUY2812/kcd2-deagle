-- Cartridge physics: recoil impulse, muzzle gas and blast (ShotPhysics), wound energy (Wound).
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U

-- What one shot does, from the gun's cartridge and build (SI units; docs/handling-physics-design.md):
-- - recoil impulse = bullet momentum + powder gas leaving ~1.5x the bullet's speed from a pistol, 1.75x from a rifle
--   (SAAMI free-recoil formula); free recoil speed / energy = that over the gun's mass;
-- - muzzle flip = impulse x bore height, turning gun + hand around the wrist;
-- - muzzle gas energy = the powder's heat (~4.5 MJ/kg) minus what went into the bullet: it makes the flash and the bang,
--   and the blast scale G = its cube root against a 9 mm (Hopkinson-Cranz scaling of blasts and fireballs);
-- - air-drag length for the bullet's energy (range loss).
local WRIST_ARM, HAND_INERTIA = 0.10, 0.004    -- m from the wrist to the gun's centre of mass; kg m^2 of the hand
local POWDER_HEAT = 4.5e6                      -- J/kg released by double-base pistol powder (typical value)
local AIR_RHO, AIR_CD = 1.2, 0.37              -- kg/m^3; drag coefficient fitted to factory tables (~8 % speed lost by 50 yd)
local function muzzleGas(cal, v) return cal.powder * POWDER_HEAT - 0.5 * cal.bullet * v * v end
-- One result per gun, made once: it depends only on the gun's and the cartridge's data, and nobody writes into it.
-- A shot asked ~10 times (flash, sound, recoil, hearing, the round...): a new table each, 1.8 ms a frame at 1000
-- rounds a second (deagle_stress 1000 sky prof, 2026-10-17). Keyed by the gun table (weakly), checked against the
-- physics and cartridge tables it came from.
local shotCache = setmetatable({}, { __mode = "k" })
function Deagle:ShotPhysics(g)
	g = g or self.gun
	local ph = g.physics or (g.cs2 and self.gunPhysics and self.gunPhysics[g.cs2])
	local cal = ph and self.calibers and self.calibers[ph.caliber]
	if not cal then return nil end
	local c = shotCache[g]
	if c and c.ph == ph and c.cal == cal then return c.r end
	local r = self:ShotPhysicsOf(ph, cal)
	shotCache[g] = { ph = ph, cal = cal, r = r }
	return r
end
function Deagle:ShotPhysicsOf(ph, cal)
	local v = cal.velocity * (ph.barrel / cal.barrel) ^ 0.25          -- longer barrel, faster bullet (pistol rule of thumb)
	local impulse = cal.bullet * v + cal.powder * (ph.hold == "rifle" and 1.75 or 1.5) * v
	local vr = impulse / ph.mass
	local inertia = ph.mass * WRIST_ARM * WRIST_ARM + HAND_INERTIA
	if ph.hold == "rifle" then inertia = inertia * 4 end               -- a stock in the shoulder: the body takes it
	local ref = self.calibers["9x19"]
	local gas = muzzleGas(cal, v)
	local area = math.pi * (cal.d / 2) ^ 2
	return { caliber = cal.name, bullet = cal.bullet, d = cal.d, velocity = v, energy = 0.5 * cal.bullet * v * v,
		momentum = cal.bullet * v, impulse = impulse, recoilVel = vr, recoilEnergy = 0.5 * ph.mass * vr * vr,
		inertia = inertia, flip = impulse * ph.bore / inertia, gasEnergy = gas,
		blast = (gas / muzzleGas(ref, ref.velocity)) ^ (1 / 3),
		dragLength = 2 * cal.bullet / (AIR_RHO * AIR_CD * area) }
end

-- Wounding from the energy the bullet leaves in the body (crush + stretch; FBI 1989, Fackler): tissue drag slows it
-- along the path, a through-and-through round carries the rest out. Health damage grows with the square root of that
-- energy (wounds grow slower than energy); one anchor: a 9 mm 124 gr FMJ at 360 m/s takes 35 HP. Stamina (the shove
-- of a hit) follows momentum. Phase 2 puts armour layers before this (BALLISTICS.md).
local TISSUE_RHO, TISSUE_CD, BODY_PATH = 1000, 0.3, 0.3   -- kg/m^3, drag in tissue (gelatin fits), torso path m
local function deposited(bullet, d, energy)
	local k = TISSUE_RHO * TISSUE_CD * math.pi * (d / 2) ^ 2 / (2 * bullet)
	return energy * (1 - math.exp(-2 * k * BODY_PATH))
end
-- energy = what reaches the body (after armour); without it, the muzzle energy less air drag over dist. Also returns
-- the energy that comes out the far side (through and through).
function Deagle:Wound(phys, dist, energy)
	local m = self.cfg.physics
	local eIn = energy or phys.energy * math.exp(-2 * (dist or 0) / phys.dragLength)
	local ref = self.calibers["9x19"]
	local eRef = deposited(ref.bullet, ref.d, 0.5 * ref.bullet * 360 * 360)
	local dep = deposited(phys.bullet, phys.d, eIn)
	local health = m.damageRef * math.sqrt(dep / eRef)
	local stamina = m.staminaRef * (math.sqrt(2 * eIn / phys.bullet) * phys.bullet) / (ref.bullet * 360)
	return health, stamina, eIn, eIn - dep
end
