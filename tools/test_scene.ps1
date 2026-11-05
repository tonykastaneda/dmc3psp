param(
    [Parameter(Mandatory)][string]$Script,
    [Parameter(Mandatory)][string]$Out,
    [int]$TimeoutSeconds = 180,
    [switch]$MissionClear,
    [string]$Emulator = "E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe"
)

# Capture only the game's framebuffer. Own a separate game directory and stop only our process.
$ErrorActionPreference = 'Stop'
$Root = Split-Path -Parent $PSScriptRoot
$Game = Join-Path (Split-Path -Parent $Emulator) 'memstick\PSP\GAME\DMC3SCENE'
New-Item -ItemType Directory -Force -Path $Game, $Out | Out-Null
Get-ChildItem -LiteralPath (Join-Path $Root 'build\testroom') -File |
    Where-Object { $_.Extension -in '.BIN', '.PBP' } |
    Copy-Item -Destination $Game -Force
Get-ChildItem -LiteralPath $Game -File |
    Where-Object { $_.Name -match '^(SHOT_.*\.BMP|MENU_CLEAR\.BMP|FOCUS_DONE\.TXT|CMP_OUT\.TXT|SEAL_LOG\.TXT|VANGUARD_LOG\.TXT)$' } |
    Remove-Item -Force
Set-Content -LiteralPath (Join-Path $Game 'FOCUS.TXT') -Value 'cmp' -Encoding ascii
Copy-Item -LiteralPath $Script -Destination (Join-Path $Game 'CMP_SCRIPT.TXT') -Force
$Process = Start-Process -FilePath $Emulator -ArgumentList ('--windowed "{0}"' -f (Join-Path $Game 'EBOOT.PBP')) -WindowStyle Hidden -PassThru
$Done = Join-Path $Game 'FOCUS_DONE.TXT'
$Deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$ReachedClear = $false
try {
    while (-not (Test-Path -LiteralPath $Done) -and -not $Process.HasExited -and [DateTime]::UtcNow -lt $Deadline) {
        $BossLog = Join-Path $Game 'VANGUARD_LOG.TXT'
        if ($MissionClear -and (Test-Path -LiteralPath (Join-Path $Game 'MENU_CLEAR.BMP')) -and
            (Test-Path -LiteralPath $BossLog) -and (Select-String -LiteralPath $BossLog -SimpleMatch 'event=mission_clear' -Quiet)) {
            $ReachedClear = $true
            break
        }
        Start-Sleep -Milliseconds 250
    }
    if ($ReachedClear) { Write-Output 'Game reached Mission Clear (game-written log and framebuffer).' }
    elseif (Test-Path -LiteralPath $Done) { Get-Content -LiteralPath $Done }
    else { throw 'Scene did not finish; inspect the collected output.' }
} finally {
    if (-not $Process.HasExited) { Stop-Process -Id $Process.Id -Force }
    Get-ChildItem -LiteralPath $Game -File |
        Where-Object { $_.Name -match '^(SHOT_.*\.BMP|MENU_CLEAR\.BMP|FOCUS_DONE\.TXT|CMP_OUT\.TXT|SEAL_LOG\.TXT|VANGUARD_LOG\.TXT)$' } |
        Copy-Item -Destination $Out -Force
    Remove-Item -LiteralPath (Join-Path $Game 'FOCUS.TXT'), (Join-Path $Game 'CMP_SCRIPT.TXT') -Force -ErrorAction SilentlyContinue
}
