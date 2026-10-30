-- Real command handlers against the offline log/registration boundary; never loads the game.
local lines, commands = {}, {}
System = {
	LogAlways = function(line) lines[#lines + 1] = line end,
	AddCCommand = function(name, body) commands[name] = body end,
	GetCurrTime = function() return 0 end,
}
Script = { SetTimer = function() end }
dofile("merchant/src/Scripts/MerchantResale/merchant.lua")
assert(commands.merchant_status and commands.merchant_log and commands.merchant_resale)
assert(lines[#lines - 1] == "[MERCHANT] hello" and lines[#lines] == "[MERCHANT] bind")
MerchantResale:Logging('"0"')
assert(lines[#lines] == "[MERCHANT] set logging 0")
MerchantResale:Logging("1")
assert(lines[#lines] == "[MERCHANT] set logging 1")
local n = #lines
MerchantResale:Logging("1;quit")
assert(#lines == n + 1 and lines[#lines]:find("usage:", 1, true))
MerchantResale:Resale("1")
assert(lines[#lines - 1] == "[MERCHANT] set resale 1")
assert(lines[#lines]:find("resale ON", 1, true))
MerchantResale:Status()   -- no player yet: must not throw
assert(lines[#lines]:find("no player yet", 1, true))
print("merchant command tests: 8 assertions passed (offline, no game)")
