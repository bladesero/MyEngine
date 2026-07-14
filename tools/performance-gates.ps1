param(
    [string]$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")),
    [ValidateSet("d3d11", "d3d12")]
    [string]$Backend = "d3d11",
    [string]$OutputDirectory = "",
    [switch]$Quick,
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$projectRoot = (Resolve-Path $Project).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "Saved\PerformanceReports"
}
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

if (-not $NoBuild) {
    Push-Location $repoRoot
    try {
        & xmake f -m release
        if ($LASTEXITCODE -ne 0) { throw "xmake release configuration failed" }
        # Rebuild the DLL first so an incremental Player link cannot retain an old ABI.
        & xmake build -r MyEngineRuntime
        if ($LASTEXITCODE -ne 0) { throw "MyEngineRuntime build failed" }
        & xmake build MyEnginePlayer
        if ($LASTEXITCODE -ne 0) { throw "MyEnginePlayer build failed" }
    }
    finally { Pop-Location }
}

$player = Join-Path $repoRoot "build\windows\x64\release\MyEnginePlayer.exe"
if (-not (Test-Path -LiteralPath $player -PathType Leaf)) {
    throw "Release Player not found: $player"
}

$scenarios = @(
    @{ Name = "cold-start";       Seconds = $(if ($Quick) { 4 } else { 8 }) },
    @{ Name = "warm-gameplay";    Seconds = $(if ($Quick) { 5 } else { 12 }) },
    @{ Name = "scene-transition"; Seconds = $(if ($Quick) { 10 } else { 20 }) },
    @{ Name = "resource-stress";  Seconds = $(if ($Quick) { 12 } else { 25 }) }
)

$hardwareClass = $null
$adapterKey = $null
$results = @()
foreach ($scenario in $scenarios) {
    $name = $scenario.Name
    $profile = "Content/Config/PerformanceProfiles/$name.profile.json"
    $report = Join-Path $OutputDirectory ("{0}-{1}.json" -f $name, $Backend)
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue

    $arguments = @(
        "--project", $projectRoot,
        "--backend", $Backend,
        "--auto-quit-seconds", $scenario.Seconds,
        "--performance-profile", $profile,
        "--performance-report", $report
    )
    if ($Quick) {
        $arguments += @("--performance-warmup-frames", "10",
                        "--performance-min-samples", "30")
    }

    Write-Output "Running $name on $Backend for $($scenario.Seconds)s..."
    $process = Start-Process -FilePath $player -WorkingDirectory $repoRoot `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(($scenario.Seconds + 30) * 1000)) {
        Stop-Process -Id $process.Id -Force
        throw "$name timed out"
    }
    if ($process.ExitCode -ne 0) { throw "$name failed with exit code $($process.ExitCode)" }
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "$name did not produce $report"
    }

    $data = Get-Content -LiteralPath $report -Raw | ConvertFrom-Json
    $resourceViolations = @($data.resources.violations)
    $unexpectedResourceViolations = @($resourceViolations | Where-Object {
        $name -ne "resource-stress" -or $_ -notin @(
            "asset_cpu_budget_pressure",
            "gpu_resource_residency_pressure",
            "gpu_descriptor_pressure",
            "gpu_native_descriptor_pressure",
            "gpu_upload_backlog_pressure",
            "actor_budget_pressure"
        )
    })
    if ($data.schemaVersion -ne 1 -or -not $data.passed -or
        $data.capture.backend -ne $Backend -or $data.profile.scenario -ne $name -or
        @($data.samples).Count -ne $data.summary.sampleCount -or
        $data.resources.pendingUploadTasks -ne 0 -or
        $data.resources.pendingUploadBytes -ne 0 -or
        $data.resources.failedNativeDescriptorAllocations -ne 0 -or
        @($data.violations).Count -ne 0 -or $unexpectedResourceViolations.Count -ne 0) {
        throw "$name report failed schema, budget, raw-sample, or residual-resource validation"
    }
    if (-not $data.stress.initialReady -or
        $data.stress.initialSceneReadyMs -gt $data.profile.maxInitialSceneReadyMs -or
        ($data.stress.completedReloads -gt 0 -and
         $data.stress.maxSceneReloadMs -gt $data.profile.maxSceneReloadMs)) {
        throw "$name exceeded its initial-ready or scene-reload latency budget"
    }
    if ($data.stress.targetReloads -ne $data.stress.completedReloads -or $data.stress.waiting) {
        throw "$name ended before all requested scene reloads completed"
    }

    $currentAdapterKey = "{0}:{1}:{2}:{3}" -f $data.device.vendorId,
        $data.device.deviceId, $data.device.driverVersion, $data.device.adapterName
    if ($null -eq $hardwareClass) {
        $hardwareClass = $data.profile.hardwareClass
        $adapterKey = $currentAdapterKey
    } elseif ($hardwareClass -ne $data.profile.hardwareClass -or $adapterKey -ne $currentAdapterKey) {
        throw "$name is not comparable with the preceding reports (hardware class or adapter changed)"
    }

    $results += [pscustomobject]@{
        Scenario = $name
        Samples = $data.summary.sampleCount
        P95FrameMs = [math]::Round([double]$data.summary.p95FrameMs, 3)
        MaxFrameMs = [math]::Round([double]$data.summary.maxFrameMs, 3)
        InitialReadyMs = [math]::Round([double]$data.stress.initialSceneReadyMs, 3)
        MaxReloadMs = [math]::Round([double]$data.stress.maxSceneReloadMs, 3)
        Reloads = $data.stress.completedReloads
        Report = $report
    }
}

$results | Format-Table -AutoSize
Write-Output "PASS performance gates: backend=$Backend hardwareClass=$hardwareClass adapter=$adapterKey"
