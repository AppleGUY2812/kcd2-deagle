-- Inventory Tools' Lua side, without the game: the console commands, the plugin handshake and the fast-transfer key's
-- wiring to the movies (UIAction.CallFunction). usage: luajit inventory/tests/run_inventory.lua   (from the repo root)
local sent, cmds, calls = {}, {}, {}
local native = {}
local LogAlways = function(s)
	s = tostring(s)
	sent[#sent + 1] = s
	if s:sub(1, 6) == "[INV] " then native[#native + 1] = s end
end
System = {
	LogAlways = LogAlways,
	AddCCommand = function(name, code, help) cmds[name] = code end,
}
Script = {
	LoadScript = function(path)
		if path ~= "Scripts/Inventory/fastmove.lua" then return 0 end
		dofile("inventory/src/Scripts/Inventory/fastmove.lua")
		return 1
	end,
}
UIAction = {
	RegisterEventSystemListener = function() end,
	RegisterElementListener = function() end,
	CallFunction = function(element, instance, fn, ...)
		calls[#calls + 1] = { element = element, instance = instance, fn = fn, n = select("#", ...), ... }
	end,
}

dofile("inventory/src/Scripts/Inventory/inventory_main.lua")

local fails, n = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end
-- KCD2 substitutes the console argument into %line quoted, as the mock engine does
local function cmd(name, arg)
	local code = cmds[name]
	if not code then check("command " .. name .. " is registered", false, "missing") return end
	code = code:gsub("%%line", function() return '"' .. tostring(arg or "") .. '"' end)
	local f, err = loadstring(code)
	if not f then check(name .. " compiles", false, err) return end
	local ok, e = pcall(f)
	check(name .. " (" .. tostring(arg) .. ") runs", ok, e)
end
local function last(prefix)
	for i = #native, 1, -1 do
		if native[i]:sub(1, #prefix) == prefix then return native[i] end
	end
end

for _, name in ipairs({ "inv_native_ok", "inv_invtime", "inv_fastmove_key", "inv_fastkey", "inv_mods", "inv_set" }) do
	if not cmds[name] then check("command " .. name .. " is registered", false, "missing") end
end
check("all six console commands are registered", cmds.inv_native_ok and cmds.inv_invtime and cmds.inv_fastmove_key
	and cmds.inv_fastkey and cmds.inv_mods and cmds.inv_set)

local at = #native
cmd("inv_fastmove_key", "h")
check("inv_fastmove_key h sends [INV] fastkey h", last("[INV] fastkey ") == "[INV] fastkey h", last("[INV] fastkey "))
check("... and the key is kept (set fastkey h)", last("[INV] set fastkey ") == "[INV] set fastkey h", last("[INV] set fastkey "))

-- the movie reported the inventory open (args come in at index 0), then the plugin's key press calls its InvFast
FastMove:OnActive("ApseInventoryList", -1, "OnInvActive", { [0] = true })
check("OnActive marks the inventory open", FastMove.open.ApseInventoryList == true, FastMove.open.ApseInventoryList)
calls = {}
cmd("inv_fastkey", "1")
check("a key press calls ApseInventoryList's InvFast once", #calls == 1, #calls)
check("... with the right element / instance / function", calls[1] and calls[1].element == "ApseInventoryList"
	and calls[1].instance == -1 and calls[1].fn == "InvFast",
	calls[1] and (tostring(calls[1].element) .. " " .. tostring(calls[1].instance) .. " " .. tostring(calls[1].fn)))

-- the inventory movie asked for a fast move: arm the dialog, then tap the game's move key through the plugin
calls = {}
FastMove:OnFastMove("ApseInventoryList", -1, "OnInvFastMove", { [0] = "w123" })
local lastTwo = #native >= 2 and (native[#native - 1] .. " | " .. native[#native]) or ""
check("OnFastMove arms the quantity dialog", calls[1] and calls[1].element == "ApseModalDialog" and calls[1].fn == "InvAutoMax",
	calls[1] and (tostring(calls[1].element) .. " " .. tostring(calls[1].fn)))
check("OnFastMove sends key x down, then key x up", lastTwo == "[INV] key x down | [INV] key x up", lastTwo)

local before = #native
cmd("inv_invtime", "3")
check("inv_invtime 3 sets invtime 3", last("[INV] invtime ") == "[INV] invtime 3", last("[INV] invtime "))
check("inv_invtime 3 keeps it (set invtime 3)", last("[INV] set invtime ") == "[INV] set invtime 3", last("[INV] set invtime "))
check("... and changes cfg", Inv.cfg.invtime == 3, Inv.cfg.invtime)

cmd("inv_set", "fastkey k")
check("inv_set fastkey k applies the key", Inv.cfg.fastkey == "k" and FastMove.key == "k", FastMove.key)

Inv.native = false
cmd("inv_native_ok")
check("inv_native_ok marks the plugin linked", Inv.native == true, Inv.native)
check("... and sends the settings again", last("[INV] invtime ") == "[INV] invtime 3" and last("[INV] fastkey ") == "[INV] fastkey k",
	last("[INV] invtime ") .. " / " .. tostring(last("[INV] fastkey ")))

print(string.format("%d checks, %d failed", n, fails))
os.exit(fails == 0 and 0 or 1)
