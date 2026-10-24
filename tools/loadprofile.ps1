# Samples the running game about once a second: CPU (that process and the whole box), disk reads per drive, memory
# paging, GPU busy, VRAM. A load in kcd.log is a silent block, so this is how we see what it is actually doing in there:
#   one core pegged, disk idle      -> the game is deserializing / building the world on one thread
#   disk saturated                  -> pak / texture I/O (the big texture mods, the save itself)
#   GPU busy, VRAM climbing         -> streaming textures for the level
#   pages_in high, avail_mb low     -> RAM is full: Windows reads the game's own memory back from the page file
# All counters come from ONE Get-Counter call per row (each call blocks ~1 s to measure a rate; two calls made a row
# every 2.6 s before 2026-10-23).
# usage: powershell -ExecutionPolicy Bypass -File tools/loadprofile.ps1 -Seconds 120 > out.csv
#        -WaitStart: wait for the game process to appear first (arm it before launching)
param([int]$Seconds = 120, [string]$Process = "KingdomCome", [switch]$WaitStart)
$inv = [System.Globalization.CultureInfo]::InvariantCulture
if ($WaitStart) { while (-not (Get-Process $Process -ErrorAction SilentlyContinue)) { Start-Sleep -Milliseconds 500 } }
# drive letter of each physical disk instance, e.g. "0 E:" -> E
$paths = @('\Processor(_Total)\% Processor Time', '\PhysicalDisk(*)\Disk Read Bytes/sec',
           '\Memory\Pages Input/sec', '\Memory\Page Writes/sec', '\Memory\Available MBytes')
"t,proc_cpu_s,proc_ws_mb,proc_commit_mb,box_cpu_pct,read_mb_s_C,read_mb_s_D,read_mb_s_E,pages_in_s,page_writes_s,avail_mb,gpu_pct,vram_mb" | Write-Output
$end = [DateTime]::Now.AddSeconds($Seconds)
while ([DateTime]::Now -lt $end) {
  $s = try { (Get-Counter $paths -ErrorAction Stop).CounterSamples } catch { @() }
  $now = [DateTime]::Now
  $p = Get-Process $Process -ErrorAction SilentlyContinue
  $v = @{}
  foreach ($c in $s) {
    $k = $c.Path.ToLower()
    if ($k -match 'physicaldisk\(\d+ ([a-z]):\)') { $v["read_" + $Matches[1].ToUpper()] = $c.CookedValue / 1MB }
    elseif ($k -like '*processor(_total)*') { $v.box = $c.CookedValue }
    elseif ($k -like '*pages input/sec') { $v.pin = $c.CookedValue }
    elseif ($k -like '*page writes/sec') { $v.pw = $c.CookedValue }
    elseif ($k -like '*available mbytes') { $v.avail = $c.CookedValue }
  }
  $g = & nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>$null
  $gp = ""; $vram = ""
  if ($g) { $parts = ($g -split ','); $gp = $parts[0].Trim(); $vram = $parts[1].Trim() }
  function F($x, $f) { if ($x -ne $null) { ([double]$x).ToString($f, $inv) } else { "" } }
  $row = @($now.ToString("HH:mm:ss.fff", $inv),
    (F $(if ($p) { $p.TotalProcessorTime.TotalSeconds }) "F2"),
    (F $(if ($p) { $p.WorkingSet64 / 1MB }) "F0"),
    (F $(if ($p) { $p.PrivateMemorySize64 / 1MB }) "F0"),
    (F $v.box "F0"), (F $v.read_C "F1"), (F $v.read_D "F1"), (F $v.read_E "F1"),
    (F $v.pin "F0"), (F $v.pw "F0"), (F $v.avail "F0"), $gp, $vram)
  $row -join "," | Write-Output
}
