# Installs (or removes with -Remove) the native plugin next to the game exe:
#   Bin\Win64MasterMasterSteamPGO\dinput8.dll        Ultimate ASI Loader x64 v9.7.4 (loads *.asi)
#   Bin\Win64MasterMasterSteamPGO\deagle_native.asi  our plugin
param([string]$Dist, [switch]$Remove)
$bin = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO'
if (Get-Process KingdomCome -ErrorAction SilentlyContinue) { "KCD2 is running: close it first"; exit 1 }
if ($Remove) { Remove-Item "$bin\dinput8.dll","$bin\deagle_native.asi","$bin\deagle_native.log" -ErrorAction SilentlyContinue; "removed"; exit 0 }
Copy-Item "$Dist\dinput8.dll" "$bin\dinput8.dll" -Force
Copy-Item "$Dist\deagle_native.asi" "$bin\deagle_native.asi" -Force
Get-ChildItem "$bin\dinput8.dll","$bin\deagle_native.asi" | % { "installed " + $_.Name + " " + $_.Length }
