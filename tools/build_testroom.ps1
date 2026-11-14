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

$StageM1 = Join-Path $Build "STAGE_M1.BIN"
if ($Regenerate -or -not (Test-Path -LiteralPath $StageM1)) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_stage.py"), $Iso.FullName, "--output", $StageM1, "--slot", "272")
}

if ($Regenerate -or -not (Test-Path -LiteralPath (Join-Path $Generated "rooms_runtime.h")) -or
    (Get-Item -LiteralPath (Join-Path $Root "tools\gen_rooms.py")).LastWriteTime -gt
    (Get-Item -LiteralPath (Join-Path $Generated "rooms_runtime.h")).LastWriteTime) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\gen_rooms.py"), $Iso.FullName, "--output", $Build, "--header", (Join-Path $Generated "rooms_runtime.h"))
}

if ($Regenerate -or -not (Test-Path -LiteralPath (Join-Path $Generated "gigapede_runtime.h"))) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_gigapede.py"), $Iso.FullName, "--output", (Join-Path $Generated "gigapede_runtime.h"))
}

$CerberusHeader = Join-Path $Generated "boss_cerberus_runtime.h"
if ($Regenerate -or -not (Test-Path -LiteralPath $CerberusHeader) -or
    (Get-Item -LiteralPath (Join-Path $Root "tools\prepare_cerberus.py")).LastWriteTime -gt (Get-Item -LiteralPath $CerberusHeader).LastWriteTime) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    Invoke-Native (Get-Command python -ErrorAction Stop).Source @((Join-Path $Root "tools\prepare_cerberus.py"), $Iso.FullName, "--output", $Generated)
}

if ($Regenerate -or -not (Test-Path -LiteralPath (Join-Path $Build "ICON0.PNG"))) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_xmb.py"), $Iso.FullName, "--output", $Build, "--focus", "0.6", "--top", "0.02")
}

$LeviathanHeader = Join-Path $Generated "boss_leviathan_runtime.h"
if ($Regenerate -or -not (Test-Path -LiteralPath $LeviathanHeader) -or
    (Get-Item -LiteralPath (Join-Path $Root "tools\prepare_leviathan.py")).LastWriteTime -gt (Get-Item -LiteralPath $LeviathanHeader).LastWriteTime) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    Invoke-Native (Get-Command python -ErrorAction Stop).Source @((Join-Path $Root "tools\prepare_leviathan.py"), $Iso.FullName, "--output", $Generated)
}

$Mission8Header = Join-Path $Generated "mission8_runtime.h"
if ($Regenerate -or -not (Test-Path -LiteralPath $Mission8Header) -or
    (Get-Item -LiteralPath (Join-Path $Root "tools\prepare_mission8.py")).LastWriteTime -gt (Get-Item -LiteralPath $Mission8Header).LastWriteTime) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    Invoke-Native (Get-Command python -ErrorAction Stop).Source @((Join-Path $Root "tools\prepare_mission8.py"), $Iso.FullName, "--output", $Generated)
}

$MenuBin = Join-Path $Build "MENU.BIN"
if ($Regenerate -or -not (Test-Path -LiteralPath $MenuBin) -or -not (Test-Path -LiteralPath (Join-Path $Generated "menu_runtime.h"))) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_menu.py"), $Iso.FullName, "--output", $MenuBin, "--header", (Join-Path $Generated "menu_runtime.h"))
}

if ($Regenerate -or -not (Test-Path -LiteralPath (Join-Path $Generated "hud_runtime.h"))) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_hud.py"), $Iso.FullName, "--output", $Generated)
}

if ($Regenerate -or -not (Test-Path -LiteralPath (Join-Path $Generated "fx_runtime.h"))) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    $Python = (Get-Command python -ErrorAction Stop).Source
    Invoke-Native $Python @((Join-Path $Root "tools\prepare_fx.py"), $Iso.FullName, "--output", $Generated)
}

$NativeFxHeader = Join-Path $Generated 'native_fx_runtime.h'
if ($Regenerate -or -not (Test-Path -LiteralPath $NativeFxHeader) -or
    (Get-Item -LiteralPath (Join-Path $Root 'tools/native_fx.py')).LastWriteTime -gt (Get-Item -LiteralPath $NativeFxHeader).LastWriteTime) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter '*.iso' | Select-Object -First 1
    Invoke-Native (Get-Command python -ErrorAction Stop).Source @((Join-Path $Root 'tools/native_fx.py'), $Iso.FullName, '--output', $Generated)
}

$VanguardHeader = Join-Path $Generated 'vanguard_ai.h'
if ($Regenerate -or -not (Test-Path -LiteralPath $VanguardHeader) -or
    -not (Test-Path -LiteralPath (Join-Path $Generated 'vanguard_fx_runtime.h')) -or
    (Get-Item -LiteralPath (Join-Path $Root 'tools\vanguard_fx.py')).LastWriteTime -gt (Get-Item -LiteralPath $VanguardHeader).LastWriteTime -or
    (Get-Item -LiteralPath (Join-Path $Root 'tools\prepare_vanguard.py')).LastWriteTime -gt (Get-Item -LiteralPath $VanguardHeader).LastWriteTime) {
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter '*.iso' | Select-Object -First 1
    Invoke-Native (Get-Command python -ErrorAction Stop).Source @((Join-Path $Root 'tools\prepare_vanguard.py'), $Iso.FullName, '--output', $Generated)
}

$CFlags = @(
    "-O2", "-G0", "-Wall", "-Wextra", "-fno-strict-aliasing", "-D_PSP_FW_VERSION=600",
    "-I$(Join-Path $SDK 'include')", "-I$Generated", "-I$Source"
)
$ObjectFiles = @()
foreach ($name in @("main", "player", "skeleton", "motion", "coat", "combat", "stage", "enemy", "vanguard", "cerberus_boss", "bullets", "hud", "fx", "nfx", "menu", "movie", "orbs", "leviathan_boss", "mission8", "mission6")) {
    $obj = Join-Path $Objects "$name.o"
    Invoke-Native "psp-gcc.exe" ($CFlags + @("-c", (Join-Path $Source "$name.c"), "-o", $obj))
    $ObjectFiles += $obj
}

$Elf = Join-Path $Build "testroom.elf"
Invoke-Native "psp-gcc.exe" (@("-L$(Join-Path $SDK 'lib')", "-Wl,-zmax-page-size=128") + $ObjectFiles + @(
    "-lpspgum", "-lpspgu", "-lpspge", "-lpspdebug", "-lpspdisplay", "-lpspctrl", "-lpsppower", "-lpspjpeg", "-lpspaudio", "-lpsputility", "-lm",
    "-o", $Elf
))
Invoke-Native "psp-fixup-imports.exe" @($Elf)

# The same objects as a PRX, for PSPLINK: pspsh runs it straight from this folder over USB.
$PrxElf = Join-Path $Build "testroom_prx.elf"
Invoke-Native "psp-gcc.exe" (@("-L$(Join-Path $SDK 'lib')", "-specs=$(Join-Path $SDK 'lib\prxspecs')", "-Wl,-q,-T$(Join-Path $SDK 'lib\linkfile.prx')", "-Wl,-zmax-page-size=128") + $ObjectFiles + @((Join-Path $SDK 'lib\prxexports.o'),
    "-lpspgum", "-lpspgu", "-lpspge", "-lpspdebug", "-lpspdisplay", "-lpspctrl", "-lpsppower", "-lpspjpeg", "-lpspaudio", "-lpsputility", "-lm",
    "-o", $PrxElf
))
Invoke-Native "psp-fixup-imports.exe" @($PrxElf)
Invoke-Native "psp-prxgen.exe" @($PrxElf, (Join-Path $Build "testroom.prx"))

# mksfo/pack-pbp are MSYS tools that mishandle Windows paths; run them with
# relative paths from inside the build directory.
$Eboot = Join-Path $Build "EBOOT.PBP"
Push-Location -LiteralPath $Build
try {
    Invoke-Native "mksfoex.exe" @("-d", "MEMSIZE=1", "Devil May Cry 3 Portable", "PARAM.SFO")
    Invoke-Native "psp-strip.exe" @("testroom.elf", "-o", "testroom_strip.elf")
    # The XMB's icon and background (tools/prepare_xmb.py), when they've been made.
    $Icon0 = if (Test-Path -LiteralPath "ICON0.PNG") { "ICON0.PNG" } else { "NULL" }
    $Pic1 = if (Test-Path -LiteralPath "PIC1.PNG") { "PIC1.PNG" } else { "NULL" }
    Invoke-Native "pack-pbp.exe" @("EBOOT.PBP", "PARAM.SFO", $Icon0, "NULL", "NULL", $Pic1, "NULL", "testroom_strip.elf", "NULL")
}
finally { Pop-Location }
Write-Log ("Test room build completed: {0} ({1:N0} bytes)" -f $Eboot, (Get-Item -LiteralPath $Eboot).Length)
