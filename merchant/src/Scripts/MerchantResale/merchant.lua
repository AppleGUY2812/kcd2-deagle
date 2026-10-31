-- Merchant Resale, phase 2 (docs/merchant_resale_design.md).
-- The plugin (merchant.asi) reports every COMMITTED checkout through MRN.pull(); this file keeps the ledger and the
-- demand per market, and settles a shop when the player walks up to it (before its shop screen can open): resold
-- items leave the shop's stash, their value goes into the shop's purse (capped at 10x its normal purse), and money
-- earned that way survives the weekly restock. State lives in the player's AI table, which every save stores
-- (Player:OnSaveAI / OnLoadAI), so each save keeps its own ledger.
-- Units: money in tenths of a groschen ("deci", the game's shop unit), time in game days.
MerchantResale = MerchantResale or {}
local MR = MerchantResale

MR.cfg = {
	-- groschen of goods a market absorbs per category (demand 1 -> 0): capacity x (1 + wealth / wealthRef), wealth =
	-- the normal purses of the market's shops the mod knows (a 339 gr hamlet fence: weapons 12 300 gr; a town whose
	-- known shops hold 12 000 gr: 90 000 gr)
	capacity = { armor = 15000, weapon = 10000, food = 1500, alchemy = 4000, misc = 5000 }, wealthRef = 1500,
	baseDays = { armor = 3, weapon = 3, food = 1, alchemy = 2, misc = 2 },
	floor = 0.15, halfLife = 4, spread = 0.3,
	marketRadius = 300, capMul = 10, minPreset = 500,          -- metres, x normal purse, deci (a 0 gr purse still earns)
	settleRadius = 6, stashRadius = 60, settleEvery = 20,      -- metres, metres, real seconds
}
local MONEY = "5ef63059-322e-4e1b-abe8-926e100c770e"
local CATS = { "armor", "weapon", "food", "alchemy", "misc" }

local function say(text) System.LogAlways("[Merchant Resale] " .. text) end
local function native(command) System.LogAlways("[MERCHANT] " .. command) end
local function clock() return System.GetCurrTime() end

-- ---- state (pure; no game calls) -------------------------------------------------------------------------------
function MR.NewState() return { shops = {}, demand = {}, rows = {} } end

-- small deterministic hash in [0, 1): stays below 2^24 because the game's Lua numbers are floats
local function unit(text)
	local h = 7
	for i = 1, #text do h = (h * 31 + text:byte(i)) % 65521 end
	return h / 65521
end
MR.unit = unit

function MR.Recover(d, now)
	if not d then return 1 end
	local dt = math.max(0, now - d.t)
	return 1 - (1 - d.v) * 0.5 ^ (dt / MR.cfg.halfLife)
end

-- the market a shop belongs to: the first known shop within marketRadius (its anchor), else itself
function MR.Observe(S, dbid, x, y, purse)
	local shop = S.shops[dbid]
	if shop then return shop end
	local anchor = dbid
	for _, other in pairs(S.shops) do
		if (other.x - x) ^ 2 + (other.y - y) ^ 2 <= MR.cfg.marketRadius ^ 2 then anchor = other.anchor; break end
	end
	shop = { x = x, y = y, anchor = anchor, preset = purse, last = purse, bonus = 0 }
	S.shops[dbid] = shop
	return shop
end

-- groschen: the normal purses of the market's known shops, and the goods a category can absorb
function MR.Wealth(S, anchor)
	local w, n = 0, 0
	for _, s in pairs(S.shops) do if s.anchor == anchor then w, n = w + s.preset, n + 1 end end
	return w / 10, n
end
function MR.Capacity(S, anchor, cat) return MR.cfg.capacity[cat] * (1 + MR.Wealth(S, anchor) / MR.cfg.wealthRef) end

function MR.DemandAt(S, anchor, cat, now) return MR.Recover(S.demand[anchor .. ":" .. cat], now) end

-- a committed sale of `count` units (one row: a stack resells together)
function MR.AddSale(S, dbid, guid, count, baseDeci, token, now)
	local shop = S.shops[dbid]
	if not shop then return nil end
	local cat = (MerchantResaleCategories and MerchantResaleCategories[guid]) or "misc"
	local key = shop.anchor .. ":" .. cat
	local d = MR.Recover(S.demand[key], now)
	local jitter = 1 + MR.cfg.spread * (2 * unit(token .. ":" .. #S.rows .. ":" .. guid) - 1)
	local row = { shop = dbid, guid = guid, cat = cat, n = count, base = baseDeci, sold = now,
		eta = now + MR.cfg.baseDays[cat] / d * jitter, token = token }
	S.rows[#S.rows + 1] = row
	S.demand[key] = { v = math.max(MR.cfg.floor, d - count * baseDeci / 10 / MR.Capacity(S, shop.anchor, cat)), t = now }
	return row
end

-- the player bought `count` units of a class back from that shop: newest rows first, no coin
function MR.Buyback(S, dbid, guid, count)
	local left = count
	for i = #S.rows, 1, -1 do
		if left <= 0 then break end
		local r = S.rows[i]
		if r.shop == dbid and r.guid == guid then
			local n = math.min(left, r.n)
			r.n = r.n - n; left = left - n
			if r.n <= 0 then table.remove(S.rows, i) end
		end
	end
	return count - left
end

-- rows of a shop that are due, oldest first (they stay in S.rows until the caller settles each one)
function MR.Due(S, dbid, now)
	local due = {}
	for _, r in ipairs(S.rows) do if r.shop == dbid and r.eta <= now then due[#due + 1] = r end end
	table.sort(due, function(a, b) return a.eta < b.eta end)
	return due
end
function MR.Drop(S, row)
	for i, r in ipairs(S.rows) do if r == row then table.remove(S.rows, i); return end end
end

-- coins a resale or restock may add without passing the cap
function MR.Room(shop, purse) return math.max(0, math.max(shop.preset, MR.cfg.minPreset) * MR.cfg.capMul - purse) end

-- the purse changed while nobody traded. A vanilla restock sets it back to the preset: the savings come back.
-- A purse that ROSE without a trade is a restock too, and its amount is the real preset (the first sighting may have
-- been a drained purse). A drop to anything but the preset (the player stole coins, ...) only moves the bookkeeping.
function MR.Restock(shop, purse)
	if math.abs(purse - shop.last) < 1 then return 0 end
	if purse > shop.last then shop.preset = purse end
	local add = 0
	if math.abs(purse - shop.preset) <= math.max(10, shop.preset * 0.01) and shop.bonus > 0 then
		add = math.min(shop.bonus, MR.Room(shop, purse))
	end
	shop.last = purse + add
	return add
end

function MR.Serialize(S)
	local out = { "v1" }
	for id, s in pairs(S.shops) do
		out[#out + 1] = string.format("S %d %.1f %.1f %d %d %d %d", id, s.x, s.y, s.anchor, s.preset, s.last, s.bonus)
	end
	for key, d in pairs(S.demand) do out[#out + 1] = string.format("D %s %.5f %.5f", key, d.v, d.t) end
	for _, r in ipairs(S.rows) do
		out[#out + 1] = string.format("R %d %s %s %d %d %.5f %.5f %s", r.shop, r.guid, r.cat, r.n, r.base, r.sold, r.eta, r.token)
	end
	return table.concat(out, "\n")
end

function MR.Deserialize(text)
	local S = MR.NewState()
	if type(text) ~= "string" or text:sub(1, 2) ~= "v1" then return S end
	for line in text:gmatch("[^\n]+") do
		local kind = line:sub(1, 1)
		if kind == "S" then
			local id, x, y, a, p, l, b = line:match("^S (%-?%d+) (%S+) (%S+) (%-?%d+) (%-?%d+) (%-?%d+) (%-?%d+)$")
			if id then S.shops[tonumber(id)] = { x = tonumber(x), y = tonumber(y), anchor = tonumber(a),
				preset = tonumber(p), last = tonumber(l), bonus = tonumber(b) } end
		elseif kind == "D" then
			local key, v, t = line:match("^D (%S+) (%S+) (%S+)$")
			if key then S.demand[key] = { v = tonumber(v), t = tonumber(t) } end
		elseif kind == "R" then
			local s, g, c, n, b, so, e, tk = line:match("^R (%-?%d+) (%S+) (%S+) (%d+) (%d+) (%S+) (%S+) (%S+)$")
			if s then S.rows[#S.rows + 1] = { shop = tonumber(s), guid = g, cat = c, n = tonumber(n), base = tonumber(b),
				sold = tonumber(so), eta = tonumber(e), token = tk } end
		end
	end
	return S
end

-- ---- the game side ------------------------------------------------------------------------------------------------
function MR:Now() return Calendar.GetWorldTime() / 86400 end
function MR:Count(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end

-- state follows the save: the player's AI table is replaced on every load (Player:OnLoadAI)
function MR:State()
	local ai = g_localActor and g_localActor.AI
	if not ai then return nil end
	if ai ~= self.aiRef then
		self.aiRef = ai
		self.S = MR.Deserialize(ai.MerchantResale)
		say(string.format("state from this save: %d shops, %d sales waiting", self:Count(self.S.shops), #self.S.rows))
	end
	return self.S
end
function MR:Store()
	local ai = g_localActor and g_localActor.AI
	if ai and self.S then ai.MerchantResale = MR.Serialize(self.S) end
end

-- every stash of a shop near the player; the purse is the one holding the most money
function MR:Stashes(dbid)
	local list, purse, best = {}, nil, -1
	local pp = g_localActor:GetWorldPos()
	for _, e in ipairs(System.GetEntitiesInSphere(pp, MR.cfg.stashRadius) or {}) do
		if e.class == "Stash" and e.inventory then
			local ok, d = pcall(Shops.GetShopDBIdByKeeper, e.id)
			if ok and d == dbid then
				list[#list + 1] = e
				local m = e.inventory:GetMoney() or 0
				if m > best then best, purse = m, e end
			end
		end
	end
	return list, purse
end
local function deci(stash) return math.floor(stash.inventory:GetMoney() * 10 + 0.5) end

function MR:Pull(S, now)
	if not (MRN and MRN.pull) then return end
	local ok, text = pcall(MRN.pull)
	if not ok or not text then return end
	local traded = {}
	for dir, dbid, guid, count, base, token in text:gmatch("(%a+) (%-?%d+) (%S+) (%d+) (%-?%d+) (%x+)\n") do
		dbid, count, base = tonumber(dbid), tonumber(count), tonumber(base)
		local stashes, purse = self:Stashes(dbid)
		if purse then
			local p = purse:GetWorldPos()
			MR.Observe(S, dbid, p.x, p.y, deci(purse))
			if dir == "sell" and base > 0 then
				-- customers pay for the condition it is in (linear in health; the plugin reports the full price)
				local _, _, it = MR.find(stashes, token, guid)
				local h = math.max(0, math.min(1, tonumber(it and it.health) or 1))
				local r = MR.AddSale(S, dbid, guid, count, math.floor(base * h + 0.5), token, now)
				say(string.format("shop %d bought %dx %s (%s, %.0f%% condition, %.1f gr each): resells in %.1f days", dbid,
					count, guid, r.cat, h * 100, r.base / 10, r.eta - now))
			elseif dir == "buy" then
				local n = MR.Buyback(S, dbid, guid, count)
				if n > 0 then say(string.format("shop %d: you bought back %d of its resale stock", dbid, n)) end
			end
			traded[dbid] = purse
		else
			say("shop " .. dbid .. ": no stash within " .. MR.cfg.stashRadius .. " m, trade not recorded")
		end
	end
	for dbid, purse in pairs(traded) do   -- trades explain this purse change: no restock bonus for it
		local shop, p = S.shops[dbid], deci(purse)
		shop.bonus = math.max(0, math.min(shop.bonus, p - shop.preset))   -- savings spent on the player's goods
		shop.last = p
	end
	self:Store()
end

-- remove a row's item from the shop's stashes: the exact instance first, else the same class.
-- true = removed, false = the count didn't drop as expected, nil = gone (bought back, restocked away, stolen)
-- the item a sale's token names, in any of the shop's stashes
local function find(stashes, token, guid)
	for _, st in ipairs(stashes) do
		for _, id in pairs(st.inventory:GetInventoryTable() or {}) do
			if tostring(id):upper():find(token:upper(), 1, true) then
				local it = ItemManager.GetItem(id)
				if it and it.class == guid then return st, id, it end
			end
		end
	end
end
MR.find = find

-- removes a resold row's items: true (removed), nil (no longer in stock: bought back or destocked), false (the game
-- refused, nothing changed); throws when only part of it went. What the player sold stays the MERCHANT's property in
-- his chest, and the chest's DeleteItem skips items another inventory owns (WHGame 1.5.6 FUN_182ac0eac: item+0x90
-- must be that inventory; seen 2026-10-30, a sold axe stayed). ItemManager.RemoveItem deletes an item from its owner's
-- inventory (FUN_182ac0e54, same delete call), whole stack only.
local function take(stashes, row)
	local st, id, it = find(stashes, row.token, row.guid)
	if st and (it.amount or 1) >= row.n then
		local inv = st.inventory
		local before = inv:GetCountOfClass(row.guid)
		inv:DeleteItem(id, row.n)
		local gone = before - inv:GetCountOfClass(row.guid)
		if gone == 0 and (it.amount or 1) == row.n and ItemManager.RemoveItem then
			ItemManager.RemoveItem(id)
			gone = before - inv:GetCountOfClass(row.guid)
		end
		if gone == row.n then return true end
		if gone == 0 then return false end
		error(string.format("%d of %d removed", gone, row.n))
	end
	for _, st in ipairs(stashes) do
		local before = st.inventory:GetCountOfClass(row.guid)
		if before >= row.n then
			st.inventory:DeleteItemOfClass(row.guid, row.n)
			local gone = before - st.inventory:GetCountOfClass(row.guid)
			if gone == row.n then return true end
			if gone == 0 then return false end
			error(string.format("%d of %d removed by class", gone, row.n))
		end
	end
	return nil
end
MR.take = take

function MR:Settle(S, dbid, now)
	local stashes, purse = self:Stashes(dbid)
	if not purse then return end
	for _, st in ipairs(stashes) do
		local ok, open = pcall(Shops.IsShopOpened, st.id)
		if ok and open then return end   -- never touch a shop whose screen is open
	end
	local p = purse:GetWorldPos()
	local shop = MR.Observe(S, dbid, p.x, p.y, deci(purse))
	local before = deci(purse)
	local restored = MR.Restock(shop, before)
	if restored > 0 then
		purse.inventory:CreateItem(MONEY, 1, restored)
		say(string.format("shop %d restocked: its savings came back (+%.1f gr)", dbid, restored / 10))
	end
	local resold, gone, coins = 0, 0, 0
	for _, row in ipairs(MR.Due(S, dbid, now)) do
		local ok, result = pcall(take, stashes, row)
		if not ok then
			self.stopped = "removing " .. row.guid .. " from shop " .. dbid .. " failed: " .. tostring(result)
			say("STOPPED for this session: " .. self.stopped)
			break
		end
		MR.Drop(S, row)
		if result == false then
			say(string.format("shop %d: the game would not remove %dx %s; it stays in stock, no coins", dbid, row.n, row.guid))
		elseif result == nil then gone = gone + 1
		else
			local add = math.min(row.base * row.n, MR.Room(shop, deci(purse)))
			if add > 0 then purse.inventory:CreateItem(MONEY, 1, add) end
			coins = coins + add; resold = resold + 1
		end
	end
	local after = deci(purse)
	if after - before ~= coins + restored then
		self.stopped = string.format("shop %d purse moved %d deci, expected %d", dbid, after - before, coins + restored)
		say("STOPPED for this session: " .. self.stopped)
	end
	shop.bonus = shop.bonus + coins
	shop.last = after
	if resold + gone > 0 then
		say(string.format("shop %d resold %d sale(s) (+%.1f gr), %d gone from stock; purse %.1f -> %.1f gr", dbid,
			resold, coins / 10, gone, before / 10, after / 10))
	end
	self:Store()
end

function MR:Tick()
	self.lastTick = clock()
	local ok, err = pcall(function()
		local S = self:State()
		if not S then return end
		if not MRN then
			if clock() > (self.bindAt or 0) then native("bind"); self.bindAt = clock() + 10 end
			return
		end
		local now = self:Now()
		self:Pull(S, now)
		if self.stopped or not (MRN.enabled and MRN.enabled()) then return end
		self.settled = self.settled or {}
		local pp = g_localActor:GetWorldPos()
		for _, e in ipairs(System.GetEntitiesInSphere(pp, MR.cfg.settleRadius) or {}) do
			local okd, dbid = pcall(Shops.GetShopDBIdByKeeper, e.id)
			if okd and dbid and dbid >= 0 and clock() > (self.settled[dbid] or 0) then
				self.settled[dbid] = clock() + MR.cfg.settleEvery
				self:Settle(S, dbid, now)
			end
		end
	end)
	if not ok then
		self.stopped = "error: " .. tostring(err)
		say("STOPPED for this session: " .. self.stopped)
	end
	Script.SetTimer(1000, function() MR:Tick() end)   -- keeps pulling (and recording) even when stopped
end

-- starts the 1 s tick; safe to call again (after a load the old timer may be gone)
function MR:Start()
	if self.lastTick and clock() - self.lastTick < 5 then return end
	self.lastTick = clock()
	native("bind")
	Script.SetTimer(1000, function() MR:Tick() end)
end

-- ---- commands -----------------------------------------------------------------------------------------------------
local function arg(line) return (tostring(line or ""):gsub('"', "")):match("^%s*(.-)%s*$") end

function MR:Logging(line)
	local value = arg(line)
	if value ~= "0" and value ~= "1" then say("usage: merchant_log 0|1"); return end
	native("set logging " .. value)
end

function MR:Resale(line)
	local value = arg(line)
	if value ~= "0" and value ~= "1" then say("usage: merchant_resale 0|1"); return end
	native("set resale " .. value)
	say(value == "1" and "resale ON (saved)" or "resale OFF (saved): sales are still recorded, nothing resells")
end

function MR:Status()
	native("status")
	local S = self:State()
	if not S then say("no player yet"); return end
	if self.stopped then say("STOPPED for this session: " .. self.stopped) end
	local now, best, bd = self:Now(), nil, 30 ^ 2
	local pp = g_localActor:GetWorldPos()
	for _, e in ipairs(System.GetEntitiesInSphere(pp, 30) or {}) do
		local ok, dbid = pcall(Shops.GetShopDBIdByKeeper, e.id)
		if ok and dbid and dbid >= 0 then
			local q = e:GetWorldPos()
			local d = (q.x - pp.x) ^ 2 + (q.y - pp.y) ^ 2
			if d < bd then best, bd = dbid, d end
		end
	end
	say(string.format("bound=%s resale=%s shops known=%d sales waiting=%d", tostring(MRN ~= nil),
		tostring(MRN and MRN.enabled and MRN.enabled()), self:Count(S.shops), #S.rows))
	if not best then say("no shop within 30 m"); return end
	local shop = S.shops[best]
	if not shop then say("shop " .. best .. ": nothing recorded yet"); return end
	local _, purse = self:Stashes(best)
	say(string.format("shop %d: purse %s gr, normal %.1f gr, savings %.1f gr, cap %.1f gr", best,
		purse and string.format("%.1f", purse.inventory:GetMoney()) or "?", shop.preset / 10, shop.bonus / 10,
		math.max(shop.preset, MR.cfg.minPreset) * MR.cfg.capMul / 10))
	local parts = {}
	for _, cat in ipairs(CATS) do parts[#parts + 1] = string.format("%s %.2f", cat, MR.DemandAt(S, shop.anchor, cat, now)) end
	say("market demand: " .. table.concat(parts, ", "))
	local w, n = MR.Wealth(S, shop.anchor)
	say(string.format("market: %d known shop(s) worth %.0f gr; it absorbs %.0f gr of weapons, %.0f gr of armor", n, w,
		MR.Capacity(S, shop.anchor, "weapon"), MR.Capacity(S, shop.anchor, "armor")))
	for _, r in ipairs(S.rows) do
		if r.shop == best then
			say(string.format("  %dx %s (%s, %.1f gr each): %s", r.n, r.guid, r.cat, r.base / 10,
				r.eta <= now and "due now" or string.format("in %.1f days", r.eta - now)))
		end
	end
end

function MR:Hello() native("hello"); self:Start() end
System.AddCCommand("merchant_status", "MerchantResale:Status()", "Nearest shop: purse, savings, demand, items waiting to resell")
System.AddCCommand("merchant_log", "MerchantResale:Logging([[%line]])", "merchant_log 0|1: checkout/open logging in merchant.log (saved)")
System.AddCCommand("merchant_resale", "MerchantResale:Resale([[%line]])", "merchant_resale 0|1: merchants resell what you sold them (saved, default 1)")
if type(UIAction) == "table" and UIAction.RegisterEventSystemListener then
	UIAction.RegisterEventSystemListener(MerchantResale, "System", "OnGameplayStarted", "Hello")
end
MR:Hello()
