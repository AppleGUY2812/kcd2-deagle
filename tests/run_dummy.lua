-- The training dummy (src/Scripts/Deagle/dummy.lua) in the mock game: make puts the plate set on and one loop keeps
-- health/stamina full, attack/block/idle change the AI brain variable, clear puts health/stamina, clothes, items,
-- buffs and the brain variable back, a second make leaves one loop, a save load stops it.
-- usage: luajit tests/run_dummy.lua   (from the repo root)
local M = dofile("tests/mock_engine.lua")

-- the pieces the mock engine does not have: ItemManager, the brain-variable setter, and buffs that return an instance
local buffs = {}
local brain = {}
local braincalls = {}
_G.ItemManager = { CreateItem = function(guid) return { guid = guid } end }
function _G.XGenAIModule.GetBrainVariable(wuid, name) return brain[name] end
function _G.XGenAIModule.SetBrainVariable(wuid, name, value)
	braincalls[#braincalls + 1] = { wuid = wuid, name = name, value = value }
	brain[name] = value
	return true
end
local function addbuff(soul, owner, guid)
	local inst = owner .. ":" .. guid .. ":" .. tostring(#buffs + 1)
	buffs[#buffs + 1] = { soul = soul, inst = inst, guid = guid }
	return inst
end
local removed = {}
local player = _G.g_localActor
player.soul.AddBuff = function(self, guid) return addbuff(self, "player", guid) end
player.soul.RemoveBuff = function(self, inst) removed[#removed + 1] = inst end

dofile("src/Scripts/Deagle/deagle_main.lua")
Deagle:OnGameplayStarted()

local n, fails = 0, 0
local function check(label, ok, detail, value)
	n = n + 1
	if not ok then fails = fails + 1 end
	print((ok and "PASS " or "FAIL ") .. label .. ": " .. tostring(value))
end

local invAdd, equipped, unequipped, deleted = {}, {}, {}, {}
local preset, drawn, holstered = nil, false, false
local npc, health, stamina = nil, 55, 42
local function newNpc()
	local e = M.newEntity("Rodrick")
	e.class = "NPC"
	e.this = { id = "npcWUID" }
	e.soul = {}
	e.soul.state = { health = health, stamina = stamina }
	function e.soul:GetState(k) return self.state[k] end
	function e.soul:SetState(k, v) self.state[k] = v end
	function e.soul:AddBuff(guid) return addbuff(self, "npc", guid) end
	function e.soul:RemoveBuff(inst) removed[#removed + 1] = inst end
	e.actor = {}
	function e.actor:GetMaxHealth() return 100 end
	function e.actor:GetInitialClothingPreset() return "initial_clothes" end
	function e.actor:EquipClothingPreset(p) preset = p end
	function e.actor:EquipInventoryItem(item) equipped[#equipped + 1] = item end
	function e.actor:UnequipInventoryItem(item) unequipped[#unequipped + 1] = item end
	e.inventory = {}
	function e.inventory:AddItem(item) invAdd[#invAdd + 1] = item end
	function e.inventory:DeleteItem(item) deleted[#deleted + 1] = item end
	e.human = {}
	function e.human:DrawWeapon() drawn = true end
	function e.human:HolsterWeapon() holstered = true end
	M.entities()[e.id] = e
	M.entities()[tostring(e.id)] = e
	return e
end
npc = newNpc()
_G.Physics.RayWorldIntersection = function(a, b, c, d, e, f, hits) hits[1] = { entity = npc } return 1 end
brain.defenceModeEnabled = true

-- make
M.console("deagle_dummy", "make")
local d = Deagle.dummy
check("make: the aimed NPC is the dummy", d ~= nil and d.e == npc, d and d.name, d and d.name)
check("make: full plate set created, added and worn (7 each)", #d.armor == 7 and #invAdd == 7 and #equipped == 7,
	"armour/inv/equipped", string.format("%d/%d/%d", #d.armor, #invAdd, #equipped))
check("make: training buffs on the dummy and Henry (4 + 3)", #buffs == 7, "buffs", #buffs)
check("make: idle at first (defenceModeEnabled true)", d.mode == "idle" and brain.defenceModeEnabled == true,
	"mode/brain", d.mode .. "/" .. tostring(brain.defenceModeEnabled))
check("make: our rounds skip its damage (no crime)", Deagle:IsTestNpc(npc) == true, "IsTestNpc", tostring(Deagle:IsTestNpc(npc)))
check("make: one live tick loop", #M.timers >= 1, "timers", #M.timers)

-- modes
M.console("deagle_dummy", "attack")
local attackMsg = M.aiMessages[#M.aiMessages]
check("attack: brain var off, weapon drawn, hitReaction sent", d.mode == "attack" and brain.defenceModeEnabled == false
	and drawn and attackMsg and attackMsg.kind == "hitReaction", "mode/brain/drawn/msg",
	d.mode .. "/" .. tostring(brain.defenceModeEnabled) .. "/" .. tostring(drawn) .. "/" .. tostring(attackMsg and attackMsg.kind))
M.console("deagle_dummy", "block")
check("block: brain var on (offense and movement off, guard/defense stay)", d.mode == "block" and brain.defenceModeEnabled == true,
	"mode/brain", d.mode .. "/" .. tostring(brain.defenceModeEnabled))
M.console("deagle_dummy", "idle")
check("idle: brain var on and weapon holstered", d.mode == "idle" and brain.defenceModeEnabled == true and holstered,
	"mode/brain/holstered", d.mode .. "/" .. tostring(brain.defenceModeEnabled) .. "/" .. tostring(holstered))

-- refill
npc.soul.state.health, npc.soul.state.stamina = 12, 3
local before = #M.timers
M.advance(0.2)
check("refill: health and stamina back to full every frame", npc.soul.state.health == 100 and npc.soul.state.stamina == 100,
	"health/stamina", string.format("%.0f/%.0f", npc.soul.state.health, npc.soul.state.stamina))

-- clear
preset, deleted, unequipped = nil, {}, {}
M.console("deagle_dummy", "clear")
check("clear: the NPC is no longer the dummy", Deagle.dummy == nil and not Deagle:IsTestNpc(npc), "dummy/testNpc",
	tostring(Deagle.dummy) .. "/" .. tostring(Deagle:IsTestNpc(npc)))
check("clear: its health and stamina are exactly as before (55/42)", npc.soul.state.health == 55 and npc.soul.state.stamina == 42,
	"health/stamina", string.format("%.0f/%.0f", npc.soul.state.health, npc.soul.state.stamina))
check("clear: its own clothes put back", preset == "initial_clothes", "preset", tostring(preset))
check("clear: all 7 items taken off and deleted", #unequipped == 7 and #deleted == 7, "unequipped/deleted",
	string.format("%d/%d", #unequipped, #deleted))
check("clear: all 7 buffs removed", #removed == 7, "removed", #removed)
check("clear: its brain variable put back (true)", brain.defenceModeEnabled == true, "brain", tostring(brain.defenceModeEnabled))

-- no second loop: make twice, count live ticks over 12 frames
local ticks, real = 0, Deagle.DummyTick
Deagle.DummyTick = function(self, token)
	if self.dummy and self.dummy.token == token then ticks = ticks + 1 end
	return real(self, token)
end
M.console("deagle_dummy", "make")
M.console("deagle_dummy", "make")
ticks = 0
M.advance(0.2)
check("no second loop: 12 frames after two makes = 12 live ticks", ticks == 12, "ticks", ticks)

-- a save load stops the loop (the mock's timers survive, like a reloaded script's callback)
M.console("deagle_dummy", "attack")
Deagle:OnGameplayStarted()
ticks = 0
M.advance(0.2)
check("save load: the dummy state and its loop are gone", Deagle.dummy == nil and ticks == 0, "dummy/ticks",
	tostring(Deagle.dummy) .. "/" .. tostring(ticks))

for _, x in ipairs(M.errors) do print("ERR " .. tostring(x)) end
check("no script errors", #M.errors == 0, "errors", #M.errors)
print(string.format("%d checks, %d failed", n, fails))
os.exit(fails == 0 and 0 or 1)
