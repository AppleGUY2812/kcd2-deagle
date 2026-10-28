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
	self.lines = nil
	self.mistakeLine = nil
	self:Render()
end

function DiceAdvisor:Busy(line)
	local generation = tonumber(arg(line))
	if not generation or not self.enabled or generation < self.generation then return end
	self.generation = generation
	self:Clear()
	self.lines = { "Calculating..." }
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
		return #out > 0 and table.concat(out, " ") or "nothing"
	end

	local keep = num(t, "keep")
	local held = num(t, "held")
	-- No dice markers: the game's selector drew blue arcs away from the dice (in game 2026-10-28). The text names the dice.

	local lines = {}
	local function choice(bankBest, bankWin, throwWin)
		lines[#lines + 1] = highlight(bankBest and "Then BANK" or "Then THROW AGAIN")
		lines[#lines + 1] = string.format("Win chance if you bank %.0f %%, if you throw %.0f %%", bankWin, throwWin)
	end
	local hasPick = num(t, "pick") == 1 and held ~= 0
	if hasPick then
		local left = num(t, "pleft")
		lines[#lines + 1] = string.format("Your pick: %s = %d points", list(held), num(t, "pscore"))
		local throwWin, bankWin = num(t, "proll") * 100, num(t, "pbank") * 100
		choice(bankWin >= throwWin, bankWin, throwWin)
		lines[#lines + 1] = string.format("Chance to score nothing with the %d %s left: %.0f %%",
			left, left == 1 and "die" or "dice", num(t, "pbust") * 100)
	elseif keep == 0 then
		lines[#lines + 1] = "No scoring dice: this turn is lost"
	else
		lines[#lines + 1] = string.format("Take %s = %d points", list(keep), num(t, "score"))
		if held ~= 0 and num(t, "pick") == 0 then
			lines[#lines + 1] = "Your current pick does not score"
		end
		choice(num(t, "bank") == 1, num(t, "bankwin") * 100, num(t, "roll") * 100)
	end
	if self.detail == 1 then
		lines[#lines + 1] = string.format("Win this turn %.0f %%   Opponent wins next turn %.0f %%",
			num(t, hasPick and "pfin" or "finish") * 100, num(t, "opp") * 100)
	end
	local mistake = num(t, "mist")
	self.mistakeLine = mistake >= 0.05 and string.format("Your last choice cost %.1f %% win chance", mistake) or nil
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
System.AddCCommand("dice_show", "DiceAdvisor:Show([[%line]])", "internal: show advice")
System.AddCCommand("dice_hide", "DiceAdvisor:Hide([[%line]])", "internal: hide advice")

if type(UIAction) == "table" and UIAction.RegisterEventSystemListener then
	UIAction.RegisterEventSystemListener(DiceAdvisor, "System", "OnGameplayStarted", "OnGameplayStarted")
end
DiceAdvisor:Hello()
