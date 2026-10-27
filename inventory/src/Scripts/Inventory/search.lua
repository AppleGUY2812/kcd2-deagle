-- Inventory search (Ctrl+F): fuzzy search over item and category names, "or" between alternatives
-- ("brunswick helmet or brunswick armour"). The matcher and the bar are in the patched inventory movie
-- (inventory/tools/weightmap/search.as). KCD2's menus pass Lua no keys and a typed letter would fire the game's own
-- menu actions (X moves an item, I leaves the screen), so the plugin owns the keyboard (native/src/invsearch.cpp):
--   Ctrl+F  -> inv_search_toggle: if the inventory is open, the bar is shown and the plugin is told "search on";
--   a key   -> inv_search_text <text>: the plugin's buffer, sent on every change (no key reaches the game);
--   Enter   -> inv_search_commit: typing stops, the bar stays up as "Filtered: ..." (Ctrl+F or the magnifier reopens
--              it, and Backspace goes straight back to editing), the list stays filtered;
--   Backspace on that kept filter -> inv_search_edit + inv_search_text: the plugin takes the keys again and edits;
--   Escape  -> inv_search_cancel: typing stops and the filter is cleared.
-- The listener below resets a screen that was left filtered when it closed (KCD2 keeps menu movies loaded).
InvSearch = InvSearch or { open = false, text = "" }

local function log(s) System.LogAlways("[Inv] search: " .. tostring(s)) end

-- the movie's entry point (UIElements/ApseInventoryList.xml: InvSearch -> fc_invSearch)
local function show(text, open)
	UIAction.CallFunction("ApseInventoryList", -1, "InvSearch", text, open and 1 or 0)
end

function InvSearch:InventoryOpen()
	return FastMove ~= nil and FastMove.open ~= nil and FastMove.open.ApseInventoryList == true
end

function InvSearch:Toggle()
	if not self:InventoryOpen() then
		Inv:Native("search off")   -- the bar lives only in the inventory: keep the plugin from eating keys
		return
	end
	if self.open then
		self:Commit()
		return
	end
	self.open = true
	show(self.text, true)   -- a kept filter reopens as it is: the plugin's buffer is kept for exactly this
	Inv:Native("search on")
	log("typing on: Enter keeps the filter, Backspace edits it again, Escape clears it")
end

-- the sort row's search button (the mouse's way in; Ctrl+F is the shortcut, and both go through Toggle)
function InvSearch:OnClick()
	self:Toggle()
end

-- every change of the plugin's buffer (it owns backspace, so the text is the whole query, not a key)
function InvSearch:Text(line)
	if not self.open then return end
	-- the console substitutes %line quoted (see Inv.Arg in inventory_main.lua); the plugin itself sends it bare
	local t = tostring(line or ""):gsub('"', ""):match("^%s*(.-)%s*$")
	self.text = t
	show(t, true)
end

function InvSearch:Commit()
	if not self.open then return end
	self.open = false
	show(self.text, false)
	Inv:Native("search keep")   -- Backspace still edits the filter (search off would hand it back to the game)
	log('filter kept: "' .. (self.text ~= "" and self.text or "none") .. '"')
end

-- the plugin's Backspace on a kept filter: show the bar again, the text that follows is the edited query
function InvSearch:Edit()
	if self.open then return end
	self.open = true
	show(self.text, true)
	log("editing the kept filter again")
end

function InvSearch:Cancel()
	self.open = false
	self.text = ""
	if self:InventoryOpen() then show("", false) end
	Inv:Native("search off")
end

-- the screen reports open / closed (its movie fires onInvActive; the parameter is at index 0, like fastmove.lua's)
function InvSearch:OnActive(elementName, instanceId, eventName, args)
	local v = type(args) == "table" and (args[0] or args[1]) or nil
	local on = v == true or v == 1 or v == "true"
	if on then
		if self.text ~= "" or self.open then
			self.text = ""
			self.open = false
			show("", false)   -- a new screen starts unfiltered
			Inv:Native("search off")   -- and the plugin keeps nothing from the last one either
		end
	else
		self.text = ""
		self.open = false
		Inv:Native("search off")
	end
end

if type(UIAction) == "table" and UIAction.RegisterElementListener then
	UIAction.RegisterElementListener(InvSearch, "ApseInventoryList", -1, "OnInvActive", "OnActive")
	UIAction.RegisterElementListener(InvSearch, "ApseInventoryList", -1, "OnInvSearchClick", "OnClick")
end
