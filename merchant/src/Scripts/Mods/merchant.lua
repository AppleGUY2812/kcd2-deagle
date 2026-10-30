System.LogAlways("$5[Merchant Resale] phase 2: merchants resell what you sell them")
for _, file in ipairs({ "Scripts/MerchantResale/categories.lua", "Scripts/MerchantResale/merchant.lua" }) do
	if Script.LoadScript(file) ~= 1 then System.LogAlways("$4[Merchant Resale] failed to load " .. file) end
end
