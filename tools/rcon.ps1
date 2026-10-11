param([string]$cmd)
$c = New-Object System.Net.Sockets.TcpClient('127.0.0.1', 4600)
$s = $c.GetStream(); $s.ReadTimeout = 1500
$enc = [System.Text.Encoding]::ASCII
$payload = $enc.GetBytes('5' + $cmd) + [byte]0
$s.Write($payload, 0, $payload.Length); $s.Flush()
$buf = New-Object byte[] 65536; $out = ''
$t = [Diagnostics.Stopwatch]::StartNew()
while ($t.ElapsedMilliseconds -lt 2500) {
  try { $n = $s.Read($buf, 0, $buf.Length); if ($n -le 0) { break }; $out += $enc.GetString($buf, 0, $n) } catch { break }
}
$c.Close()
($out -split "`0") | ? { $_ -match 'Deagle|deagle' } | % { $_.Substring(1) }
