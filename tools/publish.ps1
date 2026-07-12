param(
    [string]$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")),
    [ValidateSet("debug", "release")]
    [string]$Mode = "release",
    [string]$SignTool = "",
    [string[]]$SignArguments = @()
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

if ($SignTool) {
    $manifest = Get-Content -Raw (Join-Path $projectRoot.Path "MyEngine.project.json") | ConvertFrom-Json
    $outputRoot = if ([IO.Path]::IsPathRooted($manifest.publish.outputDirectory)) {
        $manifest.publish.outputDirectory
    } else {
        Join-Path $projectRoot.Path $manifest.publish.outputDirectory
    }
    $safeName = ($manifest.name -replace '[^A-Za-z0-9_-]', '_')
    $package = Join-Path $outputRoot ($safeName + "-" + $manifest.publish.target)
    foreach ($binary in Get-ChildItem -LiteralPath $package -File | Where-Object { $_.Extension -in @(".exe", ".dll") }) {
        & $SignTool @SignArguments $binary.FullName
        if ($LASTEXITCODE -ne 0) { throw "Package signing failed: $($binary.Name)" }
    }
}
