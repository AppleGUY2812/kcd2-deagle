-- Inventory Tools for KCD2: the weight map, G fast transfer and the inventory's open speed, as a mod of its own.
-- The weight map and the fast transfer live in the patched Apse movies (Libs/UI, built by tools/weightmap); this file
-- holds the settings, the console commands and the link to the optional plugin (inventory.asi). The plugin reads the
-- fast-transfer key and presses the game's own keys; its commands start with "[INV] " and it answers the hello with
-- the inv_native_ok console command. Without it the movies still work, minus the G key (KCD2's menus pass no keys).
Inv = Inv or {}

local cfg = Inv.cfg or {
	invtime = 2,     -- inventory open speed: 2 skips the "clothes ready" gate, 1 only logs, 0 off (docs/inventory_load.md)
	fastkey = "g",   -- the fast-transfer key, watched by the plugin
}
Inv.cfg = cfg

function Inv:Native(cmd) System.LogAlways("[INV] " .. tostring(cmd)) end
-- console arguments arrive quoted ("h"): strip them (the game substitutes %line with its quotes)
function Inv.Arg(v) return (string.gsub(tostring(v or ""), '"', '')):match("^%s*(.-)%s*$") end
local function log(msg) System.LogAlways("$5[Inv] " .. tostring(msg)) end

-- the fast-transfer key reader (Scripts/Inventory/fastmove.lua); it registers its own console commands
if Script.LoadScript("Scripts/Inventory/fastmove.lua") ~= 1 then
	log("$4failed to load Scripts/Inventory/fastmove.lua")
end
-- the item search's typing and bar state (Scripts/Inventory/search.lua), driven by the plugin's Ctrl+F and keys
if Script.LoadScript("Scripts/Inventory/search.lua") ~= 1 then
	log("$4failed to load Scripts/Inventory/search.lua")
end
-- what the plugin needs to know: sent at script start and again at every game load (it forgets both at "hello")
function Inv:SendSettings()
	self:Native("invtime " .. tostring(self.cfg.invtime))
	self:Native("fastkey " .. tostring(FastMove and FastMove.key or self.cfg.fastkey))
end
function Inv:Hello()
	self:Native("hello")
	self:SendSettings()
end

function Inv:OnGameplayStarted()
	self:Hello()
end
if type(UIAction) == "table" and UIAction.RegisterEventSystemListener then
	UIAction.RegisterEventSystemListener(Inv, "System", "OnGameplayStarted", "OnGameplayStarted")
end
Inv:Hello()

-- What a setting changes besides itself. Both are kept by the plugin (inventory_settings.txt next to it) and replayed
-- as inv_set lines at every hello.
local APPLY = {
	invtime = function(self, v)
		local n = tonumber(v)
		if not n then return end
		self.cfg.invtime = n
		self:Native("invtime " .. tostring(n))
	end,
	fastkey = function(self, v)
		self.cfg.fastkey = tostring(v)
		if FastMove then FastMove:SetKey(self.cfg.fastkey) end
	end,
}

-- inv_set <key> <value>: the plugin's replay of a kept setting
function Inv:Set(line)
	local key, value = string.match(Inv.Arg(line), "^%s*(%S+)%s+(.-)%s*$")
	if not key then log("usage: inv_set <key> <value>") return end
	if not APPLY[key] then log("no setting " .. key) return end
	APPLY[key](self, value)
end

function Inv:Invtime(line)
	local n = tonumber(Inv.Arg(line))
	if not n then log("inv_invtime 0|1|2|3|4") return end
	self:Set("invtime " .. n)
	self:Native("set invtime " .. n)   -- kept for the next load
end

function Inv:FastKey(line)
	if FastMove then FastMove:SetKey(Inv.Arg(line)) end
	self:Native("set fastkey " .. tostring(FastMove and FastMove.key or self.cfg.fastkey))
end

-- the plugin's handshake: it queued its kept settings before this, which have set the two values above already;
-- send them once more so a plugin that loaded after this script has them too (and no second "hello": that would loop)
function Inv:NativeOk()
	self.native = true
	self:SendSettings()
end

System.AddCCommand("inv_native_ok", "Inv:NativeOk()", "internal: the Inventory Tools plugin handshake")
System.AddCCommand("inv_invtime", "Inv:Invtime([[%line]])", "inv_invtime 0|1|2|3|4: inventory open speed: 2 skips the clothes-ready gate (default), 1 only logs, 0 off")
System.AddCCommand("inv_fastmove_key", "Inv:FastKey([[%line]])", "inv_fastmove_key <key>|off: the weight map's fast-transfer key (default g)")
System.AddCCommand("inv_fastkey", "FastMove:Key(Inv.Arg([[%line]]))", "internal: the plugin's read of the fast-transfer key (1 press / repeat, 0 release)")
System.AddCCommand("inv_mods", "FastMove:Mods(Inv.Arg([[%line]]))", "internal: the plugin's read of Shift (1) / Ctrl (2) for the weight map's multi-select")
System.AddCCommand("inv_set", "Inv:Set([[%line]])", "inv_set <key> <value>: a kept setting replayed by the plugin at every hello")
-- the plugin's keyboard for the item search (native/src/invsearch.cpp): Ctrl+F -> toggle, keys -> text, then Enter /
-- Escape; the bar is the movie's (inventory/tools/weightmap/search.as)
System.AddCCommand("inv_search_toggle", "InvSearch:Toggle()", "internal: Ctrl+F in the inventory: type in the item search")
System.AddCCommand("inv_search_text", "InvSearch:Text([[%line]])", "internal: the keys typed into the search bar")
System.AddCCommand("inv_search_commit", "InvSearch:Commit()", "internal: Enter in the search bar (keep the filter)")
System.AddCCommand("inv_search_edit", "InvSearch:Edit()", "internal: Backspace on a kept filter (edit the query again)")
System.AddCCommand("inv_search_cancel", "InvSearch:Cancel()", "internal: Escape in the search bar (clear the filter)")
