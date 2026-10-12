@echo off
rem Export Ready or Not assets with RonExport (CUE4Parse, read-only on the paks). Private use only.
rem usage: ronexport.bat <list.txt> [outDir]
rem   list.txt: one /Game/... package path per line (Windows path). outDir default E:\Tools\deagle-work\ron\g19.
rem The project is built in E:\Tools\deagle-work\ron\exporter\RonExport: copy tools\ron\RonExport\* there and run
rem "dotnet build -c Release" once (docs\ron_extraction.md). Mappings.usmap sits in E:\Tools\deagle-work\ron\exporter.
rem If tools\ron\local_keys.txt exists (gitignored) its AES key is passed on; the paks currently need none.
setlocal
set EXP=E:\Tools\deagle-work\ron\exporter
set OUT=%~2
if "%OUT%"=="" set OUT=E:\Tools\deagle-work\ron\g19
set KEY=
if exist "%~dp0local_keys.txt" set KEY="%~dp0local_keys.txt"
cd /d %EXP%\RonExport
dotnet run -c Release --no-build -- "C:\Program Files (x86)\Steam\steamapps\common\Ready Or Not\ReadyOrNot\Content\Paks" "%EXP%\Mappings.usmap" "%OUT%" "%~1" %KEY%
