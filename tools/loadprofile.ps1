# Samples the running game once a second: CPU (that process and the whole box), disk read, GPU busy, VRAM.
# A load in kcd.log is a silent 20 s block, so this is how we see what it is actually doing in there:
#   one core pegged, disk idle      -> the game is deserializing / building the world on one thread
#   disk saturated                  -> pak / texture I/O (the big texture mods, the save itself)
#   GPU busy, VRAM climbing         -> streaming textures for the level
# usage: powershell -File tools/loadprofile.ps1 -Seconds 120 > out.csv   (then paste the CSV to whoever asked)
param([int]$Seconds = 120, [int]$EveryMs = 1000, [string]$Process = "KingdomCome")
$inv = [System.Globalization.CultureInfo]::InvariantCulture
"t,proc_cpu_s,box_cpu_pct,disk_read_mb_s,gpu_pct,vram_mb" | Write-Output
$last = $null
$n = [int]($Seconds * 1000 / $EveryMs)
for ($i = 0; $i -lt $n; $i++) {
  $now = [DateTime]::Now
  $p = Get-Process $Process -ErrorAction SilentlyContinue
  $cpu = if ($p) { $p.TotalProcessorTime.TotalSeconds } else { $null }
  $box = try { (Get-Counter '\Processor(_Total)\% Processor Time' -ErrorAction Stop).CounterSamples[0].CookedValue } catch { $null }
  $disk = try { (Get-Counter '\PhysicalDisk(_Total)\Disk Read Bytes/sec' -ErrorAction Stop).CounterSamples[0].CookedValue / 1MB } catch { $null }
  $g = & nvidia-smi --query-gpu=utilization.gpu,memory.used --format=csv,noheader,nounits 2>$null
  $gp = $null; $vram = $null
  if ($g) { $parts = ($g -split ','); $gp = $parts[0].Trim(); $vram = $parts[1].Trim() }
  $row = @(
    $now.ToString("HH:mm:ss.fff", $inv),
    ($(if ($cpu -ne $null) { $cpu.ToString("F2", $inv) } else { "" })),
    ($(if ($box -ne $null) { ([double]$box).ToString("F0", $inv) } else { "" })),
    ($(if ($disk -ne $null) { ([double]$disk).ToString("F1", $inv) } else { "" })),
    $gp, $vram)
  $row -join "," | Write-Output
  Start-Sleep -Milliseconds $EveryMs
}
