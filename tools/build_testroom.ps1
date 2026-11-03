[CmdletBinding()]
param(
    [switch]$Regenerate
)

# Builds build\testroom\EBOOT.PBP: the sceGu test room (Dante + grey floor +
# white grid). Uses the full PSPSDK under .tools\pspdev (crt0, newlib, libpspgu)
# rather than the freestanding bootstrap path in build_psp.ps1.

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Build = Join-Path $Root "build\testroom"
$Objects = Join-Path $Build "obj"
$Generated = Join-Path $Build "generated"
$CrossBin = Join-Path $Root ".tools\pspdev\bin"
$SDK = Join-Path $Root ".tools\pspdev\psp\sdk"
$Source = Join-Path $Root "psp\testroom"

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $Message
    Add-Content -LiteralPath (Join-Path $Root "TIME.txt") -Value $line -Encoding utf8
    Write-Host $line
}

function Invoke-Native([string]$File, [string[]]$Arguments) {
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) { throw "$File failed with exit code $LASTEXITCODE" }
}

if (-not (Test-Path -LiteralPath (Join-Path $CrossBin "psp-gcc.exe"))) {
    throw "Repo-local PSP toolchain is missing under .tools\pspdev"
}
$env:PATH = "$CrossBin;$env:PATH"
New-Item -ItemType Directory -Force -Path $Build, $Objects, $Generated | Out-Null

$Header = Join-Path $Generated "model_runtime.h"
if ($Regenerate -or -not (Test-Path -LiteralPath $Header)) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    if (-not $Iso) { throw "The source ISO is required to generate the Dante model data" }
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_skinned_model.py"), $Iso.FullName, "--output", $Generated)
}

$StageBin = Join-Path $Build "STAGE.BIN"
if ($Regenerate -or -not (Test-Path -LiteralPath $StageBin)) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_stage.py"), $Iso.FullName, "--output", $StageBin)
}

$CFlags = @(
    "-O2", "-G0", "-Wall", "-Wextra", "-fno-strict-aliasing", "-D_PSP_FW_VERSION=600",
    "-I$(Join-Path $SDK 'include')", "-I$Generated", "-I$Source"
)
$ObjectFiles = @()
foreach ($name in @("main", "player", "skeleton", "motion", "coat", "combat", "stage", "enemy")) {
    $obj = Join-Path $Objects "$name.o"
    Invoke-Native "psp-gcc.exe" ($CFlags + @("-c", (Join-Path $Source "$name.c"), "-o", $obj))
    $ObjectFiles += $obj
}

$Elf = Join-Path $Build "testroom.elf"
Invoke-Native "psp-gcc.exe" (@("-L$(Join-Path $SDK 'lib')", "-Wl,-zmax-page-size=128") + $ObjectFiles + @(
    "-lpspgum", "-lpspgu", "-lpspge", "-lpspdebug", "-lpspdisplay", "-lpspctrl", "-lm",
    "-o", $Elf
))
Invoke-Native "psp-fixup-imports.exe" @($Elf)

# mksfo/pack-pbp are MSYS tools that mishandle Windows paths; run them with
# relative paths from inside the build directory.
$Eboot = Join-Path $Build "EBOOT.PBP"
Push-Location -LiteralPath $Build
try {
    Invoke-Native "mksfoex.exe" @("-d", "MEMSIZE=1", "DMC3 Test Room", "PARAM.SFO")
    Invoke-Native "psp-strip.exe" @("testroom.elf", "-o", "testroom_strip.elf")
    Invoke-Native "pack-pbp.exe" @("EBOOT.PBP", "PARAM.SFO", "NULL", "NULL", "NULL", "NULL", "NULL", "testroom_strip.elf", "NULL")
}
finally { Pop-Location }
Write-Log ("Test room build completed: {0} ({1:N0} bytes)" -f $Eboot, (Get-Item -LiteralPath $Eboot).Length)
