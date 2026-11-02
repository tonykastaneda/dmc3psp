[CmdletBinding()]
param(
    [switch]$FullAnalysis,
    [switch]$SkipGhidra,
    [switch]$SkipPacScan,
    [switch]$SkipRuntime
)

$ErrorActionPreference = "Stop"
$Root = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$Log = Join-Path $Root "TIME.txt"
$Analysis = Join-Path $Root "analysis"
$StatusPath = Join-Path $Analysis "autonomy-status.json"
$Started = [DateTimeOffset]::Now
$Status = [ordered]@{
    started = $Started.ToString("o")
    completed = $false
    result = "running"
    steps = [ordered]@{}
}

function Write-Log([string]$Message) {
    $line = "[{0}] {1}" -f [DateTimeOffset]::Now.ToString("yyyy-MM-dd HH:mm:ss.fff zzz"), $Message
    Add-Content -LiteralPath $Log -Value $line -Encoding utf8
    Write-Host $line
}

function Save-Status {
    New-Item -ItemType Directory -Force -Path $Analysis | Out-Null
    $temporary = "$StatusPath.tmp"
    $Status | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $temporary -Encoding utf8
    Move-Item -LiteralPath $temporary -Destination $StatusPath -Force
}

function Invoke-Step([string]$Name, [scriptblock]$Action) {
    Write-Log "AUTONOMY START: $Name"
    $stepStarted = [DateTimeOffset]::Now
    $Status.steps[$Name] = @{ result = "running"; started = $stepStarted.ToString("o") }
    Save-Status
    try {
        & $Action
        $Status.steps[$Name].result = "passed"
        $Status.steps[$Name].finished = [DateTimeOffset]::Now.ToString("o")
        $Status.steps[$Name].seconds = [math]::Round(([DateTimeOffset]::Now - $stepStarted).TotalSeconds, 3)
        Save-Status
        Write-Log "AUTONOMY PASS: $Name"
    }
    catch {
        $Status.steps[$Name].result = "failed"
        $Status.steps[$Name].error = $_.Exception.Message
        $Status.steps[$Name].finished = [DateTimeOffset]::Now.ToString("o")
        Save-Status
        Write-Log "AUTONOMY FAIL: $Name - $($_.Exception.Message)"
        throw
    }
}

function Invoke-Python([string[]]$Arguments) {
    Write-Log ("RUN: python {0}" -f ($Arguments -join " "))
    & python @Arguments
    if ($LASTEXITCODE -ne 0) { throw "Python failed with exit code $LASTEXITCODE" }
}

try {
    Write-Log "Autonomous DMC3 PSP port verification pass started."
    $Iso = Get-ChildItem -LiteralPath $Root -File -Filter "*.iso" | Select-Object -First 1
    if (-not $Iso) { throw "No PS2 ISO found in $Root" }
    $Status.iso = $Iso.FullName
    Save-Status

    Invoke-Step "verify-disc" {
        if ($Iso.Length -ne 4410736640) { throw "Unexpected ISO size: $($Iso.Length)" }
        $hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $Iso.FullName).Hash.ToLowerInvariant()
        if ($hash -ne "78ad0f94c04f3632b7d8bb45a8db5ece90113c600c5e5e624a55c443745e41b0") {
            throw "Unexpected ISO SHA-256: $hash"
        }
        $Status.iso_sha256 = $hash
        Save-Status
        Write-Log "Verified expected DMC3 Special Edition ISO."
    }

    Invoke-Step "extract-boot" {
        Invoke-Python @((Join-Path $Root "tools\dmc3_image.py"), "extract-boot", $Iso.FullName, "--output", (Join-Path $Analysis "disc"))
    }

    Invoke-Step "inventory-afs" {
        Invoke-Python @((Join-Path $Root "tools\dmc3_image.py"), "inventory", $Iso.FullName, "--output", $Analysis)
    }

    if (-not $SkipPacScan) {
        Invoke-Step "inventory-pac" {
            Invoke-Python @((Join-Path $Root "tools\dmc3_pac.py"), "inventory-gdata", $Iso.FullName, "--output", (Join-Path $Analysis "pac"))
        }
    }

    if (-not $SkipPacScan) {
        Invoke-Step "convert-tim2" {
            Invoke-Python @((Join-Path $Root "tools\convert_gdata_tim2.py"), $Iso.FullName, "--output", (Join-Path $Root "build\assets\tim2"))
        }
    }

    if (-not $SkipGhidra) {
        Invoke-Step "ghidra-static-map" {
            $addresses = @("001aa8d0", "00144dd0", "00145258", "00146ac0", "001aabb0", "001aaaf0", "00197da0", "001aabd0", "001aabe0")
            & (Join-Path $Root "tools\run_ghidra.ps1") -FullAnalysis:$FullAnalysis -Addresses $addresses
            if ($LASTEXITCODE -ne 0) { throw "Ghidra wrapper failed with exit code $LASTEXITCODE" }
        }
    }

    Invoke-Step "build-psp" {
        & (Join-Path $Root "tools\build_psp.ps1")
        if ($LASTEXITCODE -ne 0) { throw "PSP build failed with exit code $LASTEXITCODE" }
        Invoke-Python @((Join-Path $Root "tools\pack_pbp.py"), "--validate", (Join-Path $Root "build\psp\EBOOT.PBP"))
    }

    if (-not $SkipRuntime) {
        Invoke-Step "ppsspp-runtime" {
            & (Join-Path $Root "tools\test_ppsspp.ps1") -TimeoutSeconds 30
            if ($LASTEXITCODE -ne 0) { throw "PPSSPP test failed with exit code $LASTEXITCODE" }
        }
    }

    $Status.completed = $true
    $Status.result = "passed"
    $Status.finished = [DateTimeOffset]::Now.ToString("o")
    Save-Status
    Write-Log "Autonomous DMC3 PSP port verification pass completed successfully."
}
catch {
    $Status.completed = $true
    $Status.result = "failed"
    $Status.error = $_.Exception.Message
    $Status.finished = [DateTimeOffset]::Now.ToString("o")
    Save-Status
    Write-Log "Autonomous pass stopped: $($_.Exception.Message)"
    throw
}
