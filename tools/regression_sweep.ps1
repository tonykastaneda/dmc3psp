# A quick regression sweep: Missions 1-20 (their start rooms), a Bloody Palace floor and Secret Mission 1, 600 frames
# each with killevery 30, fastwaves 1 and notrace 1 (the test logs off). Prints crash / heap / Dante's room, position and
# enemies alive / frame rate / HUD for each; shots are analysis/testroom/rs_*_300.png and _580.png.
# Usage: pwsh -File tools/regression_sweep.ps1 (sets DMC3_SLOT=cl unless it's set).
if (-not $env:DMC3_SLOT) { $env:DMC3_SLOT = 'cl' }
$perf = "E:\SYSTEMS\PPSSPP\memstick\PSP\GAME\DMC3TESTROOM_$($env:DMC3_SLOT)\PERF.TXT"
$runs = @()
foreach ($m in 1..20) { $runs += ,@("rs_m$m", '--mission', "$m") }
$runs += ,@('rs_bp', '--mission', '21')
$runs += ,@('rs_sm1', '--mission', '4', '--room', '600')   # Secret Mission n is room 599 + n
$out = @()
foreach ($r in $runs) {
    $name = $r[0]; $rest = $r[1..($r.Count - 1)]
    $start = Get-Date
    $o = python tools/ppsspp_scene.py $name @rest --frames 600 --shot 300 --shot 580 --line 'realdt 0' --line 'killevery 30' --line 'fastwaves 1' --line 'notrace 1' --timeout 200 2>&1
    $crash = ($o | Select-String 'PPSSPP exited|timed out').Line
    $heap = ($o | Select-String '^heap').Line -replace 'heap free_kb=', '' -replace ' largest_kb=', '/'
    $own = ($o | Select-String '^own=').Line
    $pos = if ($own -match 'room=(\S+) pos=(\S+) alive=(\d+)') { "room $($matches[1]) pos $($matches[2]) alive $($matches[3])" } else { 'no own line' }
    $hud = ($o | Select-String '^hud').Line -replace '^hud ', ''
    $rows = @()
    if ((Test-Path $perf) -and (Get-Item $perf).LastWriteTime -gt $start) {
        $rows = Get-Content $perf | Select-Object -Skip 2 | ForEach-Object { $c = @($_ -split '\s+' | Where-Object { $_ -ne '' }); [int]$c[1] }
    }
    $fps = if ($rows.Count) { "fps avg $([math]::Round(($rows | Measure-Object -Average).Average,1)) min $(($rows | Measure-Object -Minimum).Minimum)" } else { 'no perf' }
    $out += "$name | $crash | heap $heap | $pos | $fps | $($hud.Substring(0,[Math]::Min(40,$hud.Length)))"
}
$out
