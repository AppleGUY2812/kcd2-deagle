-- Native inventory list (src/Scripts/Deagle/invnative.lua): a fake inventory of 1200 item ids in 300 classes, some with
-- Deagle.invData rows and some without. Checks grouping / sorting / summed amounts / header totals, 30 rows drawn per
-- frame, scroll clamps, close stopping the loop, reopening not doubling it, and no invData at all.
-- usage: luajit tests/run_invnative.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")
dofile("src/Scripts/Deagle/deagle_main.lua")

local n, fails = 0, 0
local function check(label, ok, detail)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. (ok and "" or ("  " .. tostring(detail))))
end

-- the fake inventory: 300 classes x 4 item ids, amounts 1..4 (10 per class, 3000 in total)
local NCLASS, PERCLASS = 300, 4
local ids, itemById = {}, {}
for c = 1, NCLASS do
	for k = 1, PERCLASS do
		local id = #ids + 1
		ids[#ids + 1] = id
		itemById[id] = { id = id, class = string.format("GUID%04d", c), amount = k, health = 100 }
	end
end
_G.ItemManager = {
	GetItem = function(id) return itemById[id] end,
	GetItemName = function(class) return string.format("internal%04d", tonumber(tostring(class):match("%d+"))) end,
	GetItemUIName = function(class) return "ui_nm_" .. tostring(class) end,
}
local player = g_localActor
player.inventory.GetInventoryTable = function() return ids end

-- names / weights: every 3rd class has no invData row
local data, expectedWeight = {}, 0
for c = 1, NCLASS do
	local w = 0.5 + (c % 4) * 0.25
	if c % 3 ~= 0 then
		data[string.format("guid%04d", c)] = { n = string.format("Item %03d", c), w = w, p = c, c = "misc", i = "" }
		expectedWeight = expectedWeight + 10 * w
	end
end
Deagle.invData = data

-- the expected rows: grouped, named, sorted (case-insensitively) like the file must do
local expected = {}
for c = 1, NCLASS do
	expected[#expected + 1] = { key = string.format("guid%04d", c),
		name = (c % 3 ~= 0) and string.format("Item %03d", c) or string.format("internal%04d", c),
		amount = 10, weight = (c % 3 ~= 0) and 10 * (0.5 + (c % 4) * 0.25) or nil }
end
table.sort(expected, function(a, b)
	local an, bn = a.name:lower(), b.name:lower()
	if an ~= bn then return an < bn end
	return a.name < b.name
end)

-- DrawText capture: arguments + the mock's call count
local draws = {}
System.DrawText = function(x, y, text, font, size, a, r, g, b, al)
	draws[#draws + 1] = { x = x, y = y, text = text, font = font, size = size, r = r, g = g, b = b, a = al }
	M.drawn_text = M.drawn_text + 1
	return true
end
local function frame()
	local before = M.drawn_text
	M.advance(1 / 60)
	return M.drawn_text - before
end

-- open and the first frame
local logsBefore = #M.logs
Deagle:InvnOpen()
local first = frame()
local rows = Deagle.invn.rows or {}
check("one row per class (300)", #rows == NCLASS, #rows)
local grouped, sorted = true, true
for i = 1, #expected do
	local r, e = rows[i], expected[i]
	if not r or r.key ~= e.key or r.amount ~= e.amount then grouped = false break end
end
for i = 2, #rows do
	if tostring(rows[i - 1].name):lower() > tostring(rows[i].name):lower() then sorted = false break end
end
check("rows grouped by class, amounts summed (10 each)", grouped, grouped and "" or (rows[1] and rows[1].key))
check("rows sorted by English name", sorted, sorted and "" or (rows[1] and rows[1].name))
local amountTotal = 0
for _, r in ipairs(rows) do amountTotal = amountTotal + r.amount end
check("all amounts summed (3000 items from 1200 ids)", amountTotal == 3000, amountTotal)
local header = draws[1] and draws[1].text or ""
local kinds, items, kg = header:match("^Inventory%s+(%d+) kinds, (%d+) items, ([%d%.]+) kg$")
check("header: kinds / items / total weight", tonumber(kinds) == 300 and tonumber(items) == 3000
	and tonumber(kg) and math.abs(tonumber(kg) - expectedWeight) < 0.011, header)
local rowDraws = first - 1
check("30 rows drawn in one frame (31 DrawText calls with the header)", rowDraws == 30 and first == 31, first .. " calls")
local pitch, x = 0.019 * 1080, 0.06 * 1920
local placed, white = true, true
for i = 1, 30 do
	local d = draws[i + 1]
	if not d or math.abs(d.x - x) > 1e-9 or math.abs(d.y - (0.12 * 1080 + (i - 1) * pitch)) > 1e-9 then placed = false end
	if d and not (d.r == 1 and d.g == 1 and d.b == 1 and d.a == 1) then white = false end
end
check("rows at x = 0.06 W, 1.9 % H apart from y = 0.12 H, white", placed and white, tostring(placed) .. "/" .. tostring(white))

-- one open log with numbers
local openLogs = {}
for i = logsBefore + 1, #M.logs do if M.logs[i]:find("invn: open:") then openLogs[#openLogs + 1] = M.logs[i] end end
local readMs, logItems, logKinds, firstMs, drawMs = (openLogs[1] or ""):match(
	"invn: open: read ([%d%.]+) ms %((%d+) items, (%d+) kinds%), first frame ([%d%.]+) ms after the command, draw ([%d%.]+) ms")
check("logged once per open with read / first frame / draw times", #openLogs == 1 and tonumber(logItems) == 3000
	and tonumber(logKinds) == 300 and tonumber(readMs) >= 0 and tonumber(firstMs) >= 0 and tonumber(drawMs) >= 0,
	"logs=" .. #openLogs .. " " .. (openLogs[1] or "none"))

-- scroll clamps at both ends
draws = {}
Deagle:InvnScroll(99999)
check("scroll clamps at the last page (offset 270)", Deagle.invn.offset == 270, Deagle.invn.offset)
frame()
check("last page drawn: the first row is row 271", (draws[2] and draws[2].text) == Deagle.invn.rowText[271],
	draws[2] and draws[2].text)
draws = {}
Deagle:InvnScroll(-99999)
check("scroll clamps at the top (offset 0)", Deagle.invn.offset == 0, Deagle.invn.offset)
frame()
check("top page drawn again: the first row is row 1", (draws[2] and draws[2].text) == Deagle.invn.rowText[1],
	draws[2] and draws[2].text)

-- close stops the loop
Deagle:InvnClose()
local before = M.drawn_text
M.advance(5 / 60)
check("close stops the loop (no draws after)", M.drawn_text == before, M.drawn_text - before .. " draws")

-- reopening never starts a second loop
Deagle:InvnOpen()
check("reopen draws one frame (31 calls)", frame() == 31, M.drawn_text - before)
Deagle:InvnOpen()
check("open while already open keeps one loop (31 calls)", frame() == 31, M.drawn_text - before)
Deagle:InvnClose(); Deagle:InvnOpen()
check("close + open keeps one loop (31 calls)", frame() == 31, M.drawn_text - before)

-- Deagle.invData = nil: internal names and unknown weights
Deagle.invData = nil
Deagle:InvnOpen()
draws = {}
local nilFirst = frame()
local internal, unknown = true, true
for _, r in ipairs(Deagle.invn.rows or {}) do if r.name ~= string.format("internal%04d", tonumber(r.key:match("%d+"))) then internal = false end end
for _, t in ipairs(Deagle.invn.rowText or {}) do if not t:find("%?$") then unknown = false end end
local nilHeader = draws[1] and draws[1].text or ""
check("invData nil: 300 rows / 30 drawn still work", #Deagle.invn.rows == 300 and nilFirst == 31, nilFirst)
check("invData nil: internal GetItemName names and weight ?", internal and unknown, nilHeader)
check("invData nil: header has 0.00 kg (no known weights)", nilHeader:find("0%.00 kg$") ~= nil, nilHeader)

check("no Lua errors", #M.errors == 0, table.concat(M.errors, " | "))
print(string.format("invnative: %d/%d", n - fails, n))
os.exit(fails == 0 and 0 or 1)
