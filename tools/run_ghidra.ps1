[CmdletBinding()]
param(
    [switch]$FullAnalysis,
    [string[]]$Addresses = @()
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Analysis = Join-Path $Root "analysis\ghidra"
$Ghidra = Join-Path $Root ".tools\ghidra\ghidra_12.2_PUBLIC"
$Jdk = Get-ChildItem -LiteralPath (Join-Path $Root ".tools\jdk25") -Directory -ErrorAction SilentlyContinue |
    Select-Object -First 1

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f (Get-Date).ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $Message
    Add-Content -LiteralPath (Join-Path $Root "TIME.txt") -Value $line -Encoding utf8
    Write-Host $line
}

function Invoke-Headless([string[]]$Arguments) {
    Write-Log ("RUN Ghidra: {0}" -f ($Arguments -join " "))
    & (Join-Path $Ghidra "support\analyzeHeadless.bat") @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Ghidra failed with exit code $LASTEXITCODE" }
}

if (-not $Jdk) { throw "Repo-local JDK 25 is missing" }
if (-not (Test-Path -LiteralPath (Join-Path $Ghidra "support\analyzeHeadless.bat"))) {
    throw "Repo-local Ghidra 12.2 is missing"
}
$env:JAVA_HOME = $Jdk.FullName
$env:PATH = "$($Jdk.FullName)\bin;$env:PATH"
$Settings = New-Item -ItemType Directory -Force -Path (Join-Path $Root ".tools\ghidra-user\settings")
$Cache = New-Item -ItemType Directory -Force -Path (Join-Path $Root ".tools\ghidra-user\cache")
$Temp = New-Item -ItemType Directory -Force -Path (Join-Path $Root ".tools\ghidra-user\temp")
$env:JAVA_TOOL_OPTIONS = "-Dapplication.settingsdir=$($Settings.FullName) -Dapplication.cachedir=$($Cache.FullName) -Dapplication.tempdir=$($Temp.FullName)"
New-Item -ItemType Directory -Force -Path $Analysis | Out-Null

$Project = Join-Path $Analysis "DMC3.gpr"
if ($FullAnalysis -or -not (Test-Path -LiteralPath $Project)) {
    Invoke-Headless @(
        $Analysis, "DMC3", "-import", (Join-Path $Root "analysis\disc\SLUS_213.61"),
        "-processor", "r5900:LE:32:default", "-cspec", "default", "-overwrite",
        "-analysisTimeoutPerFile", "900", "-max-cpu", "4", "-log", (Join-Path $Analysis "import.log")
    )
}

$Export = Join-Path $Analysis "export"
Invoke-Headless (@(
    $Analysis, "DMC3", "-process", "SLUS_213.61", "-noanalysis",
    "-scriptPath", (Join-Path $Root "tools\ghidra"),
    "-postScript", "ExportDmc3Inventory.java", $Export
) + $Addresses + @("-log", (Join-Path $Analysis "export.log")))
if (-not (Test-Path -LiteralPath (Join-Path $Export "functions.csv"))) {
    throw "Ghidra export did not produce functions.csv; inspect $Analysis\export.log"
}
Write-Log "Ghidra inventory/decompilation export completed: $Export"
