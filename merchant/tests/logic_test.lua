-- merchant.lua against a stand-in game (no game needed): luajit merchant/tests/logic_test.lua
local logs = {}
local MONEY = "5ef63059-322e-4e1b-abe8-926e100c770e"
local worldTime, realTime = 136 * 86400, 0
local timers = {}

-- an inventory: items {id, class, amount}; money is a class like any other (deci units, as in the game)
local function Inventory(money)
	local inv = { items = {}, next = 1 }
	function inv:Add(class, amount, id, health, owned)
		id = id or string.format("userdata: 02%014X", self.next); self.next = self.next + 1
		self.items[#self.items + 1] = { id = id, class = class, amount = amount, health = health or 1, owned = owned }
		return id
	end
	function inv:GetMoney() return self:GetCountOfClass(MONEY) / 10 end
	function inv:GetCountOfClass(c) local n = 0; for _, it in ipairs(self.items) do if it.class == c then n = n + it.amount end end; return n end
	function inv:CreateItem(c, health, amount) self:Add(c, amount) end
	function inv:DeleteItem(id, n)
		for i, it in ipairs(self.items) do if it.id == id then
			if it.owned then return end          -- the game skips items another inventory owns (sold to the merchant)
			it.amount = it.amount - n; if it.amount <= 0 then table.remove(self.items, i) end; return end end
	end
	function inv:DeleteItemOfClass(c, n)
		for i = #self.items, 1, -1 do local it = self.items[i]
			if n <= 0 then break end
			if it.class == c then local k = math.min(n, it.amount); it.amount = it.amount - k; n = n - k; if it.amount <= 0 then table.remove(self.items, i) end end
		end
	end
	function inv:GetInventoryTable() local t = {}; for _, it in ipairs(self.items) do t[#t + 1] = it.id end; return t end
	if money and money > 0 then inv:Add(MONEY, money) end
	return inv
end

local stash = { id = "stash520", class = "Stash", inventory = Inventory(3780), pos = { x = 2291, y = 2056, z = 0 } }
function stash:GetWorldPos() return self.pos end
local empty = { id = "stash520b", class = "Stash", inventory = Inventory(0), pos = { x = 2300, y = 2050, z = 0 } }
function empty:GetWorldPos() return self.pos end
local keeper = { id = "keeper520", class = "NPC", pos = { x = 2292, y = 2057, z = 0 } }
function keeper:GetWorldPos() return self.pos end
local entities = { stash, empty, keeper }
local ITEMS = {}

System = {
	LogAlways = function(l) logs[#logs + 1] = l end,
	AddCCommand = function() end,
	GetCurrTime = function() return realTime end,
	GetEntitiesInSphere = function() return entities end,
}
Calendar = { GetWorldTime = function() return worldTime end }
Shops = {
	GetShopDBIdByKeeper = function(id) return (id == "stash520" or id == "stash520b" or id == "keeper520") and 520 or -1 end,
	IsShopOpened = function() return false end,
}
local removeWorks = true
ItemManager = { GetItem = function(id)
	for _, s in ipairs({ stash, empty }) do for _, it in ipairs(s.inventory.items) do
		if it.id == id then return { class = it.class, amount = it.amount, health = it.health, id = id } end end end
end, RemoveItem = function(id)                -- deletes the whole stack from its owner, wherever it is
	if not removeWorks then return end
	for _, s in ipairs({ stash, empty }) do for i, it in ipairs(s.inventory.items) do
		if it.id == id then table.remove(s.inventory.items, i); return end end end
end }
Script = { SetTimer = function(ms, fn) timers[#timers + 1] = fn end }
g_localActor = { AI = {}, GetWorldPos = function() return { x = 2292, y = 2056, z = 0 } end }
local queue = ""
MRN = { pull = function() local q = queue; queue = ""; return q ~= "" and q or nil end, enabled = function() return true end }

dofile("merchant/src/Scripts/MerchantResale/categories.lua")
dofile("merchant/src/Scripts/MerchantResale/merchant.lua")
local MR = MerchantResale
local function tick() realTime = realTime + 30; local t = timers; timers = {}; for _, f in ipairs(t) do f() end end
local function has(text) for _, l in ipairs(logs) do if l:find(text, 1, true) then return true end end end
local checks = 0
local function check(ok, what) checks = checks + 1; if not ok then for _, l in ipairs(logs) do print(l) end; error("FAIL: " .. what, 2) end end

-- pure model: one plate in a fresh market resells in 3 days +-30%; 30 plates floor the demand
local S = MR.NewState()
MR.Observe(S, 1, 0, 0, 1000)
local PLATE = "07da54a0-0000-0000-0000-000000000001"   -- not in the table: misc; force armor below
MerchantResaleCategories[PLATE] = "armor"
local r1 = MR.AddSale(S, 1, PLATE, 1, 12000, "AA", 0)
check(r1.cat == "armor" and r1.eta >= 2.1 and r1.eta <= 3.9, "fresh plate eta " .. r1.eta)
for i = 1, 29 do MR.AddSale(S, 1, PLATE, 1, 12000, "AB" .. i, 0) end
check(math.abs(MR.DemandAt(S, 1, "armor", 0) - 0.15) < 1e-9, "flood floors demand")
check(S.rows[30].eta > 14, "last flooded plate waits " .. S.rows[30].eta .. " days")
check(MR.DemandAt(S, 1, "armor", 14) > 0.9, "demand recovers in 14 days")
-- a second shop 200 m away shares the market; one 400 m away doesn't
MR.Observe(S, 2, 200, 0, 1000); MR.Observe(S, 3, 0, 400, 1000)
check(S.shops[2].anchor == 1 and S.shops[3].anchor == 3, "markets by distance")
-- save string round trip
local S2 = MR.Deserialize(MR.Serialize(S))
check(#S2.rows == 30 and S2.shops[2].anchor == 1 and math.abs(S2.rows[30].eta - S.rows[30].eta) < 1e-4, "round trip")
-- buyback removes newest rows, no coin
check(MR.Buyback(S, 1, PLATE, 2) == 2 and #S.rows == 28, "buyback")

-- the game loop: the plugin reports a sale (a 10 gr sword, token 02...0BEEF), the item sits in the stash
local SWORD = "11490ea5-ef27-4f8f-a4c9-2b94baf753de"          -- a real MeleeWeapon class in the table
check(MerchantResaleCategories[SWORD] == "weapon", "category table knows a weapon")
tick()                                                        -- first tick: state from the save (empty)
stash.inventory:Add(SWORD, 1, "userdata: 020000000000BEEF")
stash.inventory:DeleteItemOfClass(MONEY, 100)                 -- the shop paid 10 gr for it
queue = "sell 520 " .. SWORD .. " 1 1000 020000000000BEEF\n"
tick()
check(has("shop 520 bought 1x"), "sale recorded")
local St = MR.S
check(#St.rows == 1 and St.rows[1].cat == "weapon" and St.shops[520].last == 3680, "row + purse bookkeeping")
check(g_localActor.AI.MerchantResale:find("R 520", 1, true), "state written into the save table")
-- not due yet: nothing happens
tick()
check(stash.inventory:GetCountOfClass(SWORD) == 1, "not resold early")
-- 5 game days later the sword is gone and the shop got 100 gr (its base price)
worldTime = worldTime + 5 * 86400
tick()
check(stash.inventory:GetCountOfClass(SWORD) == 0, "sword left the stash")
check(math.abs(stash.inventory:GetMoney() - 468.0) < 1e-6, "purse +100 gr: " .. stash.inventory:GetMoney())
check(St.shops[520].bonus == 1000 and #St.rows == 0, "savings 100 gr")
-- the weekly restock resets the purse to its preset (378 gr): the savings come back
stash.inventory:DeleteItemOfClass(MONEY, 900)
tick()
check(math.abs(stash.inventory:GetMoney() - 478.0) < 1e-6, "restock keeps savings: " .. stash.inventory:GetMoney())
-- the player steals 50 gr: no savings come back for that
stash.inventory:DeleteItemOfClass(MONEY, 500)
tick()
check(math.abs(stash.inventory:GetMoney() - 428.0) < 1e-6, "theft not refunded: " .. stash.inventory:GetMoney())
-- first sighting was a drained purse: a rise without a trade teaches the real preset
local S3 = MR.NewState(); local sh = MR.Observe(S3, 9, 0, 0, 200)
check(MR.Restock(sh, 3000) == 0 and sh.preset == 3000, "preset learned from a restock")
-- a sold item that vanished (bought back by class elsewhere / destocked): no coins
queue = "sell 520 " .. SWORD .. " 1 1000 020000000000CAFE\n"
tick()
worldTime = worldTime + 10 * 86400
tick()
check(has("1 gone from stock") and math.abs(stash.inventory:GetMoney() - 428.0) < 1e-6, "gone item pays nothing")
-- loading another save swaps the state
g_localActor.AI = { MerchantResale = MR.Serialize(S) }
tick()
check(MR.S ~= St and #MR.S.rows == 28, "load picks the save's state")
-- what the player sold stays the merchant's: the chest's DeleteItem skips it, ItemManager.RemoveItem takes it
g_localActor.AI = {}
tick()
local p0 = stash.inventory:GetMoney()
stash.inventory:Add(SWORD, 1, "userdata: 020000000000D00D", 1, true)
queue = "sell 520 " .. SWORD .. " 1 1000 020000000000D00D\n"
tick()
worldTime = worldTime + 10 * 86400
tick()
check(stash.inventory:GetCountOfClass(SWORD) == 0 and math.abs(stash.inventory:GetMoney() - p0 - 100) < 1e-6,
	"merchant-owned sale removed, +100 gr: " .. stash.inventory:GetMoney() - p0)
-- condition: a sword at 40% resells for 40% of its price
stash.inventory:Add(SWORD, 1, "userdata: 020000000000D0D0", 0.4, true)
queue = "sell 520 " .. SWORD .. " 1 1000 020000000000D0D0\n"
tick()
check(MR.S.rows[#MR.S.rows].base == 400 and has("40% condition, 40.0 gr each"), "condition sets the resale value")
-- the game refuses both ways: no coins, the row goes, the mod keeps running
removeWorks = false
p0 = stash.inventory:GetMoney()
worldTime = worldTime + 10 * 86400
tick()
check(not MR.stopped and has("would not remove") and #MR.S.rows == 0 and stash.inventory:GetMoney() == p0,
	"refused removal: no coins, no stop")
removeWorks = true
-- half a stack gone is a broken state: stop for the session
local realDelete = stash.inventory.DeleteItem
stash.inventory:Add(SWORD, 2, "userdata: 020000000000D11D")
queue = "sell 520 " .. SWORD .. " 2 1000 020000000000D11D\n"
tick()
stash.inventory.DeleteItem = function(self, id, n) realDelete(self, id, 1) end
worldTime = worldTime + 10 * 86400
tick()
check(MR.stopped and has("STOPPED for this session"), "partial removal stops")
stash.inventory.DeleteItem = realDelete
-- market size: one 2217.5 gr axe at a lone 339 gr shop costs weapon demand 0.18, not 0.55
local S4 = MR.NewState(); MR.Observe(S4, 522, 0, 0, 3390)
MR.AddSale(S4, 522, SWORD, 1, 22175, "AX", 0)
check(math.abs(MR.DemandAt(S4, 522, "weapon", 0) - (1 - 2217.5 / (10000 * (1 + 339 / 1500)))) < 1e-9
	and MR.DemandAt(S4, 522, "weapon", 0) > 0.8, "hamlet weapon market " .. MR.DemandAt(S4, 522, "weapon", 0))
for i = 1, 20 do MR.Observe(S4, 600 + i, 10 * i, 0, 6000) end   -- a town: 20 more shops of 600 gr nearby
check(MR.Capacity(S4, 522, "weapon") > 80000, "town weapon market " .. MR.Capacity(S4, 522, "weapon"))
print(string.format("merchant logic tests: %d checks passed (fresh plate %.2f days, flooded last plate %.1f days)",
	checks, r1.eta, S.rows[#S.rows].eta))
