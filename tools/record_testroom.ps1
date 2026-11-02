[CmdletBinding()]
param(
    [string]$Emulator = "E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe",
    [string]$Ffmpeg = "C:\ffmpeg\bin\ffmpeg.exe",
    [string]$Output = "analysis\testroom\testroom.mp4",
    [int]$TimeoutSeconds = 600,
    [ValidateSet("demo", "motions", "rebellion", "study", "swordmaster")][string]$Mode = "demo"
)

# Runs the test room's RECORD.TXT demo in PPSSPP (the PSP saves every other
# frame as REC####.BMP via GE block transfer), then encodes an H.264 MP4 at
# 2x scale (960x544, nearest-neighbour) and deletes the frames.

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Eboot = Join-Path $Root "build\testroom\EBOOT.PBP"
$EmuDir = Split-Path -Parent $Emulator
$GameDir = Join-Path $EmuDir "memstick\PSP\GAME\DMC3TESTROOM"
$Done = Join-Path $GameDir "RECORD_DONE.TXT"
if (-not [System.IO.Path]::IsPathRooted($Output)) { $Output = Join-Path $Root $Output }

if (-not (Test-Path -LiteralPath $Eboot)) { throw "Build the test room first: $Eboot" }
New-Item -ItemType Directory -Force -Path $GameDir, (Split-Path -Parent $Output) | Out-Null
Copy-Item -LiteralPath $Eboot -Destination (Join-Path $GameDir "EBOOT.PBP") -Force
Get-ChildItem -LiteralPath $GameDir -Filter "REC*.BMP" | Remove-Item -Force
Remove-Item -LiteralPath $Done, (Join-Path $GameDir "AUTOEXIT.TXT") -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath (Join-Path $GameDir "RECORD.TXT") -Value $Mode -Encoding ascii

$process = Start-Process -FilePath $Emulator -ArgumentList ('--escape-exit "{0}"' -f (Join-Path $GameDir "EBOOT.PBP")) -WorkingDirectory $EmuDir -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
try {
    while (-not (Test-Path -LiteralPath $Done) -and -not $process.HasExited -and [DateTime]::UtcNow -lt $deadline) {
        Start-Sleep -Milliseconds 500
    }
    Start-Sleep -Milliseconds 500
}
finally {
    if (-not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(4000)) { Stop-Process -Id $process.Id -Force }
    }
    Remove-Item -LiteralPath (Join-Path $GameDir "RECORD.TXT") -Force -ErrorAction SilentlyContinue
}
if (-not (Test-Path -LiteralPath $Done)) { throw "Recording did not finish within $TimeoutSeconds seconds" }

$frames = (Get-ChildItem -LiteralPath $GameDir -Filter "REC*.BMP").Count
& $Ffmpeg -y -loglevel error -framerate 30 -start_number 0 -i (Join-Path $GameDir "REC%04d.BMP") `
    -vf "scale=960:544:flags=neighbor" -c:v libx264 -profile:v high -pix_fmt yuv420p -crf 18 `
    -movflags +faststart $Output
if ($LASTEXITCODE -ne 0) { throw "ffmpeg failed with exit code $LASTEXITCODE" }
Get-ChildItem -LiteralPath $GameDir -Filter "REC*.BMP" | Remove-Item -Force
Remove-Item -LiteralPath $Done -Force
Write-Host ("Recorded {0} frames -> {1} ({2:N0} bytes)" -f $frames, $Output, (Get-Item -LiteralPath $Output).Length)
