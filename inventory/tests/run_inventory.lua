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
		if path == "Scripts/Inventory/fastmove.lua" then
			dofile("inventory/src/Scripts/Inventory/fastmove.lua")
			return 1
		end
		if path == "Scripts/Inventory/search.lua" then
			dofile("inventory/src/Scripts/Inventory/search.lua")
			return 1
		end
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
-- everything the mod logged (the [Inv] lines are not the plugin handshake's [INV] ones)
local function lastLog(prefix)
	for i = #sent, 1, -1 do
		if sent[i]:sub(1, #prefix) == prefix then return sent[i] end
	end
end
local function hasLog(text)
	for i = 1, #sent do
		if sent[i]:find(text, 1, true) then return true end
	end
	return false
end

for _, name in ipairs({ "inv_native_ok", "inv_invtime", "inv_fastmove_key", "inv_fastkey", "inv_mods", "inv_set",
	"inv_search_toggle", "inv_search_text", "inv_search_commit", "inv_search_cancel", "inv_search_edit" }) do
	if not cmds[name] then check("command " .. name .. " is registered", false, "missing") end
end
check("all eleven console commands are registered", cmds.inv_native_ok and cmds.inv_invtime and cmds.inv_fastmove_key
	and cmds.inv_fastkey and cmds.inv_mods and cmds.inv_set and cmds.inv_search_toggle and cmds.inv_search_text
	and cmds.inv_search_commit and cmds.inv_search_cancel and cmds.inv_search_edit)

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

-- the item search: Ctrl+F (the plugin sends inv_search_toggle), the typed text, Enter and Escape
calls = {}
cmd("inv_search_toggle")
check("Ctrl+F with the inventory open shows the bar", calls[1] and calls[1].element == "ApseInventoryList"
	and calls[1].fn == "InvSearch" and calls[1][1] == "" and calls[1][2] == 1, calls[1] and (tostring(calls[1].fn) .. " " .. tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... and tells the plugin to take the keys", last("[INV] search ") == "[INV] search on", last("[INV] search "))
calls = {}
cmd("inv_search_text", "brunswick helmet or brunswick armour")
check("the typed text goes to the movie with the bar up", calls[1] and calls[1].element == "ApseInventoryList"
	and calls[1].fn == "InvSearch" and calls[1][1] == "brunswick helmet or brunswick armour" and calls[1][2] == 1,
	calls[1] and (tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... and is kept for the reopen", InvSearch.text == "brunswick helmet or brunswick armour", InvSearch.text)
calls = {}
cmd("inv_search_commit")
check("Enter hides the bar and keeps the filter", calls[1] and calls[1][1] == "brunswick helmet or brunswick armour"
	and calls[1][2] == 0, calls[1] and (tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... and keeps the plugin taking Backspace (search keep)", last("[INV] search ") == "[INV] search keep", last("[INV] search "))
-- Backspace on the kept filter (the plugin answers with inv_search_edit + the shortened text): the box comes back
calls = {}
cmd("inv_search_edit")
check("Backspace on the kept filter shows the bar again", calls[1] and calls[1][1] == "brunswick helmet or brunswick armour"
	and calls[1][2] == 1, calls[1] and (tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... with typing on", InvSearch.open == true, tostring(InvSearch.open))
calls = {}
cmd("inv_search_text", "brunswick helmet or brunswick")
check("... and the edited text replaces the query", InvSearch.text == "brunswick helmet or brunswick"
	and calls[1][1] == "brunswick helmet or brunswick", InvSearch.text)
cmd("inv_search_toggle")
calls = {}
cmd("inv_search_cancel")
check("Escape clears the filter", calls[1] and calls[1][1] == "" and calls[1][2] == 0, calls[1] and (tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... and stops the typing", last("[INV] search ") == "[INV] search off", last("[INV] search "))
-- the sort row's search button: the same path as Ctrl+F, so the mouse opens the box too
calls = {}
InvSearch:OnClick()
check("the search button opens the box", calls[1] and calls[1][1] == "" and calls[1][2] == 1,
	calls[1] and (tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... and turns the plugin's typing on", last("[INV] search ") == "[INV] search on", last("[INV] search "))
InvSearch:Text("sword")
calls = {}
InvSearch:OnClick()
check("... pressing it again keeps the filter and hides the bar", calls[1] and calls[1][1] == "sword" and calls[1][2] == 0,
	calls[1] and (tostring(calls[1][1]) .. " " .. tostring(calls[1][2])))
check("... and keeps the filter editable with Backspace", last("[INV] search ") == "[INV] search keep", last("[INV] search "))
cmd("inv_search_cancel")
-- a screen reopened after a filter was kept must start unfiltered
InvSearch.text = "sword"
calls = {}
InvSearch:OnActive("ApseInventoryList", -1, "OnInvActive", { [0] = true })
check("a reopened screen starts unfiltered", #calls == 1 and calls[1][1] == "" and calls[1][2] == 0, #calls)
-- the bar lives only in the inventory
FastMove:OnActive("ApseInventoryList", -1, "OnInvActive", { [0] = false })
calls = {}
cmd("inv_search_toggle")
check("Ctrl+F outside the inventory calls no movie", #calls == 0, #calls)
check("... and keeps the plugin from taking keys", last("[INV] search ") == "[INV] search off", last("[INV] search "))
FastMove:OnActive("ApseInventoryList", -1, "OnInvActive", { [0] = true })   -- back, for the checks below

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
