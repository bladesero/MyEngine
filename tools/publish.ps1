param(
    [string]$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")),
    [ValidateSet("debug", "release")]
    [string]$Mode = "release"
)

$ErrorActionPreference = "Stop"
$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..")
$projectRoot = Resolve-Path $Project
Set-Location $repoRoot

$xmake = Get-Command xmake -ErrorAction SilentlyContinue
if (-not $xmake) {
    throw "xmake was not found on PATH."
}

& $xmake.Source f -m $Mode
if ($LASTEXITCODE -ne 0) { throw "xmake configure failed." }

& $xmake.Source build MyEnginePlayer
if ($LASTEXITCODE -ne 0) { throw "Player build failed." }
& $xmake.Source build MyEngineCooker
if ($LASTEXITCODE -ne 0) { throw "Cooker build failed." }

& $xmake.Source run MyEngineCooker --project $projectRoot.Path
if ($LASTEXITCODE -ne 0) { throw "Project publish failed." }
