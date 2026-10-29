# Compiles a converted gun's clips with the game's rc: registers its skeleton (alias Gb_<slug>) in the shared rc
# animation root, DAE -> i_caf -> CAF, and copies the CAFs + their header image to <gun dir>\build\{caf,anims.img}.
# usage: powershell -File gb_anims.ps1 -Gun E:\Tools\deagle-work\guns\<slug> -Slug <slug>
param([Parameter(Mandatory)][string]$Gun, [Parameter(Mandatory)][string]$Slug,
      [int]$MaxRc = 3, [string]$RcPriority = "BelowNormal")
$ErrorActionPreference = "Stop"
$AR = 'E:\Tools\deagle-work\animroot'; $RC = 'E:\SteamLibrary\steamapps\common\KCD2Mod\Tools\rc\rc.exe'
$script:rcJobs = New-Object System.Collections.ArrayList
function Start-Rc([string[]]$RcArgs) {
  while (($script:rcJobs | Where-Object { -not $_.HasExited }).Count -ge $MaxRc) { Start-Sleep -Milliseconds 100 }
  $args2 = $RcArgs + @('/refresh', '/userdialog=0', "/threads=$MaxRc")
  $p = Start-Process -FilePath $RC -ArgumentList $args2 -PassThru -WindowStyle Hidden
  try { $p.PriorityClass = $RcPriority } catch {}
  [void]$script:rcJobs.Add($p)
}
function Wait-Rc {
  while (($script:rcJobs | Where-Object { -not $_.HasExited }).Count -gt 0) { Start-Sleep -Milliseconds 100 }
  $script:rcJobs.Clear()
}
Write-Host "RC max $MaxRc concurrent at $RcPriority priority"
$name = "${Slug}_vm"; $alias = "Gb_$Slug"; $rel = "objects/manmade/weapons/guns/$Slug/$name.chr"
New-Item -ItemType Directory -Force "$AR\Objects\manmade\weapons\guns\$Slug" | Out-Null
Copy-Item "$Gun\build\model\$name.chr" "$AR\Objects\manmade\weapons\guns\$Slug\$name.chr" -Force
$sl = Get-Content "$AR\Animations\SkeletonList.xml" -Raw
if ($sl -notmatch "name=`"$alias`"") {
  $sl = $sl -replace '</SkeletonList>', "  <Skeleton name=`"$alias`" file=`"$rel`" />`r`n</SkeletonList>"
  Set-Content "$AR\Animations\SkeletonList.xml" $sl -Encoding ascii
}
$ad = "$AR\Animations\guns\$name"
Remove-Item -Recurse -Force $ad -ErrorAction SilentlyContinue; New-Item -ItemType Directory -Force $ad | Out-Null
Get-ChildItem "$Gun\build\anims\*.dae" | % {
  Copy-Item $_.FullName $ad
  $add = if ($_.BaseName -like '*_add') { 'true' } else { 'false' }
  Set-Content "$ad\$($_.BaseName).animsettings" "{`n`t`"build`": {`n`t`t`"skeletonAlias`": `"$alias`",`n`t`t`"additive`": $add`n`t}`n}" -Encoding ascii
}
Set-Location $ad
Get-ChildItem *.dae | % { Start-Rc @($_.Name) }
Wait-Rc
"I_CAF: " + ((Get-ChildItem *.i_caf | % Name) -join ', ')
Set-Location $AR
$out = "$AR\out_$Slug"
Remove-Item -Recurse -Force $out -ErrorAction SilentlyContinue
Start-Rc @("Animations\guns\$name\*.i_caf", '/animConfigFolder=Animations', "/SourceRoot=$AR", "/TargetRoot=$out", '/p=PC', '/AnimationsCafFilePath=Animations/Animations.img')
Wait-Rc
New-Item -ItemType Directory -Force "$Gun\build\caf" | Out-Null
Copy-Item "$out\Animations\guns\$name\*.caf" "$Gun\build\caf\" -Force
Copy-Item "$out\Animations\Animations.img" "$Gun\build\anims.img" -Force
"CAF: " + ((Get-ChildItem "$Gun\build\caf\*.caf" | % { $_.Name + ' ' + $_.Length }) -join ', ')
