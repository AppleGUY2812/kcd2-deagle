-- Wallbang / ricochet / spall data per surface family and KCD2 surface, from Ready or Not's numbers
-- (docs/ron/ron_ballistics.json: Info/PenetrationData, AmmoDataTable, weapon CDOs, Projectile_Base).
--
-- What RoN stores (read directly):
--   per surface (MaterialPenetration): bIsPenetrable, PenetrationDensity, ArmourLevel, bCanRicochet,
--     RicochetChanceMultiplier, SpallingChance. Fields a surface doesn't set are C++ defaults that weren't readable.
--     From the data the defaults look like: bIsPenetrable = false (every penetrable surface sets it, and dirt / sand /
--     grass don't), bCanRicochet = true, ArmourLevel = 0, SpallingChance = 0. GUESS: density 1, ricochet multiplier 1.
--   per round (AmmoDataTable): PenetrationLevel (9x19 FMJ 2, .50 AE FMJ 3, JHP 0), PenetrationDistance (9x19 FMJ 250,
--     .50 AE FMJ 300), RicochetChance (9x19 FMJ 0.1, .50 AE 0).
--   per weapon (BaseMagazineWeapon.PenetrationDistance, cm): pistols 2.5-14 (M9 5, M92 10, USP 8, 1911 2.5-8),
--     MP5 10, M4 25, M14 120, Python .357 55.
--   projectile: RequiredAngleToDeflect 30 (deg), PercentageToDeflect 1.
-- How they combine (native code, NOT read; inference from names and ranges):
--   1. A round gets into a surface only if bIsPenetrable and ammo PenetrationLevel >= surface ArmourLevel.
--   2. It then spends a budget: depth_cm * PenetrationDensity, up to the weapon's PenetrationDistance (cm). So the
--      thickest wall it gets through is maxDepth = budget / density. The ammo row's PenetrationDistance
--      (100-750) looks like the distance the round still flies afterwards, not a thickness.
--   3. Otherwise it stops. If the surface bCanRicochet and the shot hits within RequiredAngleToDeflect (30 deg) of the
--      surface, it ricochets with chance = ammo RicochetChance * RicochetChanceMultiplier.
--   4. A stopped round spalls with the surface's SpallingChance (ammo SpallingDamage / SpallingRadius cm).
-- Budgets used here: 9 mm = 10 cm (RoN's 9 mm guns: MP5 / M92 10). .50 AE = 30 cm: GUESS between .45 (8) and .357
-- Python (55), since RoN has no .50 AE gun.
-- The speed and damage kept after a wallbang are C++ (SpeedLossMultiplierPerSurface is not set). GUESS: keep
-- 1 - depth / maxDepth of the energy.
-- maxDepth below is in metres: { mm9 = 9 mm FMJ, ae50 = .50 AE FMJ }; 0 = stopped (armour level, or not penetrable).
Deagle.penetrationAmmo = {
	["9x19"] = { level = 2, budget = 0.10, ricochetChance = 0.1, spallDamage = 10, spallRadius = 0.6, jhpLevel = 0 },
	["50ae"] = { level = 3, budget = 0.30, ricochetChance = 0.0, spallDamage = 15, spallRadius = 0.6, jhpLevel = 0 },
}
Deagle.ricochetMaxAngle = 30  -- deg between the shot and the surface (Projectile_Base RequiredAngleToDeflect)

-- family = effect family in deagle_impacts.xml; ron = RoN surface the values come from; armour = RoN ArmourLevel;
-- ricochetChance = 9 mm FMJ chance (0.1 x RicochetChanceMultiplier; the .50 AE is 0 in RoN); spall = SpallingChance.
local function P(family, ron, density, armour, penetrable, canRic, ricMult, spall)
	local e = { family = family, ron = ron, density = density, armour = armour, penetrable = penetrable,
		ricochetChance = canRic and 0.1 * ricMult or 0, ricochetMaxAngle = canRic and 30 or 0, spall = spall, maxDepth = {} }
	for cal, a in pairs(Deagle.penetrationAmmo) do
		local key = cal == "9x19" and "mm9" or "ae50"
		e.maxDepth[key] = (penetrable and a.level >= armour) and a.budget / density or 0
	end
	return e
end

Deagle.penetration = {
	-- families (RoN source surface)
	metal      = P("metal",   "RON_Steel",          8.0,  4, true,  true,  1.0,  0.2),   -- solid steel: stops both
	metal_thin = P("metal",   "RON_Galvanized",     2.0,  3, true,  true,  1.25, 0.7),   -- sheet / plate armour: .50 AE through
	stone      = P("stone",   "RON_ConcreteStrong", 5.5,  3, true,  true,  1.0,  0.3),   -- masonry, castle stone
	stone_soft = P("stone",   "RON_ConcreteSoft",   4.25, 2, true,  true,  1.0,  0.0),
	brick      = P("stone",   "RON_Brick",          1.95, 0, true,  true,  1.0,  0.0),
	ceramic    = P("stone",   "RON_Ceramic",        1.0,  0, true,  true,  1.0,  0.0),   -- density = default (GUESS 1)
	plaster    = P("plaster", "RON_Plaster",        0.8,  0, true,  true,  1.0,  0.0),
	drywall    = P("plaster", "RON_Drywall",        0.6,  0, true,  false, 1.0,  0.0),
	wood       = P("wood",    "RON_Plywood",        1.0,  0, true,  false, 1.0,  0.0),   -- density = default (GUESS 1)
	wood_soft  = P("wood",    "RON_Pine",           1.0,  0, true,  true,  1.0,  0.0),   -- density = default (GUESS 1)
	soil       = P("soil",    "RON_Dirt",           1.0,  0, false, false, 1.0,  0.0),   -- not penetrable (ground)
	sand       = P("sand",    "RON_Sand",           1.0,  0, false, false, 1.0,  0.0),
	foliage    = P("soil",    "RON_Leaves",         0.7,  0, true,  false, 1.0,  0.0),
	cloth      = P("cloth",   "RON_Fabric_Thin",    0.5,  0, true,  false, 1.0,  0.0),
	sack       = P("cloth",   "RON_Fabric_Stuffing", 1.0, 0, true,  true,  1.0,  0.0),   -- density = default (GUESS 1)
	straw      = P("straw",   "RON_Cardboard",      0.5,  0, true,  false, 1.0,  0.0),   -- no RoN straw: GUESS cardboard
	glass      = P("glass",   "RON_Glass_Plate",    0.6,  0, true,  false, 1.0,  0.0),
	water      = P("water",   "RON_Water",          1.0,  0, true,  false, 1.0,  0.0),   -- density = default (GUESS 1)
	flesh      = P("flesh",   "RON_Flesh",          3.0,  0, true,  false, 1.0,  0.0),   -- 9 mm: 3.3 cm, .50 AE: 10 cm
	chainmail  = P("metal",   "RON_Tin",            1.0,  0, true,  true,  1.0,  0.0),   -- GUESS: rings stop little
}

-- KCD2 surfaces (docs/kcd2_impact_fx.md section 1) -> entries above
local S = Deagle.penetration
for _, m in ipairs({
	{ S.metal, "mat_metal", "mat_bells" },
	{ S.metal_thin, "mat_door_metal", "mat_plate", "mat_fabric_plate", "mat_chainmail_plate", "mat_fabric_chainmail_plate" },
	{ S.chainmail, "mat_chainmail", "mat_fabric_chainmail", "mat_leather_chainmail" },
	{ S.stone, "mat_rock", "mat_rock_unwalk", "mat_rock_horse_ignore", "mat_stairs_stone", "mat_rockwater" },
	{ S.ceramic, "mat_ceramics" },
	{ S.plaster, "mat_plaster" },
	{ S.wood, "mat_wood", "mat_wood_unwalk", "mat_door_wood", "mat_shield_wood", "mat_stairs_wood" },
	{ S.wood_soft, "mat_wood_soft", "mat_wicker" },
	{ S.soil, "mat_soil", "mat_soil_unwalk", "mat_mud", "mat_gravel", "mat_road", "mat_field", "mat_forest", "mat_grass", "mat_grassdry" },
	{ S.foliage, "mat_grass_tall", "mat_bushes", "mat_vegetation", "mat_vegetation_seethrough", "mat_canopy",
		"mat_railing_wood_seethrough", "mat_metal_seethrough" },   -- walk-blocking hulls rounds fly through (deagle_main SURFACES)
	{ S.water, "mat_water", "mat_mudwater", "mat_slowwater", "mat_puddle" },
	{ S.straw, "mat_thatch", "mat_organic_stuff" },
	{ S.sack, "mat_sack", "mat_fabric_items" },
	{ S.cloth, "mat_fabric", "mat_leather" },
	{ S.glass, "mat_glass", "mat_window" },
	{ S.flesh, "mat_flesh" },
	{ S.stone, "mat_default", "mat_invulnerable" },        -- untyped / indestructible geometry: KCD2's is mostly masonry
	{ S.metal, "mat_super_cool_sparkly_type" },
}) do
	for i = 2, #m do S[m[i]] = m[1] end
end
