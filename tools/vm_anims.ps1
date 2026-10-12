$ErrorActionPreference = "Stop"
$W = 'E:\Tools\deagle-work\vm'; $AR = 'E:\Tools\deagle-work\animroot'; $RC = 'E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
$o = "$W\Objects\manmade\weapons\deagle\vm"; if (Test-Path "$o\v_pist_deagle_arm.chr") { Move-Item "$o\v_pist_deagle_arm.chr" "$o\deagle_vm.chr" -Force }
# skeleton registered for the compiler
New-Item -ItemType Directory -Force "$AR\Objects\manmade\weapons\deagle\vm" | Out-Null
Copy-Item "$W\Objects\manmade\weapons\deagle\vm\deagle_vm.chr" "$AR\Objects\manmade\weapons\deagle\vm\deagle_vm.chr" -Force
$sl = Get-Content "$AR\Animations\SkeletonList.xml" -Raw
if ($sl -notmatch 'DeagleVM') {
  $sl = $sl -replace '</SkeletonList>', "  <Skeleton name=`"DeagleVM`" file=`"objects/manmade/weapons/deagle/vm/deagle_vm.chr`" />`r`n</SkeletonList>"
  Set-Content "$AR\Animations\SkeletonList.xml" $sl -Encoding ascii
}
# dae -> i_caf
$ad = "$AR\Animations\deagle_vm"
Remove-Item -Recurse -Force $ad -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $ad | Out-Null
Get-ChildItem "$W\anims\*.dae" | % {
  Copy-Item $_.FullName $ad
  $add = if ($_.BaseName -like '*_add') { 'true' } else { 'false' }
  Set-Content "$ad\$($_.BaseName).animsettings" "{`n`t`"build`": {`n`t`t`"skeletonAlias`": `"DeagleVM`",`n`t`t`"additive`": $add`n`t}`n}" -Encoding ascii
}
Set-Location $ad
Get-ChildItem *.dae | % { & $RC $_.Name /refresh /userdialog=0 | Out-Null }
"I_CAF: " + ((Get-ChildItem *.i_caf | % Name) -join ', ')
# i_caf -> caf + mini index
Set-Location $AR
Remove-Item -Recurse -Force "$AR\out" -ErrorAction SilentlyContinue
& $RC 'Animations\deagle_vm\*.i_caf' /animConfigFolder=Animations /SourceRoot=$AR /TargetRoot="$AR\out" /p=PC /refresh /userdialog=0 /AnimationsCafFilePath=Animations/Animations.img | Select-String 'rror|arning' | Select-Object -First 8 | % Line
"CAF: " + ((Get-ChildItem "$AR\out\Animations\deagle_vm\*.caf" | % { $_.Name + ' ' + $_.Length }) -join ', ')
Copy-Item "$AR\out\Animations\Animations.img" 'E:\Tools\deagle-work\out\deagle_vm_anims.img' -Force
