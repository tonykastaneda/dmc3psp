[CmdletBinding()]
param(
    [string]$Emulator = "E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe",
    [int]$TimeoutSeconds = 45,
    [switch]$CaptureScreenshot
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Eboot = Join-Path $Root "build\psp\EBOOT.PBP"
$EmuDir = Split-Path -Parent $Emulator
$GameDir = Join-Path $EmuDir "memstick\PSP\GAME\DMC3PSP"
$Marker = Join-Path $GameDir "BOOT_OK.TXT"
$EvidenceDir = Join-Path $Root "analysis\ppsspp"
$EmulatorLog = Join-Path $EvidenceDir "ppsspp.log"

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $Message
    Add-Content -LiteralPath (Join-Path $Root "TIME.txt") -Value $line -Encoding utf8
    Write-Host $line
}

function Save-ProcessScreenshot($Process) {
    Add-Type -AssemblyName System.Drawing
    Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class Dmc3PspWindow {
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
}
'@
    $Process.Refresh()
    $rect = New-Object Dmc3PspWindow+RECT
    if (-not [Dmc3PspWindow]::GetWindowRect($Process.MainWindowHandle, [ref]$rect)) {
        throw "Unable to get PPSSPP window bounds"
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -le 0 -or $height -le 0) { throw "Invalid PPSSPP window dimensions" }
    $bitmap = New-Object System.Drawing.Bitmap $width, $height
    try {
        $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
        try { $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size) }
        finally { $graphics.Dispose() }
        $destination = Join-Path $EvidenceDir "model-screen.png"
        $bitmap.Save($destination, [System.Drawing.Imaging.ImageFormat]::Png)
        Write-Log "Saved PPSSPP screenshot to $destination"
    }
    finally { $bitmap.Dispose() }
}

if (-not (Test-Path -LiteralPath $Emulator -PathType Leaf)) { throw "PPSSPP not found: $Emulator" }
if (-not (Test-Path -LiteralPath $Eboot -PathType Leaf)) { throw "Build EBOOT.PBP first: $Eboot" }
New-Item -ItemType Directory -Force -Path $GameDir, $EvidenceDir | Out-Null
Copy-Item -LiteralPath $Eboot -Destination (Join-Path $GameDir "EBOOT.PBP") -Force
Set-Content -LiteralPath (Join-Path $GameDir "AUTOEXIT.TXT") -Value "automated" -Encoding ascii
if (Test-Path -LiteralPath $Marker) { Remove-Item -LiteralPath $Marker -Force }
if (Test-Path -LiteralPath $EmulatorLog) { Remove-Item -LiteralPath $EmulatorLog -Force }

Write-Log "Deploying PSP EBOOT to $GameDir and launching PPSSPP for an automated boot test."
$arguments = '--log="{0}" --loglevel=3 --escape-exit "{1}"' -f $EmulatorLog, (Join-Path $GameDir "EBOOT.PBP")
$process = Start-Process -FilePath $Emulator -ArgumentList $arguments -WorkingDirectory $EmuDir -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
$passed = $false
try {
    while ([DateTime]::UtcNow -lt $deadline) {
        if (Test-Path -LiteralPath $Marker) {
            $content = Get-Content -LiteralPath $Marker -Raw -ErrorAction SilentlyContinue
            if ($content -and $content.StartsWith("DMC3PSP_BOOT_OK") -and $content.Contains("gameplay self-test") -and $content.Contains("model render") -and $content.Contains("30fps target passed")) {
                $passed = $true
                break
            }
        }
        if ($process.HasExited) { break }
        Start-Sleep -Milliseconds 250
        $process.Refresh()
    }
    if ($passed -and $CaptureScreenshot) {
        try { Save-ProcessScreenshot $process }
        catch { Write-Log "Screenshot capture failed: $($_.Exception.Message)" }
    }
}
finally {
    if ($passed -and -not $process.HasExited) {
        [void]$process.WaitForExit(4000)
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Write-Log "Requesting PPSSPP test window closure (PID $($process.Id))."
        [void]$process.CloseMainWindow()
        if (-not $process.WaitForExit(5000)) {
            Write-Log "PPSSPP test process did not close after the window request; stopping only test PID $($process.Id)."
            Stop-Process -Id $process.Id -Force
            $process.WaitForExit()
        }
    }
}

Copy-Item -LiteralPath $Marker -Destination (Join-Path $EvidenceDir "BOOT_OK.TXT") -Force
$Performance = Join-Path $GameDir "PERF.TXT"
if (Test-Path -LiteralPath $Performance) {
    Copy-Item -LiteralPath $Performance -Destination (Join-Path $EvidenceDir "PERF.TXT") -Force
    Write-Log "PSP performance evidence: $((Get-Content -LiteralPath $Performance -Raw).Trim())"
}
if (-not $passed) {
    Write-Log "PPSSPP boot test FAILED: no valid combined gameplay/model/30fps marker within $TimeoutSeconds seconds."
    throw "PPSSPP boot test failed"
}
Write-Log "PPSSPP boot test PASSED: PSP code wrote DMC3PSP_BOOT_OK; evidence copied to analysis/ppsspp/BOOT_OK.TXT."
