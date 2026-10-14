-- Ready or Not's handling sounds on our viewmodel clips (docs/ron_live_spec.md). RoN starts one full-length event per
-- action on its own animations; ours are other animations with other timings, so each RoN part event (mag out, mag in,
-- slide...) is started early enough that its main click lands on our clip's matching moment.
--
-- Deagle.ronClick: ms from the play command to the event's main click, measured by rendering each event alone in RoN's
-- engine (docs/ron_fmod_events.md "Handling events: transient timings"): (first sound - 11 ms, the start scheduling
-- the MK V's bang also has) + main click. Random multi-take events: the middle of their range.
Deagle.ronClick = {
	["event:/Weapons/Grizzly/Grizzly ReloadStart"] = 457,
	["event:/Weapons/Grizzly/Grizzly Reload Quick Start"] = 82,
	["event:/Weapons/Grizzly/Grizzly Reload Empty Start"] = 294,
	["event:/Weapons/Grizzly/Grizzly MagOut"] = 245,
	["event:/Weapons/Grizzly/Grizzly Reload Quick MagOut"] = 243,
	["event:/Weapons/Grizzly/Grizzly Reload Empty MagOut"] = 133,
	["event:/Weapons/Grizzly/Grizzly MagIn"] = 255,
	["event:/Weapons/Grizzly/Grizzly Reload Quick MagIn"] = 104,
	["event:/Weapons/Grizzly/Grizzly Reload Empty MagIn"] = 204,
	["event:/Weapons/Grizzly/Grizzly Reload Empty Slide Release"] = 56,
	["event:/Weapons/Shared/Draw/Draw Pistol V2"] = 83,
	["event:/Weapons/Shared/Pistols Shared/Pistol Holster"] = 440,
	["event:/Weapons/Shared/Mag Drops/Pistol Mag Drop"] = 0,          -- first bounce
	["event:/Weapons/G19 New/G19 Cloth-ReloadStart"] = 296,
	["event:/Weapons/G19 New/G19 Cloth-PouchGrab"] = 283,
	["event:/Weapons/G19 New/G19 Cloth-ReloadEnd"] = 7,
	["event:/Weapons/G19 New/G19 Cloth-CheckDone"] = 0,
	["event:/Weapons/G19 New/G19 Dry Mag Out"] = 3,
	["event:/Weapons/G19 New/G19 WetMagOutQuick"] = 0,
	["event:/Weapons/G19 New/G19 WetMagInQuick"] = 238,
	["event:/Weapons/G19 New/G19 Slide"] = 191,
	["event:/Weapons/G19 New/G19 Mag Check Out"] = 13,
	["event:/Weapons/G19 New/G19 Mag Check In"] = 217,
}

-- per gun, per clip: { clip seconds, event, start = true (the time is when it starts, like RoN's own action-start
-- events) or the time its click lands, feet = true (at the player's feet: a mag hitting the ground) }
local G, S = "event:/Weapons/Grizzly/Grizzly ", "event:/Weapons/Shared/"
local DROP = S .. "Mag Drops/Pistol Mag Drop"
local MAG_FALL = 0.45   -- s from a mag leaving the gun to it hitting the ground (about 1 m: sqrt(2 x 1 / 9.81))
local deagle = {
	-- The moments are the clips' own frames (24 fps timeline), measured from the tracks (j_mag1 / j_mag2 / j_slide in the
	-- gun's space, 2026-10-13): mag out = between its last seated frame and the first moving one, mag in = the seated
	-- frame, slide = home. The old times were the MWII sound cues, moved early by those files' lead-ins (up to 7 frames).
	-- tac reload: release (0.667), mag out (frames 17-18: 0.729), mag in (frame 37: 1.542)
	reload = { { 0.667, G .. "ReloadStart" }, { 0.729, G .. "MagOut" }, { 1.542, G .. "MagIn" } },
	-- empty reload: starts with the gun (RoN's Start part), mag out (frames 5-6: 0.229) and drops, mag in (frame 31:
	-- 1.292), slide home (frame 48: 2.000)
	reloadEmpty = { { 0, G .. "Reload Empty Start", start = true }, { 0.229, G .. "Reload Empty MagOut" },
		{ 0.229 + MAG_FALL, DROP, feet = true }, { 1.292, G .. "Reload Empty MagIn" }, { 2.000, G .. "Reload Empty Slide Release" } },
	-- inspect: mag out (frames 7-8: 0.312), back in (frame 77: 3.208), slide home after the press check (frame 112: 4.667)
	inspect = { { 0.312, G .. "Reload Empty MagOut" }, { 3.208, G .. "MagIn" }, { 4.667, G .. "Reload Empty Slide Release" } },
	inspectEmpty = { { 0.312, G .. "Reload Empty MagOut" }, { 3.208, G .. "MagIn" } },
	-- draw / holster: RoN's own start times on its 0.833 s actions
	draw = { { 0.188, S .. "Draw/Draw Pistol V2", start = true } },
	drawEmpty = { { 0.188, S .. "Draw/Draw Pistol V2", start = true } },
	holster = { { 0.180, S .. "Pistols Shared/Pistol Holster", start = true } },
	holsterEmpty = { { 0.180, S .. "Pistols Shared/Pistol Holster", start = true } },
}
local N = "event:/Weapons/G19 New/G19 "
local glock = {
	-- quick tac reload (1.33 s): turn (0.433), mag out (0.567), mag in (1.167), done (1.333)
	reload = { { 0.433, N .. "Cloth-ReloadStart" }, { 0.567, N .. "WetMagOutQuick" }, { 1.167, N .. "WetMagInQuick" }, { 1.333, N .. "Cloth-ReloadEnd" } },
	-- empty: mag out (0.300) and drops, pouch grab (0.567), mag in (0.685), slide released (1.333), done (1.367)
	reloadEmpty = { { 0, N .. "Cloth-ReloadStart", start = true }, { 0.300, N .. "Dry Mag Out" }, { 0.300 + MAG_FALL, DROP, feet = true },
		{ 0.567, N .. "Cloth-PouchGrab" }, { 0.685, N .. "WetMagInQuick" }, { 1.333, N .. "Slide" }, { 1.367, N .. "Cloth-ReloadEnd" } },
	-- mag check: out (0.367), in (1.767), slide released (2.900), done (3.500)
	inspect = { { 0.367, N .. "Mag Check Out" }, { 1.767, N .. "Mag Check In" }, { 2.900, N .. "Slide" }, { 3.500, N .. "Cloth-CheckDone" } },
	inspectEmpty = { { 0.367, N .. "Mag Check Out" }, { 1.767, N .. "Mag Check In" }, { 2.900, N .. "Slide" }, { 3.500, N .. "Cloth-CheckDone" } },
	-- draw racks the slide (released at 0.600)
	draw = { { 0.188, S .. "Draw/Draw Pistol V2", start = true }, { 0.600, N .. "Slide" } },
	drawEmpty = { { 0.188, S .. "Draw/Draw Pistol V2", start = true }, { 0.600, N .. "Slide" } },
	holster = { { 0.180, S .. "Pistols Shared/Pistol Holster", start = true } },
	holsterEmpty = { { 0.180, S .. "Pistols Shared/Pistol Holster", start = true } },
}
if Deagle.guns.deagle then Deagle.guns.deagle.ronCues = deagle end
if Deagle.guns.glock_17 then Deagle.guns.glock_17.ronCues = glock end
