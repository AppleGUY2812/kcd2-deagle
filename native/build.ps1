# Builds deagle_native.asi and fastload.asi (the load-time plugin, written next to the first) with MSVC (VS 2022) from a
# Windows-side copy of the sources. Optional third argument: the work folder (two sessions building at once).
# usage (from WSL, Idle priority while the game runs; a failed build prints BUILD FAILED and exits 1):
#   W=$(wslpath -w native); powershell.exe -NoProfile -ExecutionPolicy Bypass -Command "(Get-Process -Id \$PID).PriorityClass = 'Idle'; & '$W\build.ps1' '$W' '$W\dist\deagle_native.asi'"
$ErrorActionPreference = "Stop"
$src = $args[0]; $out = $args[1]
$work = if ($args.Count -ge 3) { $args[2] } else { "E:\Tools\deagle-work\native-build" }
if (Test-Path $work) { Remove-Item -Recurse -Force "$work\src","$work\CMakeLists.txt" -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force $work | Out-Null
robocopy $src $work /E /XD build .git /NFL /NDL /NJH /NJS /NP | Out-Null
$vs = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
# a failed compile left the previous build's .asi in Release\ and printed BUILT over it (the other session's catch,
# 2026-10-20): the old binary goes first, and each cmake step's exit code is checked
$asi = "$work\build\Release\deagle_native.asi"
$fl = "$work\build\Release\fastload.asi"
Remove-Item $asi,$fl -Force -ErrorAction SilentlyContinue
& $cmake -S $work -B "$work\build" -G "Visual Studio 17 2022" -A x64 | Select-String -Pattern "error|Error|Generating done" | % Line
if ($LASTEXITCODE -ne 0) { "BUILD FAILED (configure, exit $LASTEXITCODE)"; exit 1 }
# --clean-first: robocopy keeps the sources' own (older) timestamps, so an incremental build linked the previous
# build's objects for every file not touched since (2026-10-23: a working-tree build silently missed rounds.cpp's split)
& $cmake --build "$work\build" --config Release --clean-first 2>&1 | Select-String -Pattern "error|warning C4|deagle_native.vcxproj ->|fastload.vcxproj ->" | % Line
if ($LASTEXITCODE -ne 0 -or -not (Test-Path $asi) -or -not (Test-Path $fl)) { "BUILD FAILED (compile, exit $LASTEXITCODE)"; exit 1 }
Copy-Item $asi $out -Force
$flOut = Join-Path (Split-Path $out) "fastload.asi"
Copy-Item $fl $flOut -Force
"BUILT " + (Get-Item $out).Length + " + fastload.asi " + (Get-Item $flOut).Length
