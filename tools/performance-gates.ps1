param(
    [string]$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")),
    [ValidateSet("d3d11", "d3d12", "vulkan")]
    [string]$Backend = "d3d11",
    [ValidateSet("desktop", "console", "mobile")]
    [string]$DeviceProfile = "desktop",
    [ValidateRange(64, 16384)]
    [int]$Width = 1280,
    [ValidateRange(64, 16384)]
    [int]$Height = 720,
    [ValidateSet("cold-start", "warm-gameplay", "scene-transition", "resource-stress")]
    [string[]]$Scenario = @("cold-start", "warm-gameplay", "scene-transition", "resource-stress"),
    [switch]$RequireModern,
    [double]$MaxP95GpuMs = 16.6,
    [double]$MaxP95RenderSubmissionMs = 2.0,
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
        & xmake f -m release ("--vulkan=" + $(if ($Backend -eq "vulkan") { "y" } else { "n" }))
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

$scenarioDefinitions = @(
    @{ Name = "cold-start";       Seconds = $(if ($Quick) { 4 } else { 8 }) },
    @{ Name = "warm-gameplay";    Seconds = $(if ($Quick) { 5 } else { 12 }) },
    @{ Name = "scene-transition"; Seconds = $(if ($Quick) { 10 } else { 20 }) },
    @{ Name = "resource-stress";  Seconds = $(if ($Quick) { 12 } else { 25 }) }
)
$scenarios = @($scenarioDefinitions | Where-Object { $_.Name -in $Scenario })
if ($scenarios.Count -eq 0) { throw "No performance scenarios selected" }

function Get-Percentile([double[]]$Values, [double]$Percentile) {
    if ($Values.Count -eq 0) { return 0.0 }
    $sorted = @($Values | Sort-Object)
    $index = [math]::Ceiling($Percentile * $sorted.Count) - 1
    $index = [math]::Max(0, [math]::Min($index, $sorted.Count - 1))
    return [double]$sorted[$index]
}

$hardwareClass = $null
$adapterKey = $null
$results = @()
foreach ($scenarioDefinition in $scenarios) {
    $name = $scenarioDefinition.Name
    $profile = "Content/Config/PerformanceProfiles/$name.profile.json"
    $report = Join-Path $OutputDirectory ("{0}-{1}-{2}-{3}x{4}.json" -f $name, $Backend, $DeviceProfile,
                                          $Width, $Height)
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue

    $arguments = @(
        "--project", $projectRoot,
        "--backend", $Backend,
        "--device-profile", $DeviceProfile,
        "--width", $Width,
        "--height", $Height,
        "--vsync", "false",
        "--auto-quit-seconds", $scenarioDefinition.Seconds,
        "--performance-profile", $profile,
        "--performance-report", $report
    )
    if ($Quick) {
        $arguments += @("--performance-warmup-frames", "10",
                        "--performance-min-samples", "30")
    }

    Write-Output "Running $name on $Backend/$DeviceProfile at ${Width}x${Height} for $($scenarioDefinition.Seconds)s..."
    $process = Start-Process -FilePath $player -WorkingDirectory $repoRoot `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru
    if (-not $process.WaitForExit(($scenarioDefinition.Seconds + 30) * 1000)) {
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
        $data.capture.backend -ne $Backend -or $data.capture.deviceProfile -ne $DeviceProfile -or
        $data.capture.width -ne $Width -or $data.capture.height -ne $Height -or
        $data.profile.scenario -ne $name -or
        @($data.samples).Count -ne $data.summary.sampleCount -or
        $data.resources.pendingUploadTasks -ne 0 -or
        $data.resources.pendingUploadBytes -ne 0 -or
        $data.resources.failedNativeDescriptorAllocations -ne 0 -or
        @($data.violations).Count -ne 0 -or $unexpectedResourceViolations.Count -ne 0) {
        throw "$name report failed schema, budget, raw-sample, or residual-resource validation"
    }
    if ($RequireModern -and ($data.capture.resolvedRenderPipeline -ne "modern_deferred" -or
                             $data.capture.renderPipelineFallback)) {
        throw "$name did not resolve to Modern Deferred: $($data.capture.renderPipelineReason)"
    }
    $p95RenderSubmissionMs = Get-Percentile @($data.samples | ForEach-Object { [double]$_.renderSubmissionMs }) 0.95
    if ($RequireModern -and ($data.summary.gpuSampleCount -eq 0 -or
                             [double]$data.summary.p95GpuMs -gt $MaxP95GpuMs -or
                             $p95RenderSubmissionMs -gt $MaxP95RenderSubmissionMs)) {
        throw "$name exceeded Modern timing gates: gpuP95=$($data.summary.p95GpuMs)ms " +
              "renderSubmissionP95=${p95RenderSubmissionMs}ms"
    }
    if ($RequireModern -and ($data.renderer.clusterOverflow -ne 0 -or
                             $data.renderer.gpuSceneCandidates -gt 65536 -or
                             $data.renderer.indirectDrawCount -gt 65536 -or
                             $data.renderer.bindlessResourcesUsed -gt $data.renderer.bindlessResourcesCapacity -or
                             $data.renderer.bindlessResourcesCapacity -lt 4096)) {
        throw "$name failed Modern cluster, indirect, or bindless capacity gates"
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
        P95GpuMs = [math]::Round([double]$data.summary.p95GpuMs, 3)
        P95RenderSubmissionMs = [math]::Round($p95RenderSubmissionMs, 3)
        InitialReadyMs = [math]::Round([double]$data.stress.initialSceneReadyMs, 3)
        MaxReloadMs = [math]::Round([double]$data.stress.maxSceneReloadMs, 3)
        Reloads = $data.stress.completedReloads
        Report = $report
    }
}

$results | Format-Table -AutoSize
Write-Output "PASS performance gates: backend=$Backend profile=$DeviceProfile resolution=${Width}x${Height} " +
             "hardwareClass=$hardwareClass adapter=$adapterKey"
