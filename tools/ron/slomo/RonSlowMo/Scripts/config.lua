-- Ready or Not slow motion (RonSlowMo): edit, save, restart Ready or Not.
-- Key names: F1-F24, HOME, END, INS, DEL, PAGE_UP, PAGE_DOWN, NUM_ZERO-NUM_NINE, A-Z (UE4SS's Key table).
-- The keys the user picked 2026-10-20 (the INS / PAGE_UP / PAGE_DOWN / HOME / END block).
return {
	keys = {
		toggle = "INS",       -- slow motion on/off
		slower = "PAGE_DOWN", -- one speed down the list (turns slow motion on)
		faster = "PAGE_UP",   -- one speed up the list (turns slow motion on)
		freeze = "HOME",      -- stop time / carry on (the camera still turns)
		step   = "END",       -- while frozen: play step_frames frames at step_speed, then freeze again
	},
	-- game speed, 1 = normal. A pistol's muzzle flash lasts about 2-3 frames at 60 fps (~40 ms): at 0.02 it lasts ~2 s
	speeds = { 0.5, 0.25, 0.1, 0.05, 0.02, 0.01, 0.005 },
	start_speed = 0.05,  -- what toggle turns on first (picked from the list, nearest)
	step_speed = 0.1,    -- game speed during one step (0.1 at 60 fps = 1.7 ms of game time per frame)
	step_frames = 1,
}
