-- Native inventory list: a text list drawn by our own Lua every frame, no Scaleform movie. Prototype: text only
-- (no icons, no clicking). Deagle:InvnOpen() reads the player's items once, groups them by class, sorts by English
-- name and keeps the pre-built row texts in self.invn; the tick draws the header and 30 rows per frame.
-- Names/weights come from Deagle.invData[class guid lowercased] (another file); without it (or a class missing from
-- it) the internal ItemManager.GetItemName(class) name and an unknown weight are shown.
local log = (Deagle.U and Deagle.U.log) or function(msg) System.LogAlways("$5[Deagle] " .. tostring(msg)) end

Deagle.invn = Deagle.invn or { open = false, token = 0, offset = 0 }

local ROWS_DRAWN = 30   -- rows on screen at once

-- group the inventory table's items by class, sum their amounts, name / weigh them, sort by English name and build
-- every row's text once (the tick must never format). Returns rows, texts, header, item count, read ms.
function Deagle:InvnRead()
	local t0 = os.clock()
	local p = self:Player()
	local inv = p and p.inventory
	local ids = (inv and inv.GetInventoryTable) and inv:GetInventoryTable() or {}
	local IM = ItemManager
	local byClass, order, itemCount = {}, {}, 0
	for i = 1, #ids do
		local it = IM and IM.GetItem and IM.GetItem(ids[i])
		if it then
			local class = tostring(it.class or "")
			local key = class:lower()
			local row = byClass[key]
			if not row then
				row = { class = class, key = key, amount = 0 }
				byClass[key] = row
				order[#order + 1] = row
			end
			local amount = tonumber(it.amount) or 1
			row.amount = row.amount + amount
			itemCount = itemCount + amount
		end
	end
	-- names and weights: invData may be gone or miss a class -> GetItemName and "?"
	local data, totalWeight = self.invData, 0
	for _, r in ipairs(order) do
		local d = data and data[r.key]
		r.name = (d and d.n) or (IM and IM.GetItemName and IM.GetItemName(r.class)) or r.class
		local w = d and tonumber(d.w)
		if w then
			r.weight = r.amount * w
			totalWeight = totalWeight + r.weight
		else
			r.weight = nil
		end
	end
	table.sort(order, function(a, b)
		local an, bn = tostring(a.name):lower(), tostring(b.name):lower()
		if an ~= bn then return an < bn end
		return tostring(a.name) < tostring(b.name)
	end)
	local texts = {}
	for i, r in ipairs(order) do
		if r.weight then
			texts[i] = string.format("%s  x%d  %.2f kg", r.name, r.amount, r.weight)
		else
			texts[i] = string.format("%s  x%d  ?", r.name, r.amount)
		end
	end
	local header = string.format("Inventory  %d kinds, %d items, %.2f kg", #order, itemCount, totalWeight)
	return order, texts, header, itemCount, (os.clock() - t0) * 1000
end

-- Draw one frame: the header and ROWS_DRAWN rows from the scroll offset, left side, white, every frame.
function Deagle:InvnTick(token)
	local invn = self.invn
	if not invn or not invn.open or token ~= invn.token then return end   -- closed / a newer loop took over
	local t0 = os.clock()
	local ok, vx, vy, vw, vh = pcall(System.GetViewport)
	local W = (ok and type(vw) == "number" and vw > 0) and vw or 1920
	local H = (ok and type(vh) == "number" and vh > 0) and vh or 1080
	local hud = self.cfg.hud or {}
	local font = hud.font or "default"
	local size = tonumber(hud.size) or 1.4
	local x, y0, pitch = 0.06 * W, 0.12 * H, 0.019 * H
	local rows, texts, off = invn.rows, invn.rowText, invn.offset
	local function draw(y, s) pcall(System.DrawText, x, y, s, font, size, 0, 1, 1, 1, 1) end
	draw(y0 - pitch, invn.header)
	local last = math.min(ROWS_DRAWN, #rows - off)
	for i = 1, last do draw(y0 + (i - 1) * pitch, texts[off + i]) end
	local drawMs = (os.clock() - t0) * 1000
	if invn.logPending then
		invn.logPending = false
		local firstMs = (os.clock() - (invn.openedAt or t0)) * 1000
		log(string.format("invn: open: read %.2f ms (%d items, %d kinds), first frame %.1f ms after the command, draw %.2f ms",
			invn.readMs or 0, invn.items or 0, #rows, firstMs, drawMs))
	end
	Script.SetTimer(0, invn.tickFn)
end

function Deagle:InvnOpen()
	local invn = self.invn
	-- the item names / weights (invnative_data.lua, 690 KB, ~30 ms to load): once, at the first open, not at start-up
	if not self.invData and not invn.dataTried then
		invn.dataTried = true
		local t = os.clock()
		self:LoadModule("Scripts/Deagle/invnative_data.lua")
		log(string.format("invn: item data %s in %.0f ms", self.invData and "loaded" or "MISSING", (os.clock() - t) * 1000))
	end
	local rows, texts, header, itemCount, readMs = self:InvnRead()
	invn.rows, invn.rowText, invn.header = rows, texts, header
	invn.items, invn.readMs = itemCount, readMs
	invn.offset = math.min(invn.offset or 0, math.max(0, #rows - ROWS_DRAWN))
	invn.openedAt, invn.logPending = os.clock(), true
	if invn.open then return true end   -- already drawing: new data, same loop (never a second loop)
	invn.open = true
	invn.token = (invn.token or 0) + 1
	local token = invn.token
	invn.tickFn = function() Deagle:InvnTick(token) end   -- one closure per open, not one per frame
	Script.SetTimer(0, invn.tickFn)
	return true
end

function Deagle:InvnClose()
	local invn = self.invn
	if not invn then return false end
	invn.token = (invn.token or 0) + 1   -- the stored tick sees a stale token and does not re-arm
	invn.open, invn.logPending = false, false
	invn.rows, invn.rowText, invn.header = nil, nil, nil
	invn.offset = 0
	return true
end

function Deagle:InvnToggle()
	if self.invn and self.invn.open then return self:InvnClose() end
	return self:InvnOpen()
end

-- Scroll by n rows from the current offset, clamped to the first row and to the last full page.
function Deagle:InvnScroll(n)
	local invn = self.invn
	if not invn or not invn.open then return 0 end
	local maxOffset = math.max(0, #(invn.rows or {}) - ROWS_DRAWN)
	local step = math.floor(tonumber(n) or 0)
	invn.offset = math.max(0, math.min(maxOffset, (invn.offset or 0) + step))
	return invn.offset
end

function Deagle:InvnCmd(arg)
	arg = tostring(arg or ""):lower()
	if arg == "" or arg == "toggle" then
		self:InvnToggle()
	elseif arg == "on" or arg == "1" then
		self:InvnOpen()
	elseif arg == "off" or arg == "0" then
		self:InvnClose()
	elseif arg:match("^scroll") then
		local n = tonumber(arg:match("^scroll%s+(%-?%d+)"))
		if n then self:InvnScroll(n) else log("invn: scroll needs a number") end
	else
		log("invn: usage: deagle_invn [on|off|toggle|scroll <n>]")
	end
end
