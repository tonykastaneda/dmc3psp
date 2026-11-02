[CmdletBinding()]
param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Build = Join-Path $Root "build\psp"
$Objects = Join-Path $Build "obj"
$HostBin = Join-Path $Root ".tools\bin"
$PSPSDK = Get-ChildItem -LiteralPath (Join-Path $Root ".tools\src") -Directory -Filter "pspsdk-*" |
    Sort-Object Name | Select-Object -First 1

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $Message
    Add-Content -LiteralPath (Join-Path $Root "TIME.txt") -Value $line -Encoding utf8
    Write-Host $line
}

function Invoke-Native([string]$File, [string[]]$Arguments) {
    Write-Log ("RUN: {0} {1}" -f $File, ($Arguments -join " "))
    & $File @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$File failed with exit code $LASTEXITCODE"
    }
}

if (-not $PSPSDK) { throw "Pinned PSPSDK source is missing under .tools/src" }
$Clang = (Get-Command clang -ErrorAction Stop).Source
$CrossBin = Join-Path $Root ".tools\pspdev\bin"
$env:PATH = "$CrossBin;$env:PATH"
$CrossCC = Join-Path $CrossBin "psp-gcc.exe"
$Lld = Join-Path $CrossBin "psp-ld.exe"
if (-not (Test-Path -LiteralPath $CrossCC) -or -not (Test-Path -LiteralPath $Lld)) {
    throw "Repo-local PSP GCC toolchain is missing under .tools/pspdev"
}
$Python = (Get-Command python -ErrorAction Stop).Source
$SDK = $PSPSDK.FullName
New-Item -ItemType Directory -Force -Path $Build, $Objects, $HostBin | Out-Null

$Fixup = Join-Path $HostBin "psp-fixup-imports.exe"
$HostTools = Join-Path $SDK "tools"
Invoke-Native $Clang @(
    "-std=c99", "-O2", "-D_CRT_SECURE_NO_WARNINGS",
    "-I$(Join-Path $Root 'tools\pspsdk')", "-I$HostTools",
    (Join-Path $HostTools "psp-fixup-imports.c"),
    (Join-Path $HostTools "getopt_long.c"),
    (Join-Path $HostTools "sha1.c"),
    "-o", $Fixup
)

$Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
if (-not $Iso) { throw "The source ISO is required to prepare the boot texture" }
Invoke-Native $Python @((Join-Path $Root "tools\prepare_model_runtime.py"), $Iso.FullName, "--output", (Join-Path $Build "generated"))

$Common = @(
    "-G0", "-mno-abicalls", "-ffreestanding", "-fno-builtin", "-fno-strict-aliasing", "-fno-stack-protector", "-fno-pic",
    "-O2", "-Wall", "-Wextra"
)
if ($Configuration -eq "Debug") { $Common += @("-O0", "-g") }
$Includes = @("-I$(Join-Path $Build 'generated')", "-I$(Join-Path $Root 'engine\gameplay')", "-I$(Join-Path $Root 'engine\render')", "-I$(Join-Path $SDK 'src\base')", "-I$(Join-Path $SDK 'src\user')", "-I$(Join-Path $SDK 'src\kernel')")

function Build-Object([string]$Source, [string]$Name, [string[]]$Defines = @()) {
    $Output = Join-Path $Objects "$Name.o"
    Invoke-Native $CrossCC ($Common + $Includes + $Defines + @("-c", $Source, "-o", $Output))
    return $Output
}

$Linked = @()
$Linked += Build-Object (Join-Path $Root "psp\startup.c") "startup"
$Linked += Build-Object (Join-Path $Root "psp\main.c") "main"
$Linked += Build-Object (Join-Path $Root "engine\gameplay\difficulty.c") "difficulty"
$Linked += Build-Object (Join-Path $Root "engine\render\model.c") "model"
$Linked += Build-Object (Join-Path $SDK "src\debug\font.c") "font"
$Linked += Build-Object (Join-Path $SDK "src\modinfo\modinfo.S") "modinfo"
$Linked += Build-Object (Join-Path $SDK "src\display\sceDisplay.S") "display" @(
    "-DF_sceDisplay_0000", "-DF_sceDisplay_0001", "-DF_sceDisplay_0006", "-DF_sceDisplay_0014"
)
$Linked += Build-Object (Join-Path $SDK "src\ctrl\sceCtrl.S") "ctrl" @(
    "-DF_sceCtrl_0000", "-DF_sceCtrl_0001", "-DF_sceCtrl_0003", "-DF_sceCtrl_0007"
)
$Linked += Build-Object (Join-Path $SDK "src\user\IoFileMgrForUser.S") "io" @(
    "-DF_IoFileMgrForUser_0000", "-DF_IoFileMgrForUser_0007", "-DF_IoFileMgrForUser_0009", "-DF_IoFileMgrForUser_0013"
)
$Linked += Build-Object (Join-Path $SDK "src\user\LoadExecForUser.S") "loadexec" @(
    "-DF_LoadExecForUser_0000", "-DF_LoadExecForUser_0003"
)
$Linked += Build-Object (Join-Path $SDK "src\user\ThreadManForUser.S") "threadman" @(
    "-DF_ThreadManForUser_0000", "-DF_ThreadManForUser_0082"
)

$Elf = Join-Path $Build "dmc3psp.elf"
Invoke-Native $Lld (@("-m", "elf_mipsallegrexel_psp", "-z", "max-page-size=128", "-T", (Join-Path $Root "psp\linker.ld"), "-o", $Elf) + $Linked)
Invoke-Native $Fixup @("-v", $Elf)
Invoke-Native $Python @((Join-Path $Root "tools\pack_pbp.py"), "--elf", $Elf, "--output", (Join-Path $Build "EBOOT.PBP"))
Write-Log "PSP bootstrap build completed: $Build\EBOOT.PBP"
