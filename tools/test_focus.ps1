[CmdletBinding()]
param(
    [string]$Emulator = "E:\SYSTEMS\PPSSPP\PPSSPPWindows64.exe",
    [string]$Name = "strafe",
    [int]$TimeoutSeconds = 60
)

# Runs one short scripted scene (FOCUS.TXT, psp/testroom/main.c) in PPSSPP and prints
# its numbers; screenshots go to analysis\testroom. Takes seconds, unlike the full
# autotest (test_testroom.ps1).

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$EmuDir = Split-Path -Parent $Emulator
$GameDir = Join-Path $EmuDir "memstick\PSP\GAME\DMC3TESTROOM$(if ($env:DMC3_SLOT) { "_$($env:DMC3_SLOT)" } else { '' })"
$Evidence = Join-Path $Root "analysis\testroom"
Copy-Item -LiteralPath (Join-Path $Root "build\testroom\EBOOT.PBP") -Destination (Join-Path $GameDir "EBOOT.PBP") -Force
$Report = Join-Path $GameDir "FOCUS_DONE.TXT"
Remove-Item -LiteralPath $Report -Force -ErrorAction SilentlyContinue
Set-Content -LiteralPath (Join-Path $GameDir "FOCUS.TXT") -Value $Name -Encoding ascii
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class FocusWin {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
    [StructLayout(LayoutKind.Sequential)] public struct POINT { public int X, Y; }
    [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool ClientToScreen(IntPtr h, ref POINT p);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
}
"@
[FocusWin]::SetProcessDPIAware() | Out-Null
$ShotFile = Join-Path $GameDir "FOCUS_SHOT.TXT"
Remove-Item -LiteralPath $ShotFile -Force -ErrorAction SilentlyContinue
$process = Start-Process -FilePath $Emulator -ArgumentList ('--windowed --escape-exit "{0}"' -f (Join-Path $GameDir "EBOOT.PBP")) -WorkingDirectory $EmuDir -PassThru
$deadline = [DateTime]::UtcNow.AddSeconds($TimeoutSeconds)
try {
    while ([DateTime]::UtcNow -lt $deadline -and -not $process.HasExited -and -not (Test-Path -LiteralPath $Report)) {
        if (Test-Path -LiteralPath $ShotFile) {
            # The game is holding this frame: capture the window, then let it go on.
            Start-Sleep -Milliseconds 200
            $name = ((Get-Content -LiteralPath $ShotFile -Raw).Trim() -replace '\.BMP$', '').ToLower()
            $process.Refresh()
            $hwnd = $process.MainWindowHandle
            [FocusWin]::SetForegroundWindow($hwnd) | Out-Null
            Start-Sleep -Milliseconds 100
            $rect = New-Object FocusWin+RECT
            [FocusWin]::GetClientRect($hwnd, [ref]$rect) | Out-Null
            $origin = New-Object FocusWin+POINT
            [FocusWin]::ClientToScreen($hwnd, [ref]$origin) | Out-Null
            $bitmap = New-Object System.Drawing.Bitmap $rect.R, $rect.B
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($origin.X, $origin.Y, 0, 0, $bitmap.Size)
            $bitmap.Save((Join-Path $Evidence "$name.png"), [System.Drawing.Imaging.ImageFormat]::Png)
            $graphics.Dispose(); $bitmap.Dispose()
            # The game polls this file, so the emulator can have it open for a moment.
            for ($try = 0; $try -lt 40 -and (Test-Path -LiteralPath $ShotFile); ++$try) {
                try { Remove-Item -LiteralPath $ShotFile -Force -ErrorAction Stop }
                catch { Start-Sleep -Milliseconds 10 }
            }
        }
        Start-Sleep -Milliseconds 50
    }
    Start-Sleep -Milliseconds 300
}
finally {
    if (-not $process.HasExited) {
        $process.CloseMainWindow() | Out-Null
        if (-not $process.WaitForExit(3000)) { Stop-Process -Id $process.Id -Force }
    }
    Remove-Item -LiteralPath (Join-Path $GameDir "FOCUS.TXT") -Force -ErrorAction SilentlyContinue
}
Get-ChildItem -LiteralPath $GameDir -Filter "FOCUS_*.BMP" | ForEach-Object {
    $image = [System.Drawing.Image]::FromFile($_.FullName)
    try { $image.Save((Join-Path $Evidence ($_.BaseName.ToLower() + ".png")), [System.Drawing.Imaging.ImageFormat]::Png) }
    finally { $image.Dispose() }
    Remove-Item -LiteralPath $_.FullName -Force
}
if (-not (Test-Path -LiteralPath $Report)) { Write-Host "No report within $TimeoutSeconds seconds."; exit 1 }
Get-Content -LiteralPath $Report
