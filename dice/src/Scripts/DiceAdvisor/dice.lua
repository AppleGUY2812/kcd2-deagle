DiceAdvisor = DiceAdvisor or {
	enabled = 1,
	logging = 1,
	detail = 0,
	ui = 1,   -- 1 = HUD panel (default), 0 = the old centre info text; session only
	generation = 0,
	marked = {},
	lines = nil,
	panel = nil,
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

-- Normalised side weights of a dice_items entry ("6 1 6 1 6 1") as six probabilities.
local function normalised(w)
	local out, sum = {}, 0
	for v in tostring(w or ""):gmatch("[%d%.]+") do
		v = tonumber(v) or 0
		out[#out + 1] = v
		sum = sum + v
	end
	if sum > 0 then for i = 1, #out do out[i] = out[i] / sum end end
	return out
end

-- The dice_items name of the die with these six probabilities and joker flag (swapp from the plugin).
local function dieName(sp)
	local p = {}
	for v in tostring(sp or ""):gmatch("[^,]+") do p[#p + 1] = tonumber(v) or 0 end
	if #p < 7 then return "that die" end
	local best
	for _, d in pairs(DiceAdvisorItems or {}) do
		if (tonumber(d.j) or 0) == p[7] then
			local w = normalised(d.w)
			local diff = 0
			for i = 1, 6 do diff = math.max(diff, math.abs((w[i] or 0) - p[i])) end
			if diff < 0.01 and (not best or diff < best[2]) then best = { d.n, diff } end
		end
	end
	return best and best[1]:gsub(" die$", "") or "that die"
end

function DiceAdvisor:Clear()
	for _, id in ipairs(self.marked) do hud("RemoveDiceSelector", id) end
	self.marked = {}
	self.text = nil   -- the same words can belong to a different roll, so the selectors must be drawn again
end

-- The HUD panel draws the advice next to the game's score panel. The info text is the fallback (dice_advisor_ui 0).
function DiceAdvisor:Render()
	if self.ui == 1 then
		local panel = self.panel
		if panel then
			hud("DiceAdvice", panel.mode or 0, panel.faces or "", panel.action or "", panel.line2 or "", panel.line3 or "")
		else
			hud("DiceAdvice", 0, "", "", "", "")
		end
		return
	end
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
	self.panel = nil
	self.mistakeLine = nil
	self:Render()
end

function DiceAdvisor:Busy(line)
	local generation = tonumber(arg(line))
	if not generation or not self.enabled or generation < self.generation then return end
	self.generation = generation
	self:Clear()
	self.pickerOpen = false
	self.lines = { "Calculating..." }
	self.panel = { mode = 1 }
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
	-- The plugin draws the glow markers for the dice to set aside (HUD ids 100+); Lua only names the faces here.

	-- What the player can't see: which dice to take, bank or throw, what throwing on risks and why. The game already
	-- shows the points, the turn total and a bust, so none of that is repeated (user, 2026-10-28).
	local panel = { mode = 0 }
	local lines = {}
	local action = num(t, "bank") == 1 and "BANK" or "THROW AGAIN"
	local line2, line3
	if keep ~= 0 then
		local pickScores = num(t, "pick") == 1 and held ~= 0
		local faces = {}
		for i = 0, 5 do if bit(keep, i) then faces[#faces + 1] = face(i) end end
		panel.faces = table.concat(faces, ",")
		local now = held ~= 0 and held == keep
		panel.action = (now and "Now " or "then ") .. action
		if now then
			lines[1] = highlight("Now " .. action)
		elseif pickScores then
			lines[1] = highlight("Better: take " .. list(keep) .. ", then " .. action)
		else
			lines[1] = highlight("Take " .. list(keep) .. ", then " .. action)
		end
		local bankwin, roll = num(t, "bankwin"), num(t, "roll")
		line2 = string.format("Throwing on: %.0f %% chance to lose %d points", num(t, "abust") * 100, num(t, "bpts"))
		lines[2] = line2
		-- The Why row: the first rule that applies (brief order), differences in percentage points.
		if t.swap and t.swapp and math.max(bankwin, roll) - num(t, "swapwin") >= 0.005 then
			line3 = string.format("Why: keeps your %s in hand (+%.1f %% win)", dieName(t.swapp),
				(math.max(bankwin, roll) - num(t, "swapwin")) * 100)
		elseif num(t, "tf") == 1 and num(t, "tfwin") - num(t, "tallwin") >= 0.005 then
			line3 = string.format("Why: taking all (%s) wins %.1f %% less", list(num(t, "tallkeep")),
				(num(t, "tfwin") - num(t, "tallwin")) * 100)
		elseif math.abs(bankwin - roll) >= 0.005 then
			line3 = string.format(num(t, "bank") == 1 and "Why: throwing on wins %.1f %% less" or
				"Why: banking now wins %.1f %% less", math.abs(bankwin - roll) * 100)
		else
			line3 = "Close call: bank or throw, under 0.5 % apart"
		end
		if self.detail == 1 then
			local detail = string.format("Win chance: bank %.0f %%, throw %.0f %%   Opponent wins next turn %.0f %%",
				bankwin * 100, roll * 100, num(t, "opp") * 100)
			lines[3] = detail
			line3 = line3 .. "\n" .. detail
		end
	end
	local mistake = num(t, "mist")
	self.mistakeLine = self.detail == 1 and mistake >= 0.05 and string.format("Your last choice cost %.1f %% win chance", mistake) or nil
	if self.mistakeLine then
		if line3 then line3 = line3 .. "\n" .. self.mistakeLine
		elseif line2 then line3 = self.mistakeLine
		else line2 = self.mistakeLine end
	end
	panel.mode = (panel.faces or panel.action or line2 or line3) and 2 or 0
	panel.line2 = line2 or ""
	panel.line3 = line3 or ""
	self.panel = panel
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
	self.panel = { mode = 3, line2 = "Finding your best 6 dice..." }
	self:Render()
	self:SendOwned()
end

function DiceAdvisor:Best(line)
	local seq, counts, points, base = arg(line):match("^(%d+)%s+(%S+)%s+(%-?%d+)%s*(%-?%d*)")
	if tonumber(seq) ~= self.ownSeq or not self.pickerOpen or not self.owned then return end
	local panel = { mode = 3 }
	local lines = {}
	if counts == "none" then
		lines[1] = "Could not rank your dice"
		panel.line2 = lines[1]
		self.panel = panel
		self.lines = lines
		self:Render()
		return
	end
	local picks, kinds, i = {}, {}, 0
	for c in counts:gmatch("%d+") do
		i = i + 1
		local d = self.owned[i]
		local n = tonumber(c)
		if d and n and n > 0 then
			local name = d.n:gsub(" die$", "")
			picks[#picks + 1] = n > 1 and string.format("%s x%d", name, n) or name
			kinds[#kinds + 1] = { d = d, name = name }
		end
	end
	lines[1] = highlight("Bring: " .. table.concat(picks, ", ", 1, math.min(3, #picks)))
	if #picks > 3 then lines[2] = table.concat(picks, ", ", 4) end
	panel.line2 = table.concat(picks, "|")
	-- Why: the chosen die kind with the surest face (its largest normalised side weight), then the points per turn
	-- against the six-ordinary-dice baseline. Skip the trait when no face reaches 1 in 4.
	local trait, bestp, bestface, bestname, bestjoker = nil, -1, 0, nil, 0
	for _, k in ipairs(kinds) do
		local w = normalised(k.d.w)
		local j = tonumber(k.d.j) or 0
		for f = 1, 6 do
			if (w[f] or 0) > bestp then bestp, bestface, bestname, bestjoker = w[f], f, k.name, j end
		end
	end
	local pts, basepts = tonumber(points), tonumber(base)
	if basepts and pts then
		if bestname and bestp >= 0.25 then
			local faceText = (bestjoker == 1 and bestface == 1) and "J" or tostring(bestface)
			local n = math.floor(1 / bestp + 0.5)
			if n < 1 then n = 1 end
			if n == 1 then
				trait = string.format("%s: a %s every throw;", bestname, faceText)
			else
				local ord = n == 2 and "2nd" or (n == 3 and "3rd" or (n .. "th"))
				trait = string.format("%s: a %s every %s throw;", bestname, faceText, ord)
			end
		end
		local pointsText = string.format("%d pts/turn vs %d with plain dice", pts, basepts)
		panel.line3 = trait and ("Why: " .. trait .. "\n" .. pointsText) or ("Why: " .. pointsText)
	end
	self.panel = panel
	self.lines = lines
	self:Render()
end

function DiceAdvisor:NativeOk()
	native("advisor " .. tostring(self.enabled))
	native("log " .. tostring(self.logging))
	native("set detail " .. tostring(self.detail))
	native("marks " .. (self.ui == 1 and 1 or 0))
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

-- Session only (not saved): 1 = the HUD panel next to the game's score panel (default), 0 = the old centre info text.
function DiceAdvisor:SetUi(line)
	local n = tonumber(arg(line))
	if n ~= 0 and n ~= 1 then System.LogAlways("[Dice Advisor] usage: dice_advisor_ui 0|1") return end
	self.ui = n
	self.text = nil
	native("marks " .. n)
	if n == 0 then
		hud("DiceAdvice", 0, "", "", "", "")
	else
		hud("HideInfoText")
	end
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
System.AddCCommand("dice_advisor_ui", "DiceAdvisor:SetUi([[%line]])", "dice_advisor_ui 0|1: HUD panel (default) or the old centre info text (session only)")
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
