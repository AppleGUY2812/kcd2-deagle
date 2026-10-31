# KCD2 Perk Points: a small window that shows Henry's level and unspent perk points for every skill and stat, raises
# levels, sets points, and resets one skill's perks (refunding their points). Owner: Claude 1. It drives the game's own console commands
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
				$found = @($text -split "`r?`n" | Where-Object { $_ -match '\[Deagle\] points: ' } | ForEach-Object { ($_ -split '\[Deagle\] points: ', 2)[1].Trim().Trim('"') })   # the plugin's answers come in quotes
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

# Perk picker: perks.json (tools/gen_perklist.py, next to this script) lists every perk that costs a point. The
# window runs Lua in the game (deagle_lua dofile of a file written here): soul:AddPerk / RemovePerk, a perk's sub-perks
# with it. It can't show which perks Henry has: soul:HasPerk answers false for every skill-tree perk (true only for
# hidden script sub-perks; checked over all 945 perks in game 2026-10-19). Exclusive pairs (Roadrunner / Wildrider...):
# the perk screen refuses the second one, AddPerk isn't asked; what the game does with both is untested.
$Here = Split-Path -Parent $PSCommandPath
$PerksPath = Join-Path $Here "perks.json"
$LuaPath = Join-Path $Here "perk_q.lua"
$Perks = $null

function Run-Lua([string]$code, [string]$done, [int]$ms = 4000) {
	[IO.File]::WriteAllText($LuaPath, $code)
	return Ask ("deagle_lua dofile([[" + ($LuaPath -replace '\\', '/') + "]])") $done $ms
}

function Lua-List($ids) { "{" + (($ids | ForEach-Object { "'$_'" }) -join ",") + "}" }

# Levels: soul:GetSkillLevel / GetStatLevel read them; AdvanceToSkillLevel / AdvanceToStatLevel (what the game's own
# player.lua and FeatureTests use) raise them through XP, so the level-ups give their perk points as in play. The soul
# has no call that lowers a level (SetStatLevel / SetSkillLevel are nil in game, checked 2026-10-19). Stats go by full
# name (strength...; "str" is nil). Max $MaxLevel (30 in vanilla). The Main level (the menu's ui_derivstat_mainlevel) is the derived stat "lvl"
# (soul:GetDerivedStat('lvl'), 17.17 in game 2026-10-19): the game grows it from the four stats, quests and the main story
# and has no Lua call that sets it, so the window shows it and it moves when a stat is raised.
# the highest skill / stat level: 100 with the user's level-cap mod (2026-10-30; vanilla 30)
$MaxLevel = 100
$StatNames = @{ stat0 = "strength"; stat1 = "agility"; stat2 = "vitality"; stat3 = "speech" }
function Lvl-Call([string]$key) {
	if ($StatNames.ContainsKey($key)) { return @("Stat", $StatNames[$key]) } else { return @("Skill", $key) }
}
function Read-Levels {
	$parts = $Rows | ForEach-Object { $c = Lvl-Call $_[0]; "'$($_[0])=' .. tostring(s:Get$($c[0])Level('$($c[1])'))" }
	$code = "local s = player and player.soul; if not s then System.LogAlways('[Deagle] points: levels none') return end`n" +
		"System.LogAlways('[Deagle] points: levels ' .. " + ($parts -join " .. ' ' .. ") + " .. ' main=' .. string.format('%.2f', s:GetDerivedStat('lvl') or -1))"
	$a = Run-Lua $code '^levels ' 6000
	$script:LevelsRaw = $a
	$h = @{}
	foreach ($m in [regex]::Matches($a, '([a-z_0-9]+)=(\d+)')) { $h[$m.Groups[1].Value] = [int]$m.Groups[2].Value }
	if ($a -match ' main=([\d.]+)') { $script:MainLevel = $matches[1] }
	return $h
}
# raise one skill / stat to $to; answers "lvlset <key> <level now>" half a second later (the XP lands first)
function Raise-Level([string]$key, [int]$to) {
	$c = Lvl-Call $key
	$code = "local s = player.soul; local ok, e = pcall(function() s:AdvanceTo$($c[0])Level('$($c[1])', $to) end)`n" +
		"if not ok then System.LogAlways('[Deagle] points: lvlset $key failed ' .. tostring(e)) return end`n" +
		"Script.SetTimer(500, function() System.LogAlways('[Deagle] points: lvlset $key ' .. tostring(s:Get$($c[0])Level('$($c[1])'))) end)"
	return Run-Lua $code "^lvlset $key " 5000
}

# add or remove one perk (and its sub-perks); "perkset ok" / "perkset failed ..." or the game's silence
# AddPerk refuses a perk while its exclusive partner is owned, silently: pcall says ok, nothing is added (in game
# 2026-10-19). So $drop (the partners, with their sub-perks) come off first and the add follows 0.5 s later.
function Set-Perk($p, [bool]$on, $drop = @()) {
	$ids = @($p.id) + @(@($p.subs) | Where-Object { $_ })
	$fn = $(if ($on) { "AddPerk" } else { "RemovePerk" })
	$code = "local s = player and player.soul; local bad = {}`n" +
		"local function run(fn, ids) for _, id in ipairs(ids) do local ok, e = pcall(function() s[fn](s, id) end); if not ok then bad[#bad + 1] = tostring(e) end end end`n" +
		"local function main() run('$fn', $(Lua-List $ids)); System.LogAlways('[Deagle] points: perkset ' .. (#bad == 0 and 'ok' or ('failed ' .. table.concat(bad, '; ')))) end`n" +
		"local drop = $(Lua-List $drop)`n" +
		"if #drop > 0 then run('RemovePerk', drop); Script.SetTimer(500, main) else main() end"
	return Run-Lua $code '^perkset ' 6000
}

function Show-Perks([string]$key, [string]$title) {
	if (-not $script:Perks) {
		if (-not (Test-Path $PerksPath)) { $status.Text = "perks.json missing next to the script (tools/gen_perklist.py)"; return }
		$script:Perks = Get-Content $PerksPath -Raw -Encoding UTF8 | ConvertFrom-Json
	}
	$list = @($script:Perks.$key)
	if ($list.Count -eq 0) { $status.Text = "no perks listed for $title"; return }
	$byId = @{}; foreach ($g in $script:Perks.PSObject.Properties) { foreach ($p in $g.Value) { $byId[$p.id] = $p } }

	$d = New-Object System.Windows.Forms.Form
	$d.Text = "$title perks"; $d.StartPosition = "CenterParent"; $d.FormBorderStyle = "FixedSingle"; $d.MaximizeBox = $false
	$d.Font = $form.Font; $d.BackColor = $form.BackColor; $d.ForeColor = $form.ForeColor
	$info = New-Object System.Windows.Forms.Label
	$info.Text = "Pick a perk, then Add or Remove. Gold = exclusive pair: Add swaps (the other one comes off first; the game won't give both), no point change. Hover for what it does. Reopen the perk screen to see changes."
	$info.Location = "12,8"; $info.Size = "640,40"
	$d.Controls.Add($info)
	$lv = New-Object System.Windows.Forms.ListView
	$lv.View = "Details"; $lv.FullRowSelect = $true; $lv.MultiSelect = $false; $lv.ShowItemToolTips = $true; $lv.HideSelection = $false
	$lv.Location = "12,50"; $lv.Size = "640,400"
	$lv.BackColor = [System.Drawing.Color]::FromArgb(52, 45, 38); $lv.ForeColor = $form.ForeColor
	[void]$lv.Columns.Add("Lvl", 46); [void]$lv.Columns.Add("Perk", 230); [void]$lv.Columns.Add("Exclusive with", 340)
	foreach ($p in $list) {
		$it = New-Object System.Windows.Forms.ListViewItem([string]$p.level)
		[void]$it.SubItems.Add($p.name)
		[void]$it.SubItems.Add((@($p.excl) | Where-Object { $_ } | ForEach-Object { $byId[$_].name }) -join ", ")
		$it.Tag = $p; $it.ToolTipText = $p.desc
		if (@($p.excl | Where-Object { $_ }).Count) { $it.ForeColor = $gold }
		[void]$lv.Items.Add($it)
	}
	$d.Controls.Add($lv)
	$pay = New-Object System.Windows.Forms.CheckBox
	$pay.Text = "Use $title points (Add takes 1, Remove gives 1 back: only Remove a perk you have)"; $pay.Checked = $true
	$pay.Location = "12,458"; $pay.Size = "640,26"
	$d.Controls.Add($pay)
	$ps = New-Object System.Windows.Forms.Label
	$ps.Location = "12,490"; $ps.Size = "440,40"; $ps.ForeColor = $gold
	$d.Controls.Add($ps)
	$addB = New-Object System.Windows.Forms.Button
	$addB.Text = "Add"; $addB.Location = "462,492"; $addB.Size = "90,30"; $addB.FlatStyle = "Flat"; $addB.FlatAppearance.BorderColor = $gold
	$d.Controls.Add($addB)
	$remB = New-Object System.Windows.Forms.Button
	$remB.Text = "Remove"; $remB.Location = "562,492"; $remB.Size = "90,30"; $remB.FlatStyle = "Flat"
	$remB.FlatAppearance.BorderColor = [System.Drawing.Color]::FromArgb(160, 60, 50)
	$d.Controls.Add($remB)
	$d.ClientSize = "664,534"
	$act = {
		param([bool]$on)
		if ($lv.SelectedItems.Count -eq 0) { $ps.Text = "pick a perk first"; return }
		$p = $lv.SelectedItems[0].Tag
		$free = [int]$boxes[$key].Value
		if ($on -and $pay.Checked -and $free -lt 1) {
			$ok = [System.Windows.Forms.MessageBox]::Show("$title has no free points. Add $($p.name) for free?", "No points", "YesNo", "Question")
			if ($ok -ne "Yes") { return }
		}
		# an exclusive perk swaps: its partner comes off first (the game won't add it beside the partner), no point change
		$partners = @(@($p.excl) | Where-Object { $_ } | ForEach-Object { $byId[$_] })
		$swap = $on -and $partners.Count -gt 0
		$drop = @($partners | ForEach-Object { $_.id; @($_.subs) | Where-Object { $_ } })
		$ps.Text = $(if ($swap) { "swapping in" } elseif ($on) { "adding" } else { "removing" }) + " $($p.name)..."
		$a = Set-Perk $p $on $(if ($swap) { $drop } else { @() })
		if ($a -ne "perkset ok") { $ps.Text = "$($p.name): $a"; return }
		$msg = $(if ($swap) { "removed " + (($partners | ForEach-Object { $_.name }) -join ", ") + ", added" } elseif ($on) { "added" } else { "removed" }) + " $($p.name)"
		if ($pay.Checked -and -not $swap) {
			$target = $(if ($on) { [Math]::Max(0, $free - 1) } else { $free + 1 })
			if ($target -ne $free) { $msg += "; points: " + (Ask "deagle_points $key $target" "^$key \d+ -> |not found|nothing changed|isn't|must") }
		}
		$ps.Text = $msg
		Refresh-All
	}
	$addB.Add_Click({ & $act $true })
	$remB.Add_Click({ & $act $false })
	[void]$d.ShowDialog($form)
}

$ListDone = '^(no unspent points|[a-z_0-9]+ \d+(, [a-z_0-9]+ \d+)*$|.*(not found|nothing changed|isn''t))'

if ($Test) {
	Add-Type -AssemblyName System.Windows.Forms
	"log: $LogPath ($(Log-Length) bytes)"
	"echo through the console: " + (Ask "deagle_lua System.LogAlways('[Deagle] points: stealth 2, alchemy 6')" $ListDone)
	"parsed: " + ((Parse-List "stealth 2, alchemy 6").GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " "
	"deagle_points: " + (Ask "deagle_points" $ListDone)
	"levels: " + ((Read-Levels).GetEnumerator() | Sort-Object Name | ForEach-Object { "$($_.Name)=$($_.Value)" }) -join " "
	"levels raw: $script:LevelsRaw"
	"main level: $script:MainLevel"
	"perks.json: " + $(if (Test-Path $PerksPath) { "$(@((Get-Content $PerksPath -Raw -Encoding UTF8 | ConvertFrom-Json).alchemy).Count) alchemy perks" } else { "missing" })
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
$top.Text = "Level: type a higher level, press Raise (only up, max $MaxLevel; level-ups give their perk points). Points: Set changes free perk points. Reset gives a skill's perk points back. Reopen the perk screen to see changes."
$top.Location = "14,10"; $top.Size = "680,40"
$form.Controls.Add($top)

$refresh = New-Object System.Windows.Forms.Button
$refresh.Text = "Refresh"; $refresh.Location = "14,54"; $refresh.Size = "100,30"
$refresh.FlatStyle = "Flat"; $refresh.FlatAppearance.BorderColor = $gold
$form.Controls.Add($refresh)

$status = New-Object System.Windows.Forms.Label
$status.Location = "124,59"; $status.Size = "570,24"; $status.ForeColor = $gold; $status.AutoEllipsis = $true
$form.Controls.Add($status)

$main = New-Object System.Windows.Forms.Label
$main.Location = "14,92"; $main.Size = "160,20"; $main.ForeColor = $gold
$form.Controls.Add($main)
$mainTip = New-Object System.Windows.Forms.ToolTip
$mainTip.SetToolTip($main, "The game grows it from Strength, Agility, Vitality, Speech, quests and the main story; nothing sets it directly. Raise a stat to raise it.")
$boxes = @{}
$levels = @{}
$hdr = New-Object System.Windows.Forms.Label
$hdr.Text = "Level                         Points"; $hdr.Location = "180,92"; $hdr.Size = "300,20"; $hdr.ForeColor = $gold
$form.Controls.Add($hdr)
$y = 116
foreach ($r in $Rows) {
	$key = $r[0]; $name = $r[1]
	$lab = New-Object System.Windows.Forms.Label
	$lab.Text = $name; $lab.Location = "14,$($y + 4)"; $lab.Size = "160,24"
	$form.Controls.Add($lab)
	$lv = New-Object System.Windows.Forms.NumericUpDown
	$lv.Minimum = 0; $lv.Maximum = $MaxLevel; $lv.Location = "180,$y"; $lv.Size = "56,26"
	$lv.BackColor = [System.Drawing.Color]::FromArgb(52, 45, 38); $lv.ForeColor = $form.ForeColor
	$form.Controls.Add($lv)
	$levels[$key] = $lv
	$up = New-Object System.Windows.Forms.Button
	$up.Text = "Raise"; $up.Location = "242,$($y - 1)"; $up.Size = "70,28"; $up.Tag = @($key, $name)
	$up.FlatStyle = "Flat"; $up.FlatAppearance.BorderColor = $gold
	$up.Add_Click({
		$k = $this.Tag[0]; $n = $this.Tag[1].TrimEnd(' ', '*')
		$to = [int]$levels[$k].Value
		$now = $levels[$k].Tag
		# Stats: the shown level includes buffs (GetStatLevel = base + modifiers, capped at the level cap), but AdvanceToStatLevel
		# compares against the BASE (docs/whgame_re/stat_base.md), so a stat can't be refused here: a target at or
		# below the base is a no-op in the game itself.
		$isStat = $k.StartsWith("stat")
		if (-not $isStat -and $null -ne $now -and $to -le $now) { $status.Text = "$n is level ${now}: the game can only raise levels"; $levels[$k].Value = $now; return }
		$status.Text = "raising $n to $to..."
		$a = Raise-Level $k $to
		$status.Text = $(if ($a -match "^lvlset $k (\d+)$") { $(if ($isStat) { "$n raised to base $to (shown with buffs: $($matches[1])); check the perk points" } else { "$n now level $($matches[1])" }) } else { "$n`: $a" })
		Refresh-All
	})
	$form.Controls.Add($up)
	$num = New-Object System.Windows.Forms.NumericUpDown
	$num.Minimum = 0; $num.Maximum = 999; $num.Location = "330,$y"; $num.Size = "70,26"
	$num.BackColor = [System.Drawing.Color]::FromArgb(52, 45, 38); $num.ForeColor = $form.ForeColor
	$form.Controls.Add($num)
	$boxes[$key] = $num
	$set = New-Object System.Windows.Forms.Button
	$set.Text = "Set"; $set.Location = "406,$($y - 1)"; $set.Size = "70,28"; $set.Tag = $key
	$set.FlatStyle = "Flat"; $set.FlatAppearance.BorderColor = $gold
	$set.Add_Click({
		$k = $this.Tag
		$status.Text = "setting $k..."
		$status.Text = Ask "deagle_points $k $([int]$boxes[$k].Value)" "^$k \d+ -> |not found|nothing changed|isn't|must"
	})
	$form.Controls.Add($set)
	if (-not $key.StartsWith("stat")) {
		$rs = New-Object System.Windows.Forms.Button
		$rs.Text = "Reset perks"; $rs.Location = "488,$($y - 1)"; $rs.Size = "120,28"; $rs.Tag = @($key, $name)
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
	$pk = New-Object System.Windows.Forms.Button
	$pk.Text = "Perks..."; $pk.Location = "620,$($y - 1)"; $pk.Size = "90,28"; $pk.Tag = @($key, $name)
	$pk.FlatStyle = "Flat"; $pk.FlatAppearance.BorderColor = $gold
	$pk.Add_Click({ Show-Perks $this.Tag[0] $this.Tag[1] })
	$form.Controls.Add($pk)
	$y += 34
}
$note = New-Object System.Windows.Forms.Label
$note.Text = "* levels show base + buffs (base 25 +5 shows 30); Raise still raises the base. Points slot order: from the stat table, unchecked"
$note.Location = "14,$($y + 4)"; $note.Size = "500,22"; $note.ForeColor = [System.Drawing.Color]::FromArgb(150, 140, 120)
$note.Font = New-Object System.Drawing.Font("Segoe UI", 8.5)
$form.Controls.Add($note)
$form.ClientSize = "724,$($y + 34)"

function Refresh-All {
	$status.Text = "reading..."
	# levels first: they come from Lua and don't depend on the plugin's points read, which can refuse (several lists)
	$lh = Read-Levels
	foreach ($k in $levels.Keys) { if ($lh.ContainsKey($k)) { $levels[$k].Value = [Math]::Min($MaxLevel, $lh[$k]); $levels[$k].Tag = $lh[$k] } }
	$main.Text = $(if ($script:MainLevel) { "Main level: $script:MainLevel" } else { "Main level: ?" })
	$line = Ask "deagle_points" $ListDone
	$h = Parse-List $line
	if ($null -eq $h) { $status.Text = "levels read; points: $line"; return }
	foreach ($k in $boxes.Keys) { $boxes[$k].Value = $(if ($h.ContainsKey($k)) { [Math]::Min(999, $h[$k]) } else { 0 }) }
	$status.Text = "read at $(Get-Date -Format HH:mm:ss)"
}
$refresh.Add_Click({ Refresh-All })

if ($Shot) {
	$sample = @{ stealth = 2; horse_riding = 2; alchemy = 6; survival = 1; stat0 = 1; stat2 = 1; stat3 = 1 }
	foreach ($k in $boxes.Keys) { $boxes[$k].Value = $(if ($sample.ContainsKey($k)) { $sample[$k] } else { 0 }) }
	foreach ($k in $levels.Keys) { $levels[$k].Value = 12 }
	$main.Text = "Main level: 17.17"
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
