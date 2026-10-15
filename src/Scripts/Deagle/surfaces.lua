-- What each KCD2 surface does when a round hits it: its effect (Claude 2), hole (Claude 4) and sound (Claude 3)
-- columns, and surfaceName.
-- Split out of deagle_main.lua (2026-10-15), which loads it (Deagle:LoadModule); its helpers come from Deagle.U.
local U = Deagle.U

-- ---------------------------------------------------------------------------
-- hits
-- ---------------------------------------------------------------------------
-- What a round does on each of KCD2's 74 surface types (Libs/MaterialEffects/SurfaceTypes.xml): our impact effect
-- (deagle_impacts.<family>, built from Ready or Not's, docs/impact_fx_build.md) and bullet hole
-- (Materials/deagle/decals/bullet_hole_<hole>), or "pass": invisible helpers (window and map barriers, player / NPC /
-- horse-only colliders, designer blockers) and foliage, which a round flies through. Worn armour (plate / mail combos)
-- sparks like metal and gets no hole (decals don't follow a moving body).
local SURFACES = {}
local function surfaces(fx, hole, names, snd) for _, n in ipairs(names) do SURFACES[n] = { fx = fx, hole = hole, snd = snd or fx } end end
surfaces("metal", "metal", { "mat_metal", "mat_super_cool_sparkly_type" })
surfaces("metal", "metal", { "mat_door_metal" }, "metal_thin")
surfaces("metal", "metal", { "mat_bells" }, "bell")
surfaces("metal", nil, { "mat_plate", "mat_chainmail", "mat_chainmail_plate", "mat_fabric_plate", "mat_fabric_chainmail",
	"mat_fabric_chainmail_plate", "mat_leather_chainmail" }, "armor")
surfaces("stone", "stone", { "mat_rock", "mat_rock_unwalk", "mat_rock_horse_ignore", "mat_stairs_stone", "mat_default", "mat_invulnerable" })
surfaces("stone", "ceramic", { "mat_ceramics" }, "ceramic")
surfaces("plaster", "plaster", { "mat_plaster" })
surfaces("wood", "wood", { "mat_wood", "mat_wood_unwalk", "mat_door_wood", "mat_shield_wood", "mat_stairs_wood" })
surfaces("wood", "wood_soft", { "mat_wood_soft", "mat_wicker" })
surfaces("soil", "soil", { "mat_soil", "mat_soil_unwalk", "mat_mud", "mat_gravel", "mat_road", "mat_field" })
surfaces("soil", "soil", { "mat_forest", "mat_grass", "mat_grassdry" }, "grass")
surfaces("water", nil, { "mat_water", "mat_mudwater", "mat_slowwater", "mat_rockwater", "mat_puddle" })
surfaces("straw", nil, { "mat_thatch", "mat_organic_stuff" })
surfaces("cloth", "cloth", { "mat_sack", "mat_fabric_items", "mat_fabric", "mat_leather" })
surfaces("glass", "glass", { "mat_glass" })
surfaces("blood", nil, { "mat_flesh" }, "flesh")
-- The "see-through" surfaces are crude hulls that block walking, not shots: KCD2 gives them pierceability 11, like its
-- invisible map barriers (SurfaceTypes.xml), and its own arrows pierce anything above 9 (Ammo/Arrow.xml). 402 models
-- use them: most forest trees (a box over the branches; the trunks have their own mat_wood proxy), fences, palisades,
-- scaffolds, ladders, vines. A round flies through them like an arrow does (in game 2026-10-13: rounds stopped in the
-- air between a coppiced beech's trunks).
surfaces("pass", nil, { "mat_railing_wood_seethrough", "mat_metal_seethrough" })
surfaces("pass", nil, { "mat_none", "mat_window", "mat_player_collider", "mat_player_only", "mat_no_photomode", "mat_npc_only",
	"mat_horse_only", "mat_horse_ignore", "mat_player_only_with_bark", "mat_level_barrier", "mat_deep_water_barrier",
	"mat_cart_slowdown", "mat_corpses_barrier", "mat_prepadeni_roadTocamp_barrier", "mat_slope", "mat_barrier_navignore",
	"mat_nodraw_designer", "mat_raycast", "mat_canopy", "mat_vegetation", "mat_vegetation_seethrough",
	"mat_vegetation_horse_ignore", "mat_bushes", "mat_grass_tall" })
Deagle.surfaces = SURFACES

local function surfaceName(hit)
	local name = ""
	pcall(function() name = System.GetSurfaceTypeNameById(hit.surface) or "" end)
	return name
end
Deagle.U.surfaceName = surfaceName

-- for the files loaded after this one
U.SURFACES = SURFACES
