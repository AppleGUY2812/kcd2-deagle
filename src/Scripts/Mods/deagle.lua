-- Desert Eagle mod bootstrap. KCD2 runs Scripts/Mods/<modid>.lua right after Scripts/main.lua.
System.LogAlways("$5[Deagle] bootstrap")
if Script.LoadScript("Scripts/Deagle/deagle_main.lua") ~= 1 then
	System.LogAlways("$4[Deagle] failed to load Scripts/Deagle/deagle_main.lua")
end
