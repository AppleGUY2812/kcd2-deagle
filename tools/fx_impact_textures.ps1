# Compile the bullet-impact textures (TIF with alpha, from Ready or Not, private use) to streamed DDS and stage them in
# src/Textures/particles/deagle/impacts. Source TIFs: E:\Tools\deagle-work\fx_tif_impacts (see docs/impact_fx_build.md).
#   imp_*_diff.tif    particles          -> AlbedoWithGenericAlpha
#   hole_*_diff.tif   decal colour+alpha -> Decal_AlbedoWithOpacity (transparent mip border)
#   hole_*_ddna.tif   decal normal (CryEngine green-up) + smoothness in alpha -> NormalsWithSmoothness
param([string]$Dest = '\\wsl.localhost\Ubuntu\home\johnpork\kcd2-deagle\src\Textures\particles\deagle\impacts')
$ErrorActionPreference = "Stop"
$w = 'E:\Tools\deagle-work\fx\textures\particles\deagle\impacts'; $rc = 'E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
New-Item -ItemType Directory -Force $w | Out-Null
Copy-Item 'E:\Tools\deagle-work\fx_tif_impacts\*.tif' $w -Force
Set-Location $w
& $rc 'imp_*_diff.tif' /userdialog=0 /refresh /streaming=1 /preset=AlbedoWithGenericAlpha | Select-String 'processed|Exit code|rror' | % Line
& $rc 'hole_*_diff.tif' /userdialog=0 /refresh /streaming=1 /preset=Decal_AlbedoWithOpacity | Select-String 'processed|Exit code|rror' | % Line
& $rc 'hole_*_ddna.tif' /userdialog=0 /refresh /streaming=1 /preset=NormalsWithSmoothness | Select-String 'processed|Exit code|rror' | % Line
New-Item -ItemType Directory -Force $Dest | Out-Null
Get-ChildItem -File | ? { $_.Name -like '*.dds*' -and $_.Name -notlike '*.$dds' } | Copy-Item -Destination $Dest -Force
"staged " + (Get-ChildItem $Dest).Count + " files"
