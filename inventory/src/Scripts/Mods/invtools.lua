-- Inventory Tools mod bootstrap. KCD2 runs Scripts/Mods/<modid>.lua right after Scripts/main.lua.
System.LogAlways("$5[Inv] bootstrap")
if Script.LoadScript("Scripts/Inventory/inventory_main.lua") ~= 1 then
	System.LogAlways("$4[Inv] failed to load Scripts/Inventory/inventory_main.lua")
end
