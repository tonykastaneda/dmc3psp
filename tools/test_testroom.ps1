[CmdletBinding()]
param(
    [string]$Emulator = "E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe",
    [int]$TimeoutSeconds = 120
)

# Deploys build\testroom\EBOOT.PBP to PSP\GAME\DMC3TESTROOM, runs the scripted
# autotest (AUTOEXIT.TXT), and copies the report and screenshots to
# analysis\testroom. Passes only on a PSP-written TESTROOM_OK.TXT.

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Eboot = Join-Path $Root "build\testroom\EBOOT.PBP"
$EmuDir = Split-Path -Parent $Emulator
$GameDir = Join-Path $EmuDir "memstick\PSP\GAME\DMC3TESTROOM"
$Evidence = Join-Path $Root "analysis\testroom"
$Shots = @("SHOT_IDLE", "SHOT_BONES", "SHOT_RUN", "SHOT_SIDE", "SHOT_JUMP", "SHOT_COMBO", "SHOT_STINGER", "SHOT_PROP", "SHOT_DASH", "SHOT_SKY_STAR", "SHOT_BLOCK", "SHOT_DUMMY_HIT", "SHOT_DUMMY_AIR")
$Outputs = @("TESTROOM_OK.TXT", "TESTROOM_FAIL.TXT", "COMBAT_TRACE.TXT", "MEMORY.TXT") + ($Shots | ForEach-Object { "$_.BMP" })

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $Message
    Add-Content -LiteralPath (Join-Path $Root "TIME.txt") -Value $line -Encoding utf8
    Write-Host $line
}

if (-not (Test-Path -LiteralPath $Eboot)) { throw "Build the test room first: $Eboot" }
New-Item -ItemType Directory -Force -Path $GameDir, $Evidence | Out-Null
Copy-Item -LiteralPath $Eboot -Destination (Join-Path $GameDir "EBOOT.PBP") -Force
$StageBin = Join-Path (Split-Path -Parent $Eboot) "STAGE.BIN"
if (Test-Path -LiteralPath $StageBin) { Copy-Item -LiteralPath $StageBin -Destination (Join-Path $GameDir "STAGE.BIN") -Force }
Set-Content -LiteralPath (Join-Path $GameDir "AUTOEXIT.TXT") -Value "automated" -Encoding ascii
foreach ($name in $Outputs) {
    $path = Join-Path $GameDir $name
    if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
}

$log = Join-Path $Evidence "ppsspp.log"
$arguments = '--log="{0}" --loglevel=3 --escape-exit "{1}"' -f $log, (Join-Path $GameDir "EBOOT.PBP")
Write-Log "Launching PPSSPP test-room autotest."
$process = Start-Process -FilePath $Emulator -ArgumentList $arguments -WorkingDirectory $EmuDir -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$report = $null
try {
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited) {
        foreach ($name in @("TESTROOM_OK.TXT", "TESTROOM_FAIL.TXT")) {
            $candidate = Join-Path $GameDir $name
            if (Test-Path -LiteralPath $candidate) { $report = $candidate }
        }
        if ($report) { Start-Sleep -Milliseconds 500; break }
        Start-Sleep -Milliseconds 250
    }
}
finally {
    if (-not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(4000)) { Stop-Process -Id $process.Id -Force }
    }
    Remove-Item -LiteralPath (Join-Path $GameDir "AUTOEXIT.TXT") -Force -ErrorAction SilentlyContinue
}

foreach ($name in $Outputs) {
    $path = Join-Path $GameDir $name
    if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $Evidence -Force }
}
Add-Type -AssemblyName System.Drawing
foreach ($name in $Shots) {
    $bmp = Join-Path $Evidence "$name.BMP"
    if (Test-Path -LiteralPath $bmp) {
        $image = [System.Drawing.Image]::FromFile($bmp)
        try { $image.Save((Join-Path $Evidence "$($name.ToLower()).png"), [System.Drawing.Imaging.ImageFormat]::Png) }
        finally { $image.Dispose() }
        Remove-Item -LiteralPath $bmp -Force
    }
}

if (-not $report) { Write-Log "Test room autotest FAILED: no report within $TimeoutSeconds seconds."; exit 1 }
Get-Content -LiteralPath $report | ForEach-Object { Write-Host "  $_" }
if ((Split-Path -Leaf $report) -ne "TESTROOM_OK.TXT") { Write-Log "Test room autotest FAILED."; exit 1 }
Write-Log "Test room autotest PASSED; screenshots in analysis\testroom."
