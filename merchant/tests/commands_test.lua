-- Real command handlers against the offline log/registration boundary; never loads the game.
local lines, commands = {}, {}
System = {
	LogAlways = function(line) lines[#lines + 1] = line end,
	AddCCommand = function(name, body) commands[name] = body end,
}
dofile("merchant/src/Scripts/MerchantResale/merchant.lua")
assert(commands.merchant_status and commands.merchant_log and commands.merchant_resale)
assert(lines[#lines] == "[MERCHANT] hello")
MerchantResale:Logging('"0"')
assert(lines[#lines] == "[MERCHANT] set logging 0")
MerchantResale:Logging("1")
assert(lines[#lines] == "[MERCHANT] set logging 1")
local n = #lines
MerchantResale:Logging("1;quit")
assert(#lines == n + 1 and lines[#lines]:find("usage:", 1, true))
MerchantResale:Resale("1")
assert(lines[#lines - 1] == "[MERCHANT] set resale 1")
assert(lines[#lines]:find("STILL logging only", 1, true))
MerchantResale:Status()
assert(lines[#lines - 1] == "[MERCHANT] status")
print("merchant command tests: 8 assertions passed (offline, no game)")
