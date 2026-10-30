# Installs (or removes with -Remove) the native plugins next to the game exe:
#   Bin\Win64MasterMasterSteamPGO\version.dll        Ultimate ASI Loader x64 v9.7.4 (loads *.asi), named version.dll since
#     2026-10-30 so dinput8.dll stays free for KCSE (the Pull Hood mod's loader). WHGame imports both, so ours still loads
#     before the game's start-up code; KCSE's later LoadLibrary of the same .asi files only bumps the refcount.
#   Bin\Win64MasterMasterSteamPGO\deagle_native.asi  the Deagle mod's plugin
#   Bin\Win64MasterMasterSteamPGO\fastload.asi       the load-time plugin (its own mod; fastload_*.txt settings)
#   Bin\Win64MasterMasterSteamPGO\inventory.asi      the Inventory Tools mod's plugin (inventory_settings.txt)
#   Bin\Win64MasterMasterSteamPGO\global.ini         the loader's settings (asiloader_global.ini: plugins load from its DllMain)
# The load settings and records used to be deagle_<name>.txt / .bin: the first install copies each to fastload_<name>
# (the old files stay, nothing is overwritten), so the replay records and settings carry over.
param([string]$Dist, [switch]$Remove)
$bin = 'E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO'
if (Get-Process KingdomCome -ErrorAction SilentlyContinue) { "KCD2 is running: close it first"; exit 1 }
if ($Remove) { Remove-Item "$bin\version.dll","$bin\deagle_native.asi","$bin\deagle_native.log","$bin\fastload.asi","$bin\fastload.log","$bin\inventory.asi","$bin\inventory.log","$bin\global.ini" -ErrorAction SilentlyContinue; "removed"; exit 0 }
$load = 'animahead','applyfast','conceptbfs','cvars','fastzip','ftplaces','hitch','menushaders','pakahead','psoahead',
        'questcache','questearly','questfast','questmenu','questreplay','questsample','readiness','stormmemo',
        'streamahead','streamio','xmlahead'
Get-ChildItem "$bin\deagle_*" -Include *.txt,*.bin -ErrorAction SilentlyContinue | % {
	$rest = $_.Name.Substring(7)                                   # after "deagle_"
	$stem = ($rest -split '[_.]')[0]
	$new = Join-Path $bin ("fastload_" + $rest)
	if ($load -contains $stem -and -not (Test-Path $new)) { Copy-Item $_.FullName $new; "carried over " + $_.Name + " -> fastload_" + $rest }
}
# an old install had our loader as dinput8.dll: drop it (only if it is ours, never KCSE's)
if ((Test-Path "$bin\dinput8.dll") -and (Select-String -Path "$bin\dinput8.dll" -Pattern 'IsUltimateASILoader' -SimpleMatch -Quiet)) { Remove-Item "$bin\dinput8.dll"; "removed the old dinput8.dll loader (now version.dll)" }
Copy-Item "$Dist\dinput8.dll" "$bin\version.dll" -Force
Copy-Item "$Dist\deagle_native.asi" "$bin\deagle_native.asi" -Force
Copy-Item "$Dist\fastload.asi" "$bin\fastload.asi" -Force
Copy-Item "$Dist\inventory.asi" "$bin\inventory.asi" -Force
Copy-Item "$PSScriptRoot\asiloader_global.ini" "$bin\global.ini" -Force   # the loader loads the plugins from its DllMain
Get-ChildItem "$bin\version.dll","$bin\deagle_native.asi","$bin\fastload.asi","$bin\inventory.asi" | % { "installed " + $_.Name + " " + $_.Length }
