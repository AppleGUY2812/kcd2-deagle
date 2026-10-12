# Builds deagle_native.asi with MSVC (VS 2022) from a Windows-side copy of the sources.
$ErrorActionPreference = "Stop"
$src = $args[0]; $out = $args[1]
$work = "E:\Tools\deagle-work\native-build"
if (Test-Path $work) { Remove-Item -Recurse -Force "$work\src","$work\CMakeLists.txt" -ErrorAction SilentlyContinue }
New-Item -ItemType Directory -Force $work | Out-Null
robocopy $src $work /E /XD build .git /NFL /NDL /NJH /NJS /NP | Out-Null
$vs = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$cmake = Join-Path $vs "Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
& $cmake -S $work -B "$work\build" -G "Visual Studio 17 2022" -A x64 | Select-String -Pattern "error|Error|Generating done" | % Line
& $cmake --build "$work\build" --config Release 2>&1 | Select-String -Pattern "error|warning C4|deagle_native.vcxproj ->" | % Line
Copy-Item "$work\build\Release\deagle_native.asi" $out -Force
"BUILT " + (Get-Item $out).Length
