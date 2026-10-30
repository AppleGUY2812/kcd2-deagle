System.LogAlways("$5[Merchant Resale] phase 1: logging only")
if Script.LoadScript("Scripts/MerchantResale/merchant.lua") ~= 1 then
	System.LogAlways("$4[Merchant Resale] failed to load Scripts/MerchantResale/merchant.lua")
end
