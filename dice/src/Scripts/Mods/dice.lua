System.LogAlways("$5[Dice Advisor] bootstrap")
if Script.LoadScript("Scripts/DiceAdvisor/dice.lua") ~= 1 then
	System.LogAlways("$4[Dice Advisor] failed to load Scripts/DiceAdvisor/dice.lua")
end
