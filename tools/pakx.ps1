param([string]$Pak, [string]$Pattern, [string]$Out, [switch]$ListOnly)
Add-Type -AssemblyName System.IO.Compression.FileSystem
$z = [System.IO.Compression.ZipFile]::OpenRead($Pak)
$n = 0
foreach ($e in $z.Entries) {
  if ($e.FullName -match $Pattern) {
    $n++
    if ($ListOnly) { "{0}`t{1}" -f $e.FullName, $e.Length; continue }
    $dst = Join-Path $Out ($e.FullName -replace '/', '\')
    New-Item -ItemType Directory -Force (Split-Path $dst) | Out-Null
    [System.IO.Compression.ZipFileExtensions]::ExtractToFile($e, $dst, $true)
  }
}
$z.Dispose()
"MATCHED $n in $Pak"
