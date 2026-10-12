-- Cartridges and real guns: what a shot does (recoil push, muzzle flip, flash, bullet energy) is computed from these
-- in Deagle:ShotPhysics instead of being tuned per gun (BALLISTICS.md, "Cartridge-driven handling").
--
-- Cartridges, typical factory loads: bullet kg, muzzle velocity m/s from the reference barrel (m), powder charge kg.
--   .50 AE 300 gr: ~1,400-1,500 fps from a 6" Desert Eagle (Steinel, CCI), 28-34 gr of powder (Speer 300 gr .50 AE data)
--   9x19 124 gr:   ~1,150 fps from a 4" barrel (Federal American Eagle), ~4.4 gr (Hodgdon Titegroup, 124 gr RN)
Deagle.calibers = {
	["50ae"] = { name = ".50 AE", bullet = 0.01944, velocity = 442, powder = 0.00201, barrel = 0.152 },
	["9x19"] = { name = "9x19mm", bullet = 0.00804, velocity = 350, powder = 0.000285, barrel = 0.102 },
}

-- Real guns by their CS2 counterpart (a gun's own `physics` table overrides): loaded mass kg, barrel m, bore height =
-- how far the barrel's axis sits above the web of the shooting hand (m, the lever that flips the muzzle; approximate),
-- hold = "pistol" (two hands on the grip) / "rifle" (stock in the shoulder).
Deagle.gunPhysics = {
	weapon_deagle = { caliber = "50ae", mass = 2.05, barrel = 0.152, bore = 0.050, hold = "pistol" },  -- Mark XIX 6", 1,998 g empty
	weapon_glock = { caliber = "9x19", mass = 0.905, barrel = 0.114, bore = 0.030, hold = "pistol" },  -- Glock 17, low bore axis
}
