-- Fast transfer (G by default): moves the weight map's selection, or the item under the mouse, at the full amount with no
-- quantity box, and repeats while the key is held. The inventory screens' own movies do the work
-- (inventory/tools/weightmap, patched ApseInventoryList / ItemTransfer / ApseModalDialog); this file only wires the key:
--   the plugin reads the key every frame (fastkey) and sends inv_fastkey 1 (press, then every 150 ms) / 0 (release);
--   the screens tell us when they open and close (OnInvActive), so we only call the one that is open;
--   chest / horse / loot screen: its movie sends the selection through the game's own transfer (double-click path)
--     and confirms the quantity box itself at the maximum;
--   inventory: its movie answers OnInvFastMove when it has items to move; we arm the quantity dialog's
--     auto-confirm and tap the game's move key (move_item, default X), which the plugin posts through KCD2's input;
--   shop: the focused item not in the basket yet goes in (the basket action toggles); OnInvFastShop -> we arm the
--     dialog, then the movie sends the basket double-click; held G steps to the next item.
-- Why this shape, from the game's code: inventory/docs/inventory_multiselect.md (no game path moves several items
-- without the quantity box; the movie may confirm it one frame later with exactly the maximum).
FastMove = FastMove or { key = "g", moveKey = "x", open = {} }

local function log(s) System.LogAlways("[Inv] fastmove: " .. s) end

function FastMove:Key(line)
	if not tostring(line):match("^%s*1") then return end
	-- only the screen that is open: calling an element that isn't loaded could load it
	if self.open.ItemTransfer then UIAction.CallFunction("ItemTransfer", -1, "InvFast") end
	if self.open.ApseInventoryList then UIAction.CallFunction("ApseInventoryList", -1, "InvFast") end
end

-- Shift / Ctrl for the weight map's shift / ctrl click (the plugin reads them; the menus never pass them to Flash)
function FastMove:Mods(line)
	local m = tonumber(tostring(line or ""):match("%d+")) or 0
	local sh, ct = m % 2, m >= 2 and 1 or 0
	if self.open.ItemTransfer then UIAction.CallFunction("ItemTransfer", -1, "InvMods", sh, ct) end
	if self.open.ApseInventoryList then UIAction.CallFunction("ApseInventoryList", -1, "InvMods", sh, ct) end
end

-- the screens report open / closed (their movies fire onInvActive)
function FastMove:OnActive(elementName, instanceId, eventName, args)
	-- the event's one parameter comes in at index 0 (seen in game 2026-10-21: args {0=true}), not 1
	local v = type(args) == "table" and args[0]
	if v == nil and type(args) == "table" then v = args[1] end
	local on = v == true or v == 1 or v == "true"
	self.open[elementName] = on or nil
	log(tostring(elementName) .. (on and " open" or " closed"))
end

-- the weight map's rebuild timings (WM_STAT in inventory/tools/weightmap/common.as)
function FastMove:OnStat(elementName, instanceId, eventName, args)
	local v = type(args) == "table" and (args[0] or args[1]) or args
	log(tostring(elementName) .. " " .. tostring(v))
end

-- the inventory movie has items to move: arm the dialog's auto-confirm, then press the game's move key once
function FastMove:OnFastMove(elementName, instanceId, eventName, args)
	log("fast move: armed the amount box, pressing " .. self.moveKey .. " (item " .. tostring(args and args[0]) .. ")")
	UIAction.CallFunction("ApseModalDialog", -1, "InvAutoMax")
	Inv:Native("key " .. self.moveKey .. " down")
	Inv:Native("key " .. self.moveKey .. " up")
end

-- shop: arm the quantity dialog first, then let the movie send the basket double-click (it opens the dialog at once)
function FastMove:OnFastShop(elementName, instanceId, eventName, args)
	-- a sell or buy through us leaves these lines in kcd.log: if the screen goes blank after a sale and there is no
	-- "shop:" line, the sale went through the game's own click, not this path
	log("shop: armed the amount box, sending the basket click on " .. tostring(elementName) .. " " .. tostring(instanceId))
	UIAction.CallFunction("ApseModalDialog", -1, "InvAutoMax")
	UIAction.CallFunction("ApseInventoryList", instanceId or -1, "InvShopClick")
end

function FastMove:SetKey(line)
	local k = tostring(line or ""):match("^%s*(%S+)")
	if not k then log("key is " .. self.key .. " (inv_fastmove_key <key> | off)") return end
	self.key = k:lower()
	Inv:Native("fastkey " .. self.key)
	log("key set to " .. self.key)
end

-- (the test harness has no UIAction: the listeners are the game's only)
if type(UIAction) == "table" and UIAction.RegisterElementListener then
	UIAction.RegisterElementListener(FastMove, "ApseInventoryList", -1, "OnInvActive", "OnActive")
	UIAction.RegisterElementListener(FastMove, "ItemTransfer", -1, "OnInvActive", "OnActive")
	UIAction.RegisterElementListener(FastMove, "ApseInventoryList", -1, "OnInvStat", "OnStat")
	UIAction.RegisterElementListener(FastMove, "ItemTransfer", -1, "OnInvStat", "OnStat")
	UIAction.RegisterElementListener(FastMove, "ApseInventoryList", -1, "OnInvFastMove", "OnFastMove")
	UIAction.RegisterElementListener(FastMove, "ApseInventoryList", -1, "OnInvFastShop", "OnFastShop")
end
-- (inv_fastkey / inv_mods / inv_fastmove_key are registered in inventory_main.lua, which loads this file)
