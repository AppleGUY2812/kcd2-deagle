# KCD2 Perk Points: a small window that shows Henry's unspent perk points for every skill and stat, sets them, and
# resets one skill's perks (refunding their points). Owner: Claude 1. It drives the game's own console commands
# deagle_points / deagle_respec (native/src/perkpoints.cpp) over the dev console port (127.0.0.1:4600, as rcon.ps1)
# and reads the answers from kcd.log. Needs the game running with the native plugin from 2026-10-18 or later.
#   powershell -ExecutionPolicy Bypass -File points_ui.ps1            the window
#   powershell -ExecutionPolicy Bypass -File points_ui.ps1 -Test      plumbing check without a window (prints results)
#   powershell -ExecutionPolicy Bypass -File points_ui.ps1 -Shot <png> draw the window with sample values to a file
param([switch]$Test, [string]$Shot = "")
$ErrorActionPreference = "Stop"
$LogPath = "E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\kcd.log"

# the skills the perk screen shows (skill.xml hidden="false"), menu names from text_ui_soul.xml; then the four stats
$Rows = @(
	@("stealth", "Stealth"), @("horse_riding", "Horsemanship"), @("fencing", "Warfare"), @("thievery", "Thievery"),
	@("alchemy", "Alchemy"), @("craftsmanship", "Craftsmanship"), @("drinking", "Drinking"), @("survival", "Survival"),
	@("weapon_sword", "Sword"), @("heavy_weapons", "Heavy Weapons"), @("marksmanship", "Marksmanship"),
	@("weapon_large", "Polearms"), @("weapon_unarmed", "Unarmed"), @("scholarship", "Scholarship"),
	@("houndmaster", "Houndmaster"),
	@("stat0", "Strength *"), @("stat1", "Agility *"), @("stat2", "Vitality *"), @("stat3", "Speech *")
)

function Send-Console([string]$cmd) {
	$c = New-Object System.Net.Sockets.TcpClient
	$c.Connect("127.0.0.1", 4600)
	$s = $c.GetStream()
	$p = [System.Text.Encoding]::ASCII.GetBytes("5" + $cmd) + [byte]0
	$s.Write($p, 0, $p.Length); $s.Flush()
	Start-Sleep -Milliseconds 100
	$c.Close()
}

function Log-Length { (Get-Item $LogPath).Length }

# new "[Deagle] points: ..." lines written after byte $from; waits until $done matches one (or $ms runs out)
function Read-Answer([long]$from, [string]$done, [int]$ms = 3000) {
	$t = [Diagnostics.Stopwatch]::StartNew()
	$found = @()
	while ($t.ElapsedMilliseconds -lt $ms) {
		$fs = [IO.File]::Open($LogPath, 'Open', 'Read', 'ReadWrite')
		try {
			if ($fs.Length -gt $from) {
				$fs.Seek($from, 'Begin') | Out-Null
				$text = (New-Object IO.StreamReader($fs)).ReadToEnd()
				$found = @($text -split "`r?`n" | Where-Object { $_ -match '\[Deagle\] points: ' } | ForEach-Object { ($_ -split '\[Deagle\] points: ', 2)[1] })
				if ($found | Where-Object { $_ -match $done }) { return $found }
			}
		} finally { $fs.Close() }
		Start-Sleep -Milliseconds 150
		[System.Windows.Forms.Application]::DoEvents()
	}
	return $found
}

function Ask([string]$cmd, [string]$done, [int]$ms = 3000) {
	$from = Log-Length
	Send-Console $cmd
	$lines = Read-Answer $from $done $ms
	$answer = $lines | Where-Object { $_ -match $done } | Select-Object -Last 1
	if (-not $answer) { $answer = ($lines | Select-Object -Last 1) }
	if (-not $answer) { $answer = "no answer from the game (is it running, with the 2026-10-18 plugin?)" }
	return $answer
}

# "stealth 2, alchemy 6" -> hashtable; $null if the line isn't a list
function Parse-List([string]$line) {
	if ($line -eq "no unspent points") { return @{} }
	$h = @{}
	foreach ($part in ($line -split ', ')) {
		if ($part -match '^([a-z_0-9]+) (\d+)$') { $h[$matches[1]] = [int]$matches[2] } else { return $null }
	}
	return $h
}

$ListDone = '^(no unspent points|[a-z_0-9]+ \d+(, [a-z_0-9]+ \d+)*$|.*(not found|nothing changed|isn''t))'

if ($Test) {
	Add-Type -AssemblyName System.Windows.Forms
	"log: $LogPath ($(Log-Length) bytes)"
	"echo through the console: " + (Ask "deagle_lua System.LogAlways('[Deagle] points: stealth 2, alchemy 6')" $ListDone)
	"parsed: " + ((Parse-List "stealth 2, alchemy 6").GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " "
	"deagle_points: " + (Ask "deagle_points" $ListDone)
	return
}

Add-Type -AssemblyName System.Windows.Forms, System.Drawing
[System.Windows.Forms.Application]::EnableVisualStyles()
$form = New-Object System.Windows.Forms.Form
$form.Text = "KCD2 Perk Points"
$form.StartPosition = "CenterScreen"
$form.FormBorderStyle = "FixedSingle"; $form.MaximizeBox = $false
$form.Font = New-Object System.Drawing.Font("Segoe UI", 10)
$form.BackColor = [System.Drawing.Color]::FromArgb(36, 31, 26)
$form.ForeColor = [System.Drawing.Color]::FromArgb(236, 226, 205)
$gold = [System.Drawing.Color]::FromArgb(201, 162, 39)

$top = New-Object System.Windows.Forms.Label
$top.Text = "Free perk points per skill. Change a number and press Set; Reset takes that skill's perks off and gives their points back. Reopen the perk screen to see changes."
$top.Location = "14,10"; $top.Size = "500,40"
$form.Controls.Add($top)

$refresh = New-Object System.Windows.Forms.Button
$refresh.Text = "Refresh"; $refresh.Location = "14,54"; $refresh.Size = "100,30"
$refresh.FlatStyle = "Flat"; $refresh.FlatAppearance.BorderColor = $gold
$form.Controls.Add($refresh)

$status = New-Object System.Windows.Forms.Label
$status.Location = "124,59"; $status.Size = "390,24"; $status.ForeColor = $gold; $status.AutoEllipsis = $true
$form.Controls.Add($status)

$boxes = @{}
$y = 96
foreach ($r in $Rows) {
	$key = $r[0]; $name = $r[1]
	$lab = New-Object System.Windows.Forms.Label
	$lab.Text = $name; $lab.Location = "14,$($y + 4)"; $lab.Size = "160,24"
	$form.Controls.Add($lab)
	$num = New-Object System.Windows.Forms.NumericUpDown
	$num.Minimum = 0; $num.Maximum = 999; $num.Location = "180,$y"; $num.Size = "70,26"
	$num.BackColor = [System.Drawing.Color]::FromArgb(52, 45, 38); $num.ForeColor = $form.ForeColor
	$form.Controls.Add($num)
	$boxes[$key] = $num
	$set = New-Object System.Windows.Forms.Button
	$set.Text = "Set"; $set.Location = "262,$($y - 1)"; $set.Size = "70,28"; $set.Tag = $key
	$set.FlatStyle = "Flat"; $set.FlatAppearance.BorderColor = $gold
	$set.Add_Click({
		$k = $this.Tag
		$status.Text = "setting $k..."
		$status.Text = Ask "deagle_points $k $([int]$boxes[$k].Value)" "^$k \d+ -> |not found|nothing changed|isn't|must"
	})
	$form.Controls.Add($set)
	if (-not $key.StartsWith("stat")) {
		$rs = New-Object System.Windows.Forms.Button
		$rs.Text = "Reset perks"; $rs.Location = "344,$($y - 1)"; $rs.Size = "120,28"; $rs.Tag = @($key, $name)
		$rs.FlatStyle = "Flat"; $rs.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(160, 60, 50)
		$rs.Add_Click({
			$k = $this.Tag[0]; $n = $this.Tag[1]
			$ok = [System.Windows.Forms.MessageBox]::Show("Take all your $n perks off and give their points back?", "Reset $n", "YesNo", "Question")
			if ($ok -ne "Yes") { return }
			$status.Text = "resetting $n..."
			$status.Text = Ask "deagle_respec $k" "^respec |^no .* perks to reset|not found|nothing changed|disagree|isn't" 6000
			Refresh-All
		})
		$form.Controls.Add($rs)
	}
	$y += 34
}
$note = New-Object System.Windows.Forms.Label
$note.Text = "* which stat slot is which: from the game's stat table, not yet checked on the perk screen"
$note.Location = "14,$($y + 4)"; $note.Size = "500,22"; $note.ForeColor = [System.Drawing.Color]::FromArgb(150, 140, 120)
$note.Font = New-Object System.Drawing.Font("Segoe UI", 8.5)
$form.Controls.Add($note)
$form.ClientSize = "530,$($y + 34)"

function Refresh-All {
	$status.Text = "reading..."
	$line = Ask "deagle_points" $ListDone
	$h = Parse-List $line
	if ($null -eq $h) { $status.Text = $line; return }
	foreach ($k in $boxes.Keys) { $boxes[$k].Value = $(if ($h.ContainsKey($k)) { [Math]::Min(999, $h[$k]) } else { 0 }) }
	$status.Text = "read at $(Get-Date -Format HH:mm:ss)"
}
$refresh.Add_Click({ Refresh-All })

if ($Shot) {
	$sample = @{ stealth = 2; horse_riding = 2; alchemy = 6; survival = 1; stat0 = 1; stat2 = 1; stat3 = 1 }
	foreach ($k in $boxes.Keys) { $boxes[$k].Value = $(if ($sample.ContainsKey($k)) { $sample[$k] } else { 0 }) }
	$status.Text = "sample values (screenshot)"
	$form.Show(); [System.Windows.Forms.Application]::DoEvents()
	$bmp = New-Object System.Drawing.Bitmap $form.Width, $form.Height
	$form.DrawToBitmap($bmp, (New-Object System.Drawing.Rectangle 0, 0, $form.Width, $form.Height))
	$bmp.Save($Shot, [System.Drawing.Imaging.ImageFormat]::Png)
	$form.Close()
	return
}
$form.Add_Shown({ Refresh-All })
[void]$form.ShowDialog()
