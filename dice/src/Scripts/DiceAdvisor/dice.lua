DiceAdvisor = DiceAdvisor or {
	enabled = 1,
	logging = 1,
	detail = 0,
	generation = 0,
	marked = {},
	lines = nil,
	mistakeLine = nil,
	text = "",
}

local function arg(value)
	return (tostring(value or ""):gsub('"', "")):match("^%s*(.-)%s*$")
end

local function native(command)
	System.LogAlways("[DICE] " .. tostring(command))
end

local function hud(name, ...)
	if type(UIAction) == "table" and UIAction.CallFunction then
		UIAction.CallFunction("hud", -1, name, ...)
	end
end

local function parse(line)
	local t = {}
	for key, value in arg(line):gmatch("([%w_]+):([^%s]+)") do t[key] = value end
	return t
end

local function num(t, key, fallback)
	return tonumber(t[key]) or fallback or 0
end

local function bit(mask, i)
	return math.floor(mask / 2 ^ i) % 2 == 1
end

local function count(mask)
	local n = 0
	for i = 0, 5 do if bit(mask, i) then n = n + 1 end end
	return n
end

local function highlight(text)
	return '<b><font color="#fff2a8">' .. text .. '</font></b>'
end

function DiceAdvisor:Clear()
	for _, id in ipairs(self.marked) do hud("RemoveDiceSelector", id) end
	self.marked = {}
	self.text = nil   -- the same words can belong to a different roll, so the selectors must be drawn again
end

-- One existing HUD text: HideInfoText flushes, ShowInfoText draws the mc_InfoText panel with the game font and the
-- ornament background. It is centered a little below the screen center and holds six short lines.
function DiceAdvisor:Render()
	local all = {}
	for _, line in ipairs(self.lines or {}) do all[#all + 1] = line end
	if self.mistakeLine then all[#all + 1] = self.mistakeLine end
	local text = table.concat(all, "<br/>")
	if text == self.text then return end
	self.text = text
	hud("HideInfoText")
	if text ~= "" then
		hud("ShowInfoText", '<p align="center"><font face="DefaultFont" size="16" color="#f6e890">' ..
			text .. '</font></p>', 0, -1, true)
	end
end

function DiceAdvisor:Hide(line)
	local generation = tonumber(arg(line))
	if generation then
		if self.generation > 0 and generation < self.generation then return end
		self.generation = generation
	else
		self.generation = 0
	end
	self:Clear()
	self.pickerOpen = false
	self.lines = nil
	self.mistakeLine = nil
	self:Render()
end

function DiceAdvisor:Busy(line)
	local generation = tonumber(arg(line))
	if not generation or not self.enabled or generation < self.generation then return end
	self.generation = generation
	self:Clear()
	self.pickerOpen = false
	self.lines = { "Calculating...", "(first throw with these dice and this target - then it is instant)" }
	self.mistakeLine = nil
	self:Render()
end

-- The plugin sends the advice in short dice_part lines as key:value (the console reads "a=b" as a
-- cvar assignment), the first carrying gen:; dice_show gen:N then applies the collected fields.
function DiceAdvisor:Part(line)
	local t = parse(line)
	if t.gen then self.part = {} end
	self.part = self.part or {}
	for k, v in pairs(t) do self.part[k] = v end
end

function DiceAdvisor:Show(line)
	local t = parse(line)
	if self.part and self.part.gen == t.gen then
		for k, v in pairs(self.part) do if t[k] == nil then t[k] = v end end
	end
	self.part = nil
	local generation = tonumber(t.gen)
	if not generation or not self.enabled or generation < self.generation then return end
	self.generation = generation
	self:Clear()
	self.pickerOpen = false

	local dice = {}
	for i = 0, 5 do
		local x, y, value, joker = (t["d" .. i] or ""):match("([^,]+),([^,]+),([^,]+),([^,]+)")
		dice[i] = {
			x = tonumber(x), y = tonumber(y),
			value = tonumber(value) or 0, joker = joker == "1",
		}
	end
	local function face(i)
		return dice[i].value == 6 and "J" or tostring(dice[i].value + 1)
	end
	local function list(mask)
		local out = {}
		for i = 0, 5 do if bit(mask, i) then out[#out + 1] = face(i) end end
		table.sort(out)
		return #out > 0 and table.concat(out, " ") or "nothing"
	end

	local keep = num(t, "keep")
	local held = num(t, "held")
	-- No dice markers: the game's selector drew blue arcs away from the dice (in game 2026-10-28). The text names the dice.

	-- What the player can't see: which dice to take, bank or throw, and what throwing on risks. The game already shows
	-- the points, the turn total and a bust, so none of that is repeated (user, 2026-10-28).
	local lines = {}
	local action = num(t, "bank") == 1 and "BANK" or "THROW AGAIN"
	if keep ~= 0 then
		local pickScores = num(t, "pick") == 1 and held ~= 0
		if held ~= 0 and held == keep then
			lines[1] = highlight("Now " .. action)
		elseif pickScores then
			lines[1] = highlight("Better: take " .. list(keep) .. ", then " .. action)
		else
			lines[1] = highlight("Take " .. list(keep) .. ", then " .. action)
		end
		lines[2] = string.format("Throwing on: %.0f %% chance to lose %d points", num(t, "abust") * 100, num(t, "bpts"))
		if self.detail == 1 then
			lines[3] = string.format("Win chance: bank %.0f %%, throw %.0f %%   Opponent wins next turn %.0f %%",
				num(t, "bankwin") * 100, num(t, "roll") * 100, num(t, "opp") * 100)
		end
	end
	local mistake = num(t, "mist")
	self.mistakeLine = self.detail == 1 and mistake >= 0.05 and string.format("Your last choice cost %.1f %% win chance", mistake) or nil
	self.lines = lines
	self:Render()
end

-- ---- Dice picker: the best 6 of the player's dice, shown on the dice-picking screen (the plugin ranks them with
-- tools/farkle/bestset.h). Dice are inventory items; dice_items.lua maps item classes to their side weights.
function DiceAdvisor:OwnedDice()
	local byKey, list, ordinary = {}, {}, false
	pcall(function()
		local inv = player and player.inventory
		for _, id in pairs(inv and inv:GetInventoryTable() or {}) do
			local it = ItemManager.GetItem(id)
			local d = it and DiceAdvisorItems and DiceAdvisorItems[string.lower(tostring(it.class))]
			if d then
				local key = d.n .. "|" .. d.w .. "|" .. d.j
				local e = byKey[key]
				if not e then e = { n = d.n, w = d.w, j = d.j, c = 0 }; byKey[key] = e; list[#list + 1] = e end
				e.c = e.c + math.max(1, math.floor(tonumber(it.amount) or 1))
				if d.n == "Ordinary die" then ordinary = true end
			end
		end
	end)
	-- Assumed: a set is filled up with ordinary dice, so six of them are always available.
	if not ordinary then list[#list + 1] = { n = "Ordinary die", w = "1 1 1 1 1 1", j = 0, c = 6 } end
	table.sort(list, function(a, b) return a.n < b.n end)
	return list
end

-- Sends the collection to the plugin; it answers with dice_best <seq> (at once when this collection was ranked before).
function DiceAdvisor:SendOwned()
	self.owned = self:OwnedDice()
	self.ownSeq = (self.ownSeq or 0) + 1
	local parts = {}
	for i, d in ipairs(self.owned) do
		parts[i] = string.format("%d,%s,%d", d.c, (d.w:gsub(" ", ",")), d.j)
	end
	native("own " .. self.ownSeq .. " " .. table.concat(parts, ";"))
end

function DiceAdvisor:Picker(line)
	local generation = tonumber(arg(line))
	if not self.enabled or (generation and generation < self.generation) then return end
	if generation then self.generation = generation end
	self:Clear()
	self.pickerOpen = true
	self.mistakeLine = nil
	self.lines = { "Finding your best 6 dice..." }
	self:Render()
	self:SendOwned()
end

function DiceAdvisor:Best(line)
	local seq, counts, points = arg(line):match("^(%d+)%s+(%S+)%s+(%-?%d+)")
	if tonumber(seq) ~= self.ownSeq or not self.pickerOpen or not self.owned then return end
	local lines = {}
	if counts == "none" then
		lines[1] = "Could not rank your dice"
	else
		local picks, i = {}, 0
		for c in counts:gmatch("%d+") do
			i = i + 1
			local d = self.owned[i]
			if d and tonumber(c) > 0 then
				local name = d.n:gsub(" die$", "")
				picks[#picks + 1] = tonumber(c) > 1 and string.format("%s x%s", name, c) or name
			end
		end
		lines[1] = highlight("Bring: " .. table.concat(picks, ", ", 1, math.min(3, #picks)))
		if #picks > 3 then lines[2] = table.concat(picks, ", ", 4) end
	end
	self.lines = lines
	self:Render()
end

function DiceAdvisor:NativeOk()
	native("advisor " .. tostring(self.enabled))
	native("log " .. tostring(self.logging))
	native("set detail " .. tostring(self.detail))
end

function DiceAdvisor:SetEnabled(line)
	local n = tonumber(arg(line))
	if n ~= 0 and n ~= 1 then System.LogAlways("[Dice Advisor] usage: dice_advisor 0|1") return end
	self.enabled = n
	if n == 0 then self:Hide() end
	native("advisor " .. n)
	native("set advisor " .. n)
end

function DiceAdvisor:SetLogging(line)
	local n = tonumber(arg(line))
	if n ~= 0 and n ~= 1 then System.LogAlways("[Dice Advisor] usage: dice_advisor_log 0|1") return end
	self.logging = n
	native("log " .. n)
	native("set log " .. n)
end

function DiceAdvisor:SetDetail(line)
	local n = tonumber(arg(line))
	if n ~= 0 and n ~= 1 then System.LogAlways("[Dice Advisor] usage: dice_advisor_detail 0|1") return end
	self.detail = n
	native("set detail " .. n)
	self.text = nil
	self:Render()
end

function DiceAdvisor:Set(line)
	local key, value = arg(line):match("^(%S+)%s+(%S+)$")
	if key == "advisor" and (value == "0" or value == "1") then self.enabled = tonumber(value)
	elseif key == "log" and (value == "0" or value == "1") then self.logging = tonumber(value)
	elseif key == "detail" and (value == "0" or value == "1") then self.detail = tonumber(value) end
	self:NativeOk()
	self.text = nil
	self:Render()
end

function DiceAdvisor:Hello()
	self:Hide()
	native("hello")
	self:NativeOk()
	self:SendOwned()   -- rank the collection in the background, so the picker has the answer at once
end

function DiceAdvisor:OnGameplayStarted()
	self:Hello()
end

System.AddCCommand("dice_advisor", "DiceAdvisor:SetEnabled([[%line]])", "dice_advisor 0|1: show the best Farkle move (saved)")
System.AddCCommand("dice_advisor_log", "DiceAdvisor:SetLogging([[%line]])", "dice_advisor_log 0|1: log every live dice state (saved)")
System.AddCCommand("dice_advisor_detail", "DiceAdvisor:SetDetail([[%line]])", "dice_advisor_detail 0|1: extra numbers: take fewer, opponent, average points (saved)")
System.AddCCommand("dice_native_ok", "DiceAdvisor:NativeOk()", "internal: native plugin handshake")
System.AddCCommand("dice_set", "DiceAdvisor:Set([[%line]])", "internal: replay a saved setting")
System.AddCCommand("dice_busy", "DiceAdvisor:Busy([[%line]])", "internal: solver busy")
System.AddCCommand("dice_part", "DiceAdvisor:Part([[%line]])", "internal: advice fields")
System.AddCCommand("dice_picker", "DiceAdvisor:Picker([[%line]])", "internal: dice-picking screen opened")
System.AddCCommand("dice_best", "DiceAdvisor:Best([[%line]])", "internal: best dice to bring")
System.AddCCommand("dice_show", "DiceAdvisor:Show([[%line]])", "internal: show advice")
System.AddCCommand("dice_hide", "DiceAdvisor:Hide([[%line]])", "internal: hide advice")

if type(UIAction) == "table" and UIAction.RegisterEventSystemListener then
	UIAction.RegisterEventSystemListener(DiceAdvisor, "System", "OnGameplayStarted", "OnGameplayStarted")
end
DiceAdvisor:Hello()
