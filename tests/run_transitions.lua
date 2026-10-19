-- Every risky clip combination: checks the clip and blend the script starts against the audited transition rules.
-- usage: luajit tests/run_transitions.lua
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted(); M.console("deagle_native_ok")
local fails, n = 0, 0
local function act(a, act_) Deagle:OnAction(a, act_ or "press") end
local function mark() return #M.anims end
local function since(m)
	local out = {}
	for i = m + 1, #M.anims do local a = M.anims[i] if a.layer == 0 then out[#out + 1] = a.name:gsub("deagle_vm_", "") .. "/" .. string.format("%.2f", a.blend or -1) end end
	return table.concat(out, " ")
end
local function expect(label, got, want)
	n = n + 1
	local ok = got:find(want, 1, true) ~= nil
	if not ok then fails = fails + 1 end
	print(string.format("%s %-44s %s", ok and "PASS" or "FAIL", label, ok and got or (got .. "   (want: " .. want .. ")")))
end
local function reset(ammo) Deagle.state.ammo = ammo or 7; M.advance(3.0) end

local m = mark(); act("deagle_toggle"); M.advance(0.3); act("deagle_fire"); act("deagle_reload"); act("deagle_inspect"); M.advance(1.6)
expect("draw blocks fire / reload / inspect", since(m), "draw/0.00 idle1/0.10")   -- from hidden: its own first frame   -- ends where it settles: 0.1 s blend
expect("  ...nothing else started", tostring(select(2, since(m):gsub("/", ""))), "2")

reset(7); m = mark(); act("deagle_fire"); M.advance(0.25); act("deagle_fire"); M.advance(2.0)
expect("refire mid-recoil blends 0.12", since(m), "/0.00 shoot2/0.12 idle1/0.00")

reset(7); m = mark(); act("deagle_fire"); M.advance(0.1); act("deagle_reload"); M.advance(3.0)
expect("reload cuts a shot over 0.2", since(m), "/0.00 reload/0.20")
expect("reload end -> idle instantly", since(m), "reload/0.20 idle1/0.00")

reset(7); m = mark(); act("deagle_inspect"); M.advance(1.0); act("deagle_inspect"); M.advance(0.2); act("deagle_fire"); M.advance(2.0)
expect("Y mid-inspect is ignored, fire cuts over 0.3", since(m), "inspect/0.08 shoot2/0.30")

reset(6); m = mark(); act("deagle_reload"); M.advance(1.0); act("deagle_toggle"); M.advance(2.0)
print("     holster asked mid-reload; drawn:", tostring(Deagle.state.drawn))
M.advance(1.5)
expect("holster waits for the reload, swap first", since(m), "reload/0.08 idle1/0.00 holster/0.08")
expect("  ...and the gun is away", tostring(Deagle.vmShown), "false")

m = mark(); act("deagle_toggle"); M.advance(0.5); act("deagle_toggle"); M.advance(1.2)
expect("holster asked mid-draw waits for the draw", since(m), "draw/0.00 holster/0.08")
M.advance(1.0); m = mark(); act("deagle_toggle"); M.advance(0.3); act("deagle_toggle"); M.advance(0.3); act("deagle_toggle"); M.advance(2.5)
expect("draw/holster double-tap ends where asked", tostring(Deagle.state.drawn) .. " " .. tostring(Deagle.vmShown), "true true")

reset(0); m = mark(); act("deagle_inspect"); M.advance(7.0)
expect("empty gun: inspect_empty, back to empty idle", since(m), "inspect_empty/0.08 idle_empty/0.10")
m = mark(); act("deagle_toggle"); M.advance(1.5)
expect("empty gun: holster_empty", since(m), "holster_empty/")
m = mark(); act("deagle_toggle"); M.advance(1.8)
expect("empty gun: draw_empty -> idle_empty", since(m), "draw_empty/0.00 idle_empty/0.10")
m = mark(); act("deagle_reload"); M.advance(3.2)
expect("empty reload settles into idle", since(m), "reload_empty/0.08 idle1/0.10")

-- reload timing (Deagle times / rate 1.25): mag clicks in 1.18 s (the retimed mag-in cue), ready 1.90 s, clean cancel
-- until 0.30 s, old mag out at 0.67 s. Staged like Insurgency: Sandstorm; a chambered round stays (7 + 1).
reset(3); act("deagle_reload"); M.advance(1.25)
expect("the mag clicks in: 7 + the chambered round", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading), "8 true")
M.advance(0.7)
expect("  ...and the reload is over once the gun is up", tostring(Deagle.state.reloading), "false")
reset(3); m = mark(); act("deagle_reload"); M.advance(0.15); act("deagle_fire"); M.advance(1.5)
expect("a shot before the spare shows cancels cleanly", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading) .. " " .. since(m), "2 false reload/0.08 shoot")
reset(3); act("deagle_reload"); M.advance(0.5); act("deagle_fire"); M.advance(0.05)
local hiddenMid = tostring(M.hidden and M.hidden.vm_mag1) .. "/" .. tostring(M.hidden and M.hidden.vm_mag2)
M.advance(0.4)
expect("old mag still in: the rounds stay (mags hidden in the blend)", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading) .. " " .. hiddenMid .. " then " .. tostring(M.hidden.vm_mag1), "2 false true/true then false")
reset(3); act("deagle_reload"); M.advance(0.8); act("deagle_fire"); M.advance(0.5)
expect("old mag out: the chambered round fires, no mag after", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading) .. " mag hidden " .. tostring(M.hidden.vm_mag1), "0 false mag hidden true")
act("deagle_reload"); M.advance(0.7); local magBack = tostring(M.hidden.vm_mag1); M.advance(2.6)
expect("  ...the next reload brings a mag back (empty: 7)", magBack .. " ammo " .. Deagle.state.ammo, "false ammo 7")
reset(3); act("deagle_reload"); M.advance(1.5); act("deagle_fire"); M.advance(0.05)
local queued = Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading)
M.advance(0.6)
expect("after the click, the shot waits for the gun to come up", queued .. " -> " .. Deagle.state.ammo, "8 true -> 7")
reset(0); m = mark(); act("deagle_reload"); M.advance(0.1); act("deagle_fire"); M.advance(0.5)
expect("an empty gun has nothing to fire: the reload goes on", tostring(Deagle.state.reloading) .. " " .. since(m), "true reload_empty/0.08")
M.advance(3.0)
reset(8); act("deagle_reload"); M.advance(0.2)
expect("7 + 1 is full: no reload", tostring(Deagle.state.reloading), "false")

Deagle.cfg.vm.ads = true
reset(6); m = mark(); act("deagle_aim"); M.advance(0.3); act("deagle_reload"); M.advance(3.2)
expect("aim held through reload: swap, then ADS", since(m), "reload/0.20 idle1/0.00 ads/0.20")
m = mark(); act("deagle_toggle"); M.advance(1.5)
expect("holster from ADS blends 0.2", since(m), "holster/0.20")
act("deagle_aim", "release"); M.advance(0.2)
m = mark(); act("deagle_toggle"); M.advance(0.2); act("deagle_aim"); M.advance(1.6)
expect("aim held through draw hands over at 0.2", since(m), "draw/0.00 ads/0.20")
act("deagle_aim", "release"); M.advance(0.5)
reset(7); m = mark(); act("deagle_fire"); M.advance(0.1); act("deagle_aim"); M.advance(0.5)
expect("aim cuts a hip shot over 0.2", since(m), "/0.00 ads/0.20")
act("deagle_aim", "release"); M.advance(0.5)
reset(7); m = mark(); act("deagle_inspect"); M.advance(1.0); act("deagle_aim"); M.advance(0.5)
expect("aim cuts inspect over 0.3", since(m), "inspect/0.08 ads/0.30")

-- the holster hides the gun before its clip's last key: a finished base-layer clip drops to the rest pose (the idle
-- gun), shown for a frame when the hide came on a timer (the user, 2026-10-19). Frame by frame at 60 and 30 fps.
Deagle.cfg.vm.ads = false
local function holsterEnd(fps)
	reset(7)
	if not Deagle.state.drawn then act("deagle_toggle"); M.advance(2.0) end
	local vm = System.GetEntity(Deagle.vmId)
	act("deagle_toggle")
	local pop, last = 0, 0
	for _ = 1, 2 * fps do
		M.advance(1 / fps, fps)
		local t = vm:GetAnimationTime(0, 0)
		if not vm.hidden and tostring(vm.layers[0]):find("holster") then
			last = t
			if t >= 1 then pop = pop + 1 end
		end
	end
	return string.format("%d rest-pose frames, last frame shown at %.3f, hidden %s", pop, last, tostring(vm.hidden)), last
end
for _, fps in ipairs({ 60, 30 }) do
	local got, last = holsterEnd(fps)
	expect("holster ends hidden at " .. fps .. " fps, no rest pose", got, "0 rest-pose frames")
	-- hidden for its last 2 frames (HOLSTER_AHEAD): the last one shown is at most 3 frames of the clip before its end
	local a, c = Deagle.cfg.vm.anims.holster, Deagle.cfg.vm
	local span = (a[2] - 1) / (a[3] or c.fps) / (c.holsterSpeed * Deagle:Rate("holster"))
	expect("  ...and shows the clip to its last 3 frames", tostring(last and last >= 1 - 3.01 / (fps * span)), "true")
	print("     " .. got)
end

print(string.format("\n%d/%d transition checks pass; script errors: %d", n - fails, n, #M.errors))
for _, e in ipairs(M.errors) do print("  " .. e) end
os.exit(fails == 0 and #M.errors == 0 and 0 or 1)
