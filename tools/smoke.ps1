param(
    [ValidateSet("debug", "release")]
    [string]$Mode = "debug",

    [switch]$Vulkan,
    [switch]$SkipBuild,
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

function Invoke-Step {
    param(
        [string]$Name,
        [scriptblock]$Action
    )

    Write-Host ""
    Write-Host "==> $Name"
    & $Action
}

function Fail-Smoke {
    param([string]$Message)

    Write-Error $Message
    exit 1
}

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
Set-Location $repoRoot

$xmake = Get-Command xmake -ErrorAction SilentlyContinue
if (-not $xmake) {
    Fail-Smoke "xmake was not found on PATH. Install xmake 2.8+ and retry."
}

Invoke-Step "Check xmake" {
    $output = & $xmake.Source --version 2>&1
    if ($LASTEXITCODE -ne 0) {
        $text = ($output | Out-String).Trim()
        if ($text -match "_xmake_main\.lua") {
            Fail-Smoke @"
xmake is installed but its extracted core scripts are incomplete.

Observed:
$text

Fix the local xmake install/cache, then retry:
  1. Reinstall xmake, or replace the broken executable at '$($xmake.Source)'.
  2. Remove stale extracted caches:
     `$env:TEMP\.xmake0
     `$env:TMP\.xmake0
  3. Run: tools\smoke.ps1
"@
        }
        Fail-Smoke "xmake failed to start:`n$text"
    }
    $output | ForEach-Object { Write-Host $_ }
}

Invoke-Step "Check RHI boundaries" {
    & powershell -NoProfile -ExecutionPolicy Bypass -File `
        (Join-Path $PSScriptRoot "check-rhi-boundaries.ps1")
    if ($LASTEXITCODE -ne 0) {
        Fail-Smoke "RHI boundary check failed."
    }
}

if (-not $SkipBuild) {
    Invoke-Step "Configure ($Mode)" {
        $configureArgs = @("f", "-m", $Mode, ("--vulkan=" + ($(if ($Vulkan) { "y" } else { "n" }))))
        & $xmake.Source @configureArgs
        if ($LASTEXITCODE -ne 0) {
            Fail-Smoke "xmake configure failed."
        }
    }

    Invoke-Step "Build" {
        & $xmake.Source
        if ($LASTEXITCODE -ne 0) {
            Fail-Smoke "xmake build failed."
        }
    }
}

if (-not $SkipTests) {
    Invoke-Step "Run tests" {
        & $xmake.Source run MyEngineTests
        if ($LASTEXITCODE -ne 0) {
            Fail-Smoke "MyEngineTests failed."
        }
    }
}

Write-Host ""
Write-Host "Smoke passed."
