# Compile the Deagle particle textures (TIF with alpha) to streamed DDS and stage them in src/Textures/particles/deagle.
param([string]$Dest)
$ErrorActionPreference = "Stop"
$w = 'E:\Tools\deagle-work\fx\textures\particles\deagle'; $rc = 'E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item 'E:\Tools\deagle-work\fx_tif\*.tif' $w -Force
Set-Location $w
& $rc '*.tif' /userdialog=0 /refresh /streaming=1 /preset=AlbedoWithGenericAlpha | Select-String 'processed|Exit code|rror' | % Line
New-Item -ItemType Directory -Force $Dest | Out-Null
Get-ChildItem -File | ? { $_.Name -like '*.dds*' -and $_.Name -notlike '*.$dds' } | Copy-Item -Destination $Dest -Force
"staged " + (Get-ChildItem $Dest).Count + " files"
