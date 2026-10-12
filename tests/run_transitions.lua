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
expect("draw blocks fire / reload / inspect", since(m), "draw/0.08 idle1/0.10")   -- ends where it settles: 0.1 s blend
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
expect("holster asked mid-draw waits for the draw", since(m), "draw/0.08 holster/0.08")
M.advance(1.0); m = mark(); act("deagle_toggle"); M.advance(0.3); act("deagle_toggle"); M.advance(0.3); act("deagle_toggle"); M.advance(2.5)
expect("draw/holster double-tap ends where asked", tostring(Deagle.state.drawn) .. " " .. tostring(Deagle.vmShown), "true true")

reset(0); m = mark(); act("deagle_inspect"); M.advance(7.0)
expect("empty gun: inspect_empty, back to empty idle", since(m), "inspect_empty/0.08 idle_empty/0.10")
m = mark(); act("deagle_toggle"); M.advance(1.5)
expect("empty gun: holster_empty", since(m), "holster_empty/")
m = mark(); act("deagle_toggle"); M.advance(1.8)
expect("empty gun: draw_empty -> idle_empty", since(m), "draw_empty/0.08 idle_empty/0.10")
m = mark(); act("deagle_reload"); M.advance(3.2)
expect("empty reload settles into idle", since(m), "reload_empty/0.08 idle1/0.10")

-- reload timing (Deagle times / rate 1.25): mag seated 1.23 s, ready 1.90 s, clean cancel until 0.30 s
reset(3); act("deagle_reload"); M.advance(1.3)
expect("ammo goes in when the mag seats", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading), "7 true")
M.advance(0.65)
expect("  ...and the reload is over once the gun is up", tostring(Deagle.state.reloading), "false")
reset(3); m = mark(); act("deagle_reload"); M.advance(0.15); act("deagle_fire"); M.advance(1.5)
expect("a shot before the mag leaves cancels the reload", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading) .. " " .. since(m), "2 false reload/0.08 shoot")
reset(3); act("deagle_reload"); M.advance(0.6); act("deagle_fire"); M.advance(0.1)
expect("  ...after it nothing until the gun is up", Deagle.state.ammo .. " " .. tostring(Deagle.state.reloading), "3 true")
M.advance(1.3); m = mark(); act("deagle_fire"); M.advance(0.5)
expect("  ...then a shot cuts the settle", Deagle.state.ammo .. " " .. since(m), "6 ")
reset(0); m = mark(); act("deagle_reload"); M.advance(0.1); act("deagle_fire"); M.advance(0.5)
expect("an empty reload cancels to the empty idle", tostring(Deagle.state.reloading) .. " " .. since(m), "false reload_empty/0.08 idle_empty/0.20")

Deagle.cfg.vm.ads = true
reset(6); m = mark(); act("deagle_aim"); M.advance(0.3); act("deagle_reload"); M.advance(3.2)
expect("aim held through reload: swap, then ADS", since(m), "reload/0.20 idle1/0.00 ads/0.20")
m = mark(); act("deagle_toggle"); M.advance(1.5)
expect("holster from ADS blends 0.2", since(m), "holster/0.20")
act("deagle_aim", "release"); M.advance(0.2)
m = mark(); act("deagle_toggle"); M.advance(0.2); act("deagle_aim"); M.advance(1.6)
expect("aim held through draw hands over at 0.2", since(m), "draw/0.08 ads/0.20")
act("deagle_aim", "release"); M.advance(0.5)
reset(7); m = mark(); act("deagle_fire"); M.advance(0.1); act("deagle_aim"); M.advance(0.5)
expect("aim cuts a hip shot over 0.2", since(m), "/0.00 ads/0.20")
act("deagle_aim", "release"); M.advance(0.5)
reset(7); m = mark(); act("deagle_inspect"); M.advance(1.0); act("deagle_aim"); M.advance(0.5)
expect("aim cuts inspect over 0.3", since(m), "inspect/0.08 ads/0.30")

print(string.format("\n%d/%d transition checks pass; script errors: %d", n - fails, n, #M.errors))
for _, e in ipairs(M.errors) do print("  " .. e) end
os.exit(fails == 0 and #M.errors == 0 and 0 or 1)
