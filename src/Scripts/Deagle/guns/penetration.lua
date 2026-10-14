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
--      surface, it ricochets with chance = ammo RicochetChance * RicochetChanceMultiplier. NOT USED HERE: 30 deg for
--      every surface, at 10-30 %, is far from real ricochets (below: each material's critical angle).
--   4. A stopped round spalls with the surface's SpallingChance (ammo SpallingDamage / SpallingRadius cm).
-- Budgets used here: 9 mm = 10 cm (RoN's 9 mm guns: MP5 / M92 10). .50 AE = 30 cm: GUESS between .45 (8) and .357
-- Python (55), since RoN has no .50 AE gun.
-- The speed and damage kept after a wallbang are C++ (SpeedLossMultiplierPerSurface is not set). GUESS: keep
-- 1 - depth / maxDepth of the energy.
-- maxDepth below is in metres: { mm9 = 9 mm FMJ, ae50 = .50 AE FMJ }; 0 = stopped (armour level, or not penetrable).
Deagle.penetrationAmmo = {
	["9x19"] = { level = 2, budget = 0.10, spallDamage = 10, spallRadius = 0.6, jhpLevel = 0 },
	["50ae"] = { level = 3, budget = 0.30, spallDamage = 15, spallRadius = 0.6, jhpLevel = 0 },
}

-- Ricochet: crit = the material's critical angle (deg between the path and the surface): hit flatter and the round
-- skips off, steeper and it digs in or breaks up; at crit itself half the rounds skip (Haag & Haag, Shooting Incident
-- Reconstruction, the definition the studies below use). Measured with pistol / rifle FMJ:
--   steel plate: a 9 mm FMJ still skips whole off 1/2" A36 plate at 15, a JHP breaks up from 14, both from 25 (Diaczuk,
--   CUNY PhD thesis 2014, "... 9 mm Parabellum Bullet Ricochet from Common Substrates", Table 19), .45 ACP off 1/8"
--   steel 30-35 (Jauhari, J Crim Law Criminol 1969); one review table gives 5 (CZ 75, ResearchGate 393603576): 15
--   used; sand: a 9 mm skips at 13, 3-4 of 10 at 14, none at 15 (Diaczuk 2014, Tables 7-9); wallboard, car-body steel, wood, glass 5-9
--   (9 mm / 5.56 / 7.62 together, AAFS 2018 B99); 1 mm sheet steel: 9 mm FMJ 15.9 (pistol, 250 m/s) and 15 (SMG,
--   375 m/s), 5.56 breaks up from 5 (Nishshanka et al., Science & Justice 2023); float glass 15.8 for 9 mm FMJ (.32 Auto 21, .45 Auto 17.6: JFS 2016,
--   PubMed 27644019); water ~7 (AK, same), 6.5 (9 mm, Uzi: Gold & Schecter, JFS 1992) and ~8 (AAFS 2012 A102); concrete 10.8-11.1, cement 13.2
--   (7.62x39, Science & Justice 2021); glazed tile 14.8 floor / 16.6 wall (9 mm, JFS 2023, PubMed 36453554); plywood,
--   MDF, melamine 14-18 (9 mm, JFS 2025); whitewood: skips at 9-12, never from 13 (9 mm, CUNY thesis); harder, denser
--   wood skips at steeper angles (same). 0 = never skips (soft: cloth, straw, leaves, flesh). The bullet matters too
--   (on glass .32 Auto 21, 9 mm 15.8, .45 Auto 17.6; on wood .32 above 9 mm), not simply by size: not modelled, one
--   angle per material. No handgun study of soil found: packed ground (road, gravel, field) and soft ground (forest
--   floor, grass, mud: GUESS skips only flatter, and less steeply) are guesses (the only soil data: cal .50 AP off sod /
--   soil, critical ~8, 7 -> 10.3 out: Hitchcock, BRL Report 629, 1947).
-- hard = unyielding (steel, stone, tile): a skipping round leaves flatter than it came (cfg.ricochetLift); the others
-- yield and leave steeper (lift: the round ploughs a little ramp; soil and sand GUESS). Water: see below the table.
-- family = effect family in deagle_impacts.xml; ron = RoN surface the values come from; armour = RoN ArmourLevel;
-- spall = SpallingChance.
local function P(family, ron, density, armour, penetrable, crit, hard, spall, lift)
	local e = { family = family, ron = ron, density = density, armour = armour, penetrable = penetrable,
		crit = crit, hard = hard, lift = lift, spall = spall, maxDepth = {} }
	for cal, a in pairs(Deagle.penetrationAmmo) do
		local key = cal == "9x19" and "mm9" or "ae50"
		e.maxDepth[key] = (penetrable and a.level >= armour) and a.budget / density or 0
	end
	return e
end

Deagle.penetration = {
	-- families (RoN source surface)                                           crit hard  spall lift
	metal      = P("metal",   "RON_Steel",          8.0,  4, true,  15,  true,  0.2),        -- solid steel: stops both; 1/2" plate 14-25
	metal_thin = P("metal",   "RON_Galvanized",     2.0,  3, true,  15,  true,  0.7, 1),     -- sheet / plate armour: .50 AE through; 1 mm sheet 15-16 (9 mm)
	stone      = P("stone",   "RON_ConcreteStrong", 5.5,  3, true,  12,  true,  0.3),        -- masonry, castle stone; concrete 11-13
	stone_soft = P("stone",   "RON_ConcreteSoft",   4.25, 2, true,  12,  true,  0.0),        -- concrete 11-13
	brick      = P("stone",   "RON_Brick",          1.95, 0, true,  12,  true,  0.0),        -- GUESS: as concrete
	ceramic    = P("stone",   "RON_Ceramic",        1.0,  0, true,  16,  true,  0.0),        -- tile 15-17; density = default (GUESS 1)
	plaster    = P("plaster", "RON_Plaster",        0.8,  0, true,  7,   false, 0.0),        -- wallboard 5-9
	drywall    = P("plaster", "RON_Drywall",        0.6,  0, true,  7,   false, 0.0),        -- wallboard 5-9
	wood       = P("wood",    "RON_Plywood",        1.0,  0, true,  16,  false, 0.0),        -- plywood 14-18; density = default (GUESS 1)
	wood_soft  = P("wood",    "RON_Pine",           1.0,  0, true,  11,  false, 0.0),        -- whitewood 9-12; density = default (GUESS 1)
	soil       = P("soil",    "RON_Dirt",           1.0,  0, false, 14,  false, 0.0, 0.78),   -- packed ground, not penetrable: as sand (below)
	soil_soft  = P("soil",    "RON_Dirt",           1.0,  0, false, 5,   false, 0.0, 1.5),   -- forest floor, grass, mud; GUESS
	sand       = P("sand",    "RON_Sand",           1.0,  0, false, 14,  false, 0.0, 0.78),  -- 9 mm 13-14; leaves at 2.8 + 0.78x (below)
	foliage    = P("soil",    "RON_Leaves",         0.7,  0, true,  0,   false, 0.0),
	cloth      = P("cloth",   "RON_Fabric_Thin",    0.5,  0, true,  0,   false, 0.0),
	sack       = P("cloth",   "RON_Fabric_Stuffing", 1.0, 0, true,  0,   false, 0.0),        -- density = default (GUESS 1)
	straw      = P("straw",   "RON_Cardboard",      0.5,  0, true,  0,   false, 0.0),        -- no RoN straw: GUESS cardboard
	glass      = P("glass",   "RON_Glass_Plate",    0.6,  0, true,  16,  false, 0.0),        -- float glass 15.8 (9 mm)
	water      = P("water",   "RON_Water",          1.0,  0, true,  7,   false, 0.0),        -- water 6.5-8; density = default (GUESS 1)
	flesh      = P("flesh",   "RON_Flesh",          3.0,  0, true,  0,   false, 0.0),        -- 9 mm: 3.3 cm, .50 AE: 10 cm
	chainmail  = P("metal",   "RON_Tin",            1.0,  0, true,  6,   true,  0.0),        -- GUESS: rings stop little; crit GUESS (no study of mail)
}
-- Off water a round leaves at ~10 deg whatever it came in at (9 mm FMJ: 2.5 -> 9.5, 5 -> 12, 8.5 -> 9.5; .45 ACP
-- 2.5 -> 8: American Handgunner, "The Art Of The Ricochet") with ~10 % of its energy (pointed and round-nosed bullets
-- "give up as much as 90 %" and come off tumbling: Bookstaber, "Shooting Bullets Into Water II"; destabilised flight:
-- AAFS 2010 A199). exit = deg off the surface, keep = energy kept (others: cfg.ricochetKeep)
Deagle.penetration.water.exit, Deagle.penetration.water.keep = 10, 0.1
-- Off sheet steel a 9 mm leaves at ~6 deg whatever it came in at (5-15 in: 4.8-7.6 out, mean 6), never steeper than it
-- came (Science & Justice 2023, above): lift 1 (in the table) up to exitMax.
Deagle.penetration.metal_thin.exitMax = 6
-- Off concrete, marble and 1/2" steel plate a 9 mm leaves at about half its angle (5 -> 2.7-3.1, 10 -> 4.1-5.1,
-- 15 -> 6.0-6.9 deg; least squares 0.46: Diaczuk 2014, Tables 19, 21, 23); brick as concrete. Off glazed tile at
-- ~1.4 deg whatever it came in at (5-15 in: 0.1-2.6 out, Nishshanka et al., JFS 2023, Tables 2-3). Off glass at 3/4 of
-- it (laminated windshield 5 -> 3, 10 -> 8: Diaczuk & Goldfrank, AAFS 2017 B39; off float glass always flatter than it
-- came: JFS 2016). Off cement board steeper, 1.2x (7 -> 8.3-9.6, 10 -> 11.1-12.7: Daly, John Jay MS thesis 2021), the
-- nearest measured board, for plaster and drywall. Off sand at 2.8 + 0.78x (4 -> 6.0-6.5, 7 -> 7.9-8.0,
-- 13 -> 12.0-13.1, 14 -> 14.3: Diaczuk 2014, Tables 7-9).
for _, k in ipairs({ "metal", "stone", "stone_soft", "brick" }) do Deagle.penetration[k].lift = 0.46 end
Deagle.penetration.ceramic.exit = 1.4
Deagle.penetration.glass.lift = 0.75
Deagle.penetration.plaster.lift, Deagle.penetration.drywall.lift = 1.2, 1.2
Deagle.penetration.sand.exitAdd = 2.8
-- Packed ground (road, gravel, field, soil) as sand, the nearest measured ground (GUESS that it carries over; no
-- handgun study of soil found). Soft ground (forest floor, grass, mud) stays a guess.
Deagle.penetration.soil.exitAdd = 2.8
-- Energy kept on a skip (keep; others cfg.ricochetKeep, a GUESS): pistol bullets off sheet steel up to 30 deg stay
-- whole and keep ~75 % (Houlden, J Forensic Sci Soc 1994, "Distribution of energy among fragments of ricocheting pistol
-- bullets"; a .22 LR off a steel plate kept 0.85 of its speed, 72 %: Kolomiitsev et al., Theory and Practice of
-- Forensic Science 2022, medians of 10-12 shots under 30 deg). Off concrete and brick the .22 LR kept 0.78-0.79 of its
-- speed, ~60 % of its energy (same); off concrete it falls with the angle, ~30 % at 45 deg (Houlden). Tile as the
-- concrete under it (GUESS). Off soft pine a .32 lost 26 % of its speed and a .45 51 % (Rathman 1987): energy 0.55 and
-- 0.24, ~0.4 between them.
for _, k in ipairs({ "metal", "metal_thin", "chainmail" }) do Deagle.penetration[k].keep = 0.75 end
for _, k in ipairs({ "stone", "stone_soft", "brick", "ceramic" }) do Deagle.penetration[k].keep = 0.6 end
Deagle.penetration.wood.keep, Deagle.penetration.wood_soft.keep = 0.4, 0.4
-- worn armour layers (guns/armor.lua) by layer: their ricochet data
Deagle.armorRicochet = { plate = Deagle.penetration.metal_thin, mail = Deagle.penetration.chainmail }

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
	{ S.soil, "mat_soil", "mat_soil_unwalk", "mat_gravel", "mat_road", "mat_field" },
	{ S.soil_soft, "mat_forest", "mat_grass", "mat_grassdry", "mat_mud" },
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
