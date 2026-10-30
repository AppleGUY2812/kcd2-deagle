-- Commands only. No sale callbacks, inventory mutations, timers, or save hooks in phase 1.
MerchantResale = MerchantResale or {}

local function native(command) System.LogAlways("[MERCHANT] " .. command) end
local function arg(line)
	return (tostring(line or ""):gsub('"', "")):match("^%s*(.-)%s*$")
end

function MerchantResale:Logging(line)
	local value = arg(line)
	if value ~= "0" and value ~= "1" then
		System.LogAlways("[Merchant Resale] usage: merchant_log 0|1")
		return
	end
	native("set logging " .. value)
end

function MerchantResale:Resale(line)
	local value = arg(line)
	if value ~= "0" and value ~= "1" then
		System.LogAlways("[Merchant Resale] usage: merchant_resale 0|1 (reserved; phase 1 never resells)")
		return
	end
	native("set resale " .. value)
	System.LogAlways("[Merchant Resale] setting kept for later phases; phase 1 is STILL logging only")
end

function MerchantResale:Status()
	native("status")
	System.LogAlways("[Merchant Resale] phase 1: see merchant.log for hook counters and last observed shop; no nearest-merchant lookup or live ledger yet")
end

function MerchantResale:Hello() native("hello") end
System.AddCCommand("merchant_status", "MerchantResale:Status()", "Phase 1 hook status in merchant.log (not nearest merchant yet)")
System.AddCCommand("merchant_log", "MerchantResale:Logging([[%line]])", "merchant_log 0|1: checkout/open logging (saved)")
System.AddCCommand("merchant_resale", "MerchantResale:Resale([[%line]])", "merchant_resale 0|1: reserved saved setting; NO resale in phase 1")
if type(UIAction) == "table" and UIAction.RegisterEventSystemListener then
	UIAction.RegisterEventSystemListener(MerchantResale, "System", "OnGameplayStarted", "Hello")
end
MerchantResale:Hello()
