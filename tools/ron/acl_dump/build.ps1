# Builds acl_dump.exe with MSVC (VS 2022) against the ACL 2.1 / RTM 2.2 headers.
#   powershell -File tools/ron/acl_dump/build.ps1 <acl_dump.cpp> <acl includes> <rtm includes> <out.exe>
$ErrorActionPreference = "Stop"
$src = $args[0]; $acl = $args[1]; $rtm = $args[2]; $out = $args[3]
$vs = & "C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$vcvars = Join-Path $vs "VC\Auxiliary\Build\vcvars64.bat"
$obj = [System.IO.Path]::ChangeExtension($out, ".obj")
cmd /c "`"$vcvars`" >nul && cl /nologo /O2 /EHsc /std:c++17 /I`"$acl`" /I`"$rtm`" `"$src`" /Fo`"$obj`" /Fe`"$out`"" | Select-String -Pattern "error|warning" | % Line
if (Test-Path $out) { "BUILT " + (Get-Item $out).Length } else { "FAILED"; exit 1 }
