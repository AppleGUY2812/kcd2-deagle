-- deagle_dummy: a sparring partner. Never spawns one: the NPC you aim at (60 m, like deagle_testnpc) becomes the
-- dummy. It wears a full plate set, cannot die or fall unconscious, keeps full health and stamina every frame, and
-- is driven by the game's own training-fight levers. deagle_dummy clear puts the NPC back as it was.
-- Research in work/API.md: the game's fight is a Skald/engine setup (fighteffect + StopFight); from Lua the levers are
-- the soul's health/stamina (SetState), the NPC brain variables of AI/npc/basic/switch/interrupt_attack.xml
-- (defenceModeEnabled = no offense and no movement, guard/defense stay on), and the game's hitReaction stimulus.
local U = Deagle.U
local log, vscale, try = U.log, U.vscale, U.try

-- The training fight's protections (work/API.md section 1): immortality + no unconsciousness on the dummy, no health
-- damage on both, the fistfight crime invisibility on both, and the training XP debuff on Henry.
local BUFFS = {
	{ guid = "730503bf-735a-4f47-baae-c2d84ee77524", who = "dummy", name = "immortality_nonpersistent" },
	{ guid = "f46120bf-b45f-4ec5-95c6-03d526cb40bf", who = "dummy", name = "unconsciousness_protection_nonpersistent" },
	{ guid = "7d6a30e4-c6fe-470f-b8c9-f8b226ee44cf", who = "dummy", name = "combatTutorial_preventDamage" },
	{ guid = "fe9ca784-46f1-4bb4-9efc-1abe7e96a99a", who = "both", name = "fistfights_fightInvisibility" },
	{ guid = "3f98693d-43d6-4c09-bec7-2498c40ea908", who = "player", name = "combatTutorial_preventHealthDamage" },
	{ guid = "ffda734a-763e-4de6-9cac-309c6084513b", who = "player", name = "training_experience_debuff" },
}

-- Full plate, tier A5 (item.xml IDs and lines in work/API.md section 3).
local ARMOUR = {
	{ "helmet",   "311f5baa-ce48-48e0-98f2-e480b677a05a" },
	{ "body",     "1b214a97-8aa8-4892-bcd0-461b12b34258" },
	{ "arms",     "410ee66c-ee38-4d67-9a93-ac1ec288f89c" },
	{ "legs",     "a8cb0916-6466-4728-b551-6f645d40a76d" },
	{ "gloves",   "09ae6cbc-77d1-4686-801e-871b49440d7d" },
	{ "boots",    "245188d0-9cc8-46d3-9b15-08d12f7db981" },
	{ "gambeson","b08cb478-d900-4be1-9fd9-f2754f4d2ff2" },
}
local HIT_BULLET, HIT_MAJOR = 6, 5   -- HitReactionType.Bullet (ai_enums.xml); health lost => major injury (hits.lua:172)

-- Every engine call is pcall'd; a method the game lacks is logged once and skipped from then on.
local said = {}
local function call(what, fn, ...)
	if type(fn) ~= "function" then
		if not said[what] then said[what] = true; log("$4dummy: " .. what .. " is missing, skipped") end
		return nil
	end
	local ok, a = pcall(fn, ...)
	if not ok then
		if not said[what] then said[what] = true; log("$4dummy: " .. what .. " failed: " .. tostring(a)) end
		return nil
	end
	return a
end
local function argwords(s)
	local w = {}
	for x in tostring(s or ""):gsub('"', ""):lower():gmatch("%S+") do w[#w + 1] = x end
	return w
end
-- a name without assuming GetName exists on every entity class
local function entityName(e)
	local ok, n = pcall(e.GetName, e)
	return ok and tostring(n or "") or ""
end

-- deagle_dummy -> status; the aimed NPC becomes the dummy
function Deagle:DummyCmd(args)
	local w = argwords(args)
	local what = w[1] or ""
	if what == "" or what == "status" then return self:DummyStatus() end
	if what == "make" then return self:DummyMake() end
	if what == "attack" or what == "block" or what == "idle" then return self:DummyMode(what) end
	if what == "clear" then return self:DummyClear() end
	self:Notify("deagle_dummy: make | attack | block | idle | clear  (no argument = status)")
end

function Deagle:DummyStatus()
	local d = self.dummy
	if not d then self:Notify("deagle_dummy: no dummy (aim at an NPC and deagle_dummy make)") return end
	local hp, st = "?", "?"
	pcall(function() hp = d.e.soul:GetState("health") or "?"; st = d.e.soul:GetState("stamina") or "?" end)
	self:Notify(string.format("dummy %s: %s, health %s, stamina %s, %d armour items", tostring(d.name), d.mode,
		tostring(hp), tostring(st), #d.armor))
	log(string.format("dummy %s: mode %s, health %s, stamina %s, %d items", tostring(d.name), d.mode, tostring(hp), tostring(st), #d.armor))
end

function Deagle:DummyMake()
	if self.dummy then self:DummyClear() end
	local pl = self:Player()
	local hits = {}
	local ok, n = pcall(Physics.RayWorldIntersection, System.GetViewCameraPos(), vscale(System.GetViewCameraDir(), 60), 1, ent_all,
		pl and pl.id, nil, hits)
	local e = ok and n and n > 0 and hits[1] and hits[1].entity
	if not (e and e.actor and e.soul and e.inventory and e.this) then
		self:Notify("deagle_dummy: aim at an NPC (within 60 m) first")
		return
	end
	local d = {
		e = e, id = e.id, name = entityName(e), mode = "idle", armor = {}, buffs = {}, saved = {},
		token = (self.dummyToken or 0) + 1,
	}
	-- snapshot what must be put back: health, stamina, max health, clothes and the brain variable this changes
	local s = d.saved
	s.health = call("health", e.soul.GetState, e.soul, "health")
	s.stamina = call("stamina", e.soul.GetState, e.soul, "stamina")
	s.maxHealth = call("max health", e.actor.GetMaxHealth, e.actor) or 100
	s.clothing = call("initial clothes", e.actor.GetInitialClothingPreset, e.actor)
	s.brain = call("brain var", XGenAIModule and XGenAIModule.GetBrainVariable, e.this.id, "defenceModeEnabled")
	s.brainNil = s.brain == nil
	self.dummy = d
	self.dummyToken = d.token
	d.armor = self:DummyWear(e)
	self:DummyBuff(d, pl)
	self.testNpcs = self.testNpcs or {}
	self.testNpcs[tostring(e.id)] = true   -- our own rounds don't call DealDamage, so they never count as crime
	self:DummyBrain(d, true)                -- idle: no offense, no movement
	self:Notify(string.format("dummy: %s is the sparring partner (%d armour items, full health, mode idle); deagle_dummy attack | block | idle | clear", d.name, #d.armor))
	log(string.format("dummy: %s, %d armour items, health %.0f stamina %.0f", d.name, #d.armor, s.health or 0, s.stamina or 0))
	self:DummyTick(d.token)
end

function Deagle:DummyWear(e)
	local items = {}
	for _, it in ipairs(ARMOUR) do
		local item = call("make item " .. it[1], ItemManager and ItemManager.CreateItem, it[2], 1, 1)
		if not item then
			item = call("create item " .. it[1], e.inventory.CreateItem, e.inventory, it[2], 1, 1)
		end
		if item then
			call("add item " .. it[1], e.inventory.AddItem, e.inventory, item)
			call("equip item " .. it[1], e.actor.EquipInventoryItem, e.actor, item)
			items[#items + 1] = { name = it[1], id = it[2], item = item }
		else
			log("$4dummy: could not create the " .. it[1])
		end
	end
	return items
end

function Deagle:DummyBuff(d, pl)
	for _, b in ipairs(BUFFS) do
		local souls = {}
		if b.who == "dummy" or b.who == "both" then souls[#souls + 1] = d.e.soul end
		if (b.who == "player" or b.who == "both") and pl and pl.soul then souls[#souls + 1] = pl.soul end
		for _, soul in ipairs(souls) do
			local inst = call("buff " .. b.name, soul.AddBuff, soul, b.guid)
			d.buffs[#d.buffs + 1] = { soul = soul, guid = b.guid, inst = inst, name = b.name }
		end
	end
end

-- The AI tree's own switch (interrupt_attack.xml:26,206,242): true = no offense and no movement, guard/defense stay on.
function Deagle:DummyBrain(d, defence)
	local e = d.e
	if not (XGenAIModule and e.this and XGenAIModule.SetBrainVariable) then
		if not said["set brain variable"] then said["set brain variable"] = true; log("$4dummy: set brain variable is missing, skipped") end
		return false
	end
	local ok, err = pcall(XGenAIModule.SetBrainVariable, e.this.id, "defenceModeEnabled", defence and true or false)
	if not ok then
		if not said["set brain variable"] then said["set brain variable"] = true; log("$4dummy: set brain variable failed: " .. tostring(err)) end
		return false
	end
	return true
end

-- The game's own hit stimulus (hitReaction, type Bullet): the NPC answers with combat (attack / block according to
-- defenceModeEnabled). Never at make; attack mode only.
function Deagle:DummyProvoke(e)
	local p = self:Player()
	local msg = table.MakeFromType("hitReaction", { attacker = p.this.id, hitType = HIT_BULLET, hitStrength = HIT_MAJOR })
	call("hit reaction", XGenAIModule and XGenAIModule.SendMessageToEntityData, e.this.id, "hitReaction", msg)
end

function Deagle:DummyMode(mode)
	local d = self.dummy
	if not d then self:Notify("deagle_dummy: no dummy (aim at an NPC and deagle_dummy make)") return end
	if mode == "attack" then
		d.mode = "attack"
		if not self:DummyBrain(d, false) then
			self:Notify("dummy: the game has no SetBrainVariable, so attack mode can only provoke a normal fight")
		end
		call("draw weapon", d.e.human and d.e.human.DrawWeapon, d.e.human)
		self:DummyProvoke(d.e)
	elseif mode == "block" then
		d.mode = "block"
		self:DummyBrain(d, true)   -- offense off, guard/defense on: it only blocks
	else
		d.mode = "idle"
		self:DummyBrain(d, true)   -- no offense, no movement
		call("holster weapon", d.e.human and d.e.human.HolsterWeapon, d.e.human)
	end
	self:Notify("dummy: " .. d.name .. " " .. (mode == "attack" and "attacks you" or mode == "block" and "only blocks" or "stands still"))
	log("dummy mode " .. mode)
end

-- One loop only: a token, like VmTick (viewmodel.lua:241). It ends at clear, at a save load, and when the entity is gone.
function Deagle:DummyTick(token)
	local d = self.dummy
	if not d or d.token ~= token then return end
	local e = d.e
	local cur = call("get dummy entity", System.GetEntity, e.id)
	if not cur or not cur.soul then
		self:DummyGone()
		return
	end
	call("dummy health", cur.soul.SetState, cur.soul, "health", d.saved.maxHealth or 100)
	call("dummy stamina", cur.soul.SetState, cur.soul, "stamina", 100)
	Script.SetTimer(0, function() Deagle:DummyTick(token) end)
end

-- entity vanished under us: drop the loop, nothing left to restore reliably
function Deagle:DummyGone()
	local d = self.dummy
	if d then
		log("dummy: the entity is gone, loop stopped")
		if self.testNpcs then self.testNpcs[tostring(d.id)] = nil end
	end
	self.dummy = nil
	self.dummyToken = (self.dummyToken or 0) + 1
end

-- a save load: Script timers are gone and the old ids name another world; only drop the loop/state.
function Deagle:DummyLoaded()
	local d = self.dummy
	if d then
		log("dummy: a save load ended the dummy loop")
		if self.testNpcs then self.testNpcs[tostring(d.id)] = nil end
	end
	self.dummy = nil
	self.dummyToken = (self.dummyToken or 0) + 1
end

function Deagle:DummyClear()
	local d = self.dummy
	if not d then self:Notify("deagle_dummy: no dummy to clear") return end
	self.dummy, self.dummyToken = nil, (self.dummyToken or 0) + 1   -- stop the loop first
	local e = d.e
	-- take off and remove our items, then put the NPC's own clothes back
	for _, it in ipairs(d.armor) do
		call("unequip " .. it.name, e.actor.UnequipInventoryItem, e.actor, it.item)
		call("delete " .. it.name, e.inventory.DeleteItem, e.inventory, it.item, 1)
	end
	if d.saved.clothing then call("restore clothes", e.actor.EquipClothingPreset, e.actor, d.saved.clothing) end
	-- remove only the buffs this dummy added (instance id when the game gave one, else that guid's instances)
	for _, b in ipairs(d.buffs) do
		if b.inst == nil then
			call("remove " .. b.name, b.soul.RemoveAllBuffsByGuid, b.soul, b.guid)
		else
			call("remove " .. b.name, b.soul.RemoveBuff, b.soul, b.inst)
		end
	end
	d.buffs = {}
	-- exact health and stamina from before make
	call("restore health", e.soul.SetState, e.soul, "health", d.saved.health or d.saved.maxHealth or 100)
	call("restore stamina", e.soul.SetState, e.soul, "stamina", d.saved.stamina or 100)
	-- its own behaviour
	if not d.saved.brainNil then self:DummyBrain(d, d.saved.brain) end
	if self.testNpcs then self.testNpcs[tostring(e.id)] = nil end
	self:Notify("dummy: " .. d.name .. " is back as it was")
	log("dummy cleared: " .. d.name)
end
