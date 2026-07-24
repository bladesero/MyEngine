param(
    [string]$Project = (Resolve-Path (Join-Path $PSScriptRoot "..")),
    [switch]$Vulkan,
    [ValidateRange(1, 86400)]
    [int]$SoakSeconds = 5,
    [ValidateRange(1, 100)]
    [int]$ReloadIterations = 3,
    [ValidateRange(16, 4096)]
    [int]$MaxWorkingSetGrowthMB = 256,
    [switch]$KeepTempOnFailure
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceProject = (Resolve-Path $Project).Path
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("myengine_release_smoke_" + [guid]::NewGuid().ToString("N"))
$testProject = Join-Path $tempRoot "Project"
$publishBase = Join-Path $tempRoot "Published"
$utf8 = New-Object Text.UTF8Encoding($false)
$succeeded = $false

function Write-JsonFile([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 20), $utf8)
}

function Copy-Package([string]$Source, [string]$Name) {
    $destination = Join-Path $tempRoot $Name
    Copy-Item -LiteralPath $Source -Destination $destination -Recurse
    return $destination
}

function Start-PlayerProcess([string]$Package, [string[]]$Arguments) {
    $exe = Join-Path $Package "MyEnginePlayer.exe"
    $oldPath = $Env:PATH
    try {
        $systemRoot = $Env:SystemRoot
        $pathParts = @($Package)
        if ($systemRoot) {
            $pathParts += @(
                (Join-Path $systemRoot "System32"),
                $systemRoot
            )
        }
        $Env:PATH = ($pathParts -join [IO.Path]::PathSeparator)
        return Start-Process -FilePath $exe -WorkingDirectory $Package `
            -ArgumentList $Arguments -WindowStyle Hidden -PassThru
    }
    finally {
        $Env:PATH = $oldPath
    }
}

function Assert-PlayerFailure([string]$Package, [string]$Label, [switch]$AllowLoaderHang) {
    try {
        $process = Start-PlayerProcess $Package @("--backend", "d3d11")
    }
    catch {
        Write-Output "PASS expected failure: $Label ($($_.Exception.Message))"
        return
    }
    if (-not $process.WaitForExit(10000)) {
        Stop-Process -Id $process.Id -Force
        if ($AllowLoaderHang) {
            Write-Output "PASS expected failure: $Label (loader did not reach engine startup)"
            return
        }
        throw "Player unexpectedly kept running for failure case: $Label"
    }
    if ($process.ExitCode -eq 0) {
        throw "Player returned success for failure case: $Label"
    }
    Write-Output "PASS expected failure: $Label (exit $($process.ExitCode))"
}

function Assert-PlayerRuns([string]$Package, [string]$Backend, [string]$Scene = "",
                           [int]$Duration = 3, [bool]$Conformance = $false,
                           [bool]$Performance = $false) {
    $arguments = @("--backend", $Backend, "--auto-quit-seconds", $Duration)
    if ($Conformance) { $arguments += "--rhi-conformance" }
    if ($Scene) { $arguments += @("--scene", $Scene) }
    $performanceReport = Join-Path $Package ("performance-" + $Backend + ".json")
    if ($Performance) {
        $performanceWarmup = [math]::Min(60, [math]::Max(1, $Duration * 10))
        $performanceMinimum = [math]::Min(120, [math]::Max(10, $Duration * 30))
        if (Test-Path -LiteralPath $performanceReport) {
            Remove-Item -LiteralPath $performanceReport -Force
        }
        # Performance acceptance must not inherit the desktop's refresh cadence. Combining VSync with the
        # engine's 60 Hz limiter can alternate short and long frames even when GPU work is well below budget.
        $arguments += @("--vsync", "off",
                        "--performance-report", $performanceReport,
                        "--performance-warmup-frames", $performanceWarmup,
                        "--performance-min-samples", $performanceMinimum)
    }
    $process = Start-PlayerProcess $Package $arguments
    $samples = @()
    while (-not $process.HasExited) {
        $process.Refresh()
        $samples += $process.WorkingSet64
        if (-not $process.WaitForExit(1000) -and $samples.Count -gt ($Duration + 10)) {
            Stop-Process -Id $process.Id -Force
            throw "Published Player did not exit after the configured soak for $Backend"
        }
    }
    if ($process.ExitCode -ne 0) { throw "Published Player failed for $Backend with code $($process.ExitCode)" }
    if ($Performance) {
        if (-not (Test-Path -LiteralPath $performanceReport -PathType Leaf)) {
            throw "Published Player did not write a performance report for $Backend"
        }
        $performanceText = Get-Content -Raw -LiteralPath $performanceReport
        $performanceData = $performanceText | ConvertFrom-Json
        if (-not $performanceData.passed -or $performanceData.schemaVersion -ne 1 -or
            $performanceData.capture.backend -ne $Backend -or
            [string]::IsNullOrWhiteSpace($performanceData.device.adapterName) -or
            [string]::IsNullOrWhiteSpace($performanceData.device.driverVersion) -or
            $performanceData.device.vendorId -eq 0 -or
            [string]::IsNullOrWhiteSpace($performanceData.profile.name) -or
            [string]::IsNullOrWhiteSpace($performanceData.profile.hardwareClass) -or
            $performanceData.profile.source -ne "Content/Config/Performance.profile.json" -or
            $performanceData.summary.sampleCount -lt $performanceMinimum -or
            @($performanceData.samples).Count -ne $performanceData.summary.sampleCount) {
            throw "Published Player performance report failed validation for $Backend " +
                  "(passed=$($performanceData.passed) schema=$($performanceData.schemaVersion) " +
                  "reportedBackend=$($performanceData.capture.backend) " +
                  "adapter=$($performanceData.device.adapterName) " +
                  "driver=$($performanceData.device.driverVersion) " +
                  "profile=$($performanceData.profile.name) " +
                  "profileSource=$($performanceData.profile.source) " +
                  "samples=$($performanceData.summary.sampleCount) required=$performanceMinimum " +
                  "raw=$(@($performanceData.samples).Count) bytes=$($performanceText.Length))"
        }
    }
    if ($samples.Count -gt 1) {
        $growth = (($samples[-1] - $samples[0]) / 1MB)
        if ($growth -gt $MaxWorkingSetGrowthMB) {
            throw "Published Player working set grew by $([math]::Round($growth, 1)) MB for $Backend"
        }
    }
    $suffix = if ($Scene) { " with CLI scene override" } else { "" }
    Write-Output "PASS published Player $Backend$suffix"
}

function Assert-DeviceLossDiagnostic([string]$Package, [string]$Backend) {
    $logs = Join-Path $Package "logs"
    if (Test-Path -LiteralPath $logs) { Remove-Item -LiteralPath $logs -Recurse -Force }
    $arguments = @("--backend", $Backend, "--rhi-conformance",
                   "--rhi-test-inject-device-loss", "--auto-quit-seconds", "10")
    $process = Start-PlayerProcess $Package $arguments
    if (-not $process.WaitForExit(20000)) {
        Stop-Process -Id $process.Id -Force
        throw "Player did not exit after injected device loss for $Backend"
    }
    if ($process.ExitCode -ne 3) {
        throw "Injected device loss returned $($process.ExitCode), expected 3 for $Backend"
    }
    $report = Get-ChildItem -LiteralPath $logs -Filter "crash-*.log" |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $report) { throw "Injected device loss did not create a report for $Backend" }
    $content = Get-Content -Raw -LiteralPath $report.FullName
    $required = @("build_id=", "git_commit=", "reason=diagnostic: RHI device lost backend=$Backend",
                  "reason=removed", "nativeCode=-1", "generation=1")
    foreach ($needle in $required) {
        if (-not $content.Contains($needle)) {
            throw "Device-loss report for $Backend is missing '$needle'"
        }
    }
    Write-Output "PASS injected device-loss diagnostic $Backend (exit 3)"
}

try {
    Write-Output "==> Prepare isolated project"
    New-Item -ItemType Directory -Path $testProject -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $sourceProject "Content") `
        -Destination (Join-Path $testProject "Content") -Recurse
    $projectJson = Get-Content -Raw (Join-Path $sourceProject "MyEngine.project.json") |
        ConvertFrom-Json
    $projectJson.publish.outputDirectory = $publishBase
    $projectJson.publish.target = "windows-x64"
    Write-JsonFile (Join-Path $testProject "MyEngine.project.json") $projectJson

    Set-Location $repoRoot
    $xmake = Get-Command xmake -ErrorAction SilentlyContinue
    if (-not $xmake) { throw "xmake was not found on PATH." }

    Write-Output "==> Configure and build release Player/Cooker"
    $configureArgs = @("f", "-y", "-m", "release", ("--vulkan=" + ($(if ($Vulkan) { "y" } else { "n" }))))
    & $xmake.Source @configureArgs
    if ($LASTEXITCODE -ne 0) { throw "xmake release configure failed." }
    # Public Runtime headers define C++ layouts consumed across runtime.dll and
    # the app executables. xmake's mode switch can retain an otherwise up-to-date
    # DLL while recompiling only a consumer, producing an ABI-mismatched package.
    # Rebuild the DLL first so this release gate always validates one coherent ABI.
    & $xmake.Source build -r MyEngineRuntime
    if ($LASTEXITCODE -ne 0) { throw "release Runtime rebuild failed." }
    & $xmake.Source build MyEngineRuntimeLinkProbe
    if ($LASTEXITCODE -ne 0) { throw "release Runtime ABI link probe build failed." }
    & $xmake.Source run MyEngineRuntimeLinkProbe
    if ($LASTEXITCODE -ne 0) { throw "release Runtime ABI link probe failed." }
    # Build the runtime-linked icon generator first.  A stale tool can otherwise
    # be launched by a dependent target after runtime.dll has changed ABI.
    & $xmake.Source build MyEngineIconTool
    if ($LASTEXITCODE -ne 0) {
        # Windows Defender/indexers can briefly retain the generated ICO while
        # switching the same checkout from debug to release. Retry the
        # idempotent target once; a deterministic build failure still surfaces.
        Write-Warning "IconTool build failed once; retrying after transient file contention."
        Start-Sleep -Milliseconds 500
        & $xmake.Source build MyEngineIconTool
    }
    if ($LASTEXITCODE -ne 0) { throw "release IconTool build failed." }
    & $xmake.Source build MyEnginePlayer
    if ($LASTEXITCODE -ne 0) { throw "release Player build failed." }
    & $xmake.Source build MyEngineCooker
    if ($LASTEXITCODE -ne 0) { throw "release Cooker build failed." }

    Write-Output "==> Publish isolated project"
    & $xmake.Source run MyEngineCooker --project $testProject
    if ($LASTEXITCODE -ne 0) { throw "release project publish failed." }
    $safeName = ($projectJson.name -replace '[^A-Za-z0-9_-]', '_')
    $package = Join-Path $publishBase ($safeName + "-windows-x64")
    if (-not (Test-Path -LiteralPath $package -PathType Container)) {
        throw "published package was not created: $package"
    }

    Write-Output "==> Validate package layout"
    $runtimeManifestPath = Join-Path $package "RuntimeDependencies.json"
    $runtimeManifest = Get-Content -Raw $runtimeManifestPath | ConvertFrom-Json
    $runtimeFiles = @($runtimeManifest.files | ForEach-Object { $_.file })
    $allowed = @(
        "Content.pak", "CookManifest.json", "RuntimeDependencies.json",
        "MyEngine.project.json", "SBOM.spdx.json", "ThirdPartyNotices.json"
    ) + $runtimeFiles
    $actual = @(Get-ChildItem -LiteralPath $package -Force | ForEach-Object Name)
    $unexpected = @($actual | Where-Object { $_ -notin $allowed })
    $missing = @($allowed | Where-Object { $_ -notin $actual })
    if ($unexpected.Count -gt 0 -or $missing.Count -gt 0) {
        throw "package layout mismatch; missing=[$($missing -join ', ')], unexpected=[$($unexpected -join ', ')]"
    }
    if (Test-Path -LiteralPath (Join-Path $package "Content")) {
        throw "published package contains loose Content"
    }
    $cookManifest = Get-Content -Raw (Join-Path $package "CookManifest.json") |
        ConvertFrom-Json
    $sbom = Get-Content -Raw (Join-Path $package "SBOM.spdx.json") | ConvertFrom-Json
    $notices = Get-Content -Raw (Join-Path $package "ThirdPartyNotices.json") | ConvertFrom-Json
    if ($sbom.spdxVersion -ne "SPDX-2.3" -or @($sbom.packages).Count -lt 8 -or
        @($notices.packages).Count -ne @($sbom.packages).Count) {
        throw "package SBOM or third-party license inventory is incomplete"
    }
    $expectedBackends = if ($Vulkan) { "d3d11,d3d12,vulkan" } else { "d3d11,d3d12" }
    if ($cookManifest.version -ne 2 -or $cookManifest.hashAlgorithm -ne "sha256" -or
        (@($cookManifest.requiredBackends) -join ",") -ne $expectedBackends) {
        throw "CookManifest v2 compatibility contract is incomplete"
    }
    foreach ($dependency in $runtimeManifest.files) {
        $actualHash = (Get-FileHash -Algorithm SHA256 `
            -LiteralPath (Join-Path $package $dependency.file)).Hash.ToLowerInvariant()
        if ($actualHash -ne $dependency.hash) {
            throw "runtime dependency hash mismatch: $($dependency.file)"
        }
    }
    $cookedPaths = @($cookManifest.files | ForEach-Object { $_.path })
    if (-not ($cookedPaths -contains "Content/Engine/Shaders/Mesh.shader")) {
        throw "CookManifest is missing cooked engine shaders"
    }
    if (@($cookedPaths | Where-Object { $_ -match '\.hlsl(i)?$' }).Count -ne 0) {
        throw "CookManifest contains shader source files"
    }

    Write-Output "==> Launch D3D11 and D3D12"
    Assert-PlayerRuns $package "d3d11" "Content/Scenes/Main.scene.json" $SoakSeconds $true $true
    Assert-PlayerRuns $package "d3d12" "" $SoakSeconds $true $true
    Write-Output "==> Validate device-loss diagnostic and clean exit"
    Assert-DeviceLossDiagnostic $package "d3d11"
    Assert-DeviceLossDiagnostic $package "d3d12"
    Write-Output "==> Repeat scene load/unload process gate ($ReloadIterations iterations)"
    for ($iteration = 0; $iteration -lt $ReloadIterations; ++$iteration) {
        Assert-PlayerRuns $package "d3d11" "Content/Scenes/Main.scene.json" 1
    }
    if ($Vulkan) {
        Write-Output "==> Launch Vulkan"
        Assert-PlayerRuns $package "vulkan"
    }

    Write-Output "==> Validate failure paths"
    $corrupt = Copy-Package $package "CorruptArchive"
    $pak = Join-Path $corrupt "Content.pak"
    $stream = [IO.File]::Open($pak, [IO.FileMode]::Open, [IO.FileAccess]::ReadWrite,
                              [IO.FileShare]::None)
    try {
        $stream.Seek(-1, [IO.SeekOrigin]::End) | Out-Null
        $value = $stream.ReadByte()
        $stream.Seek(-1, [IO.SeekOrigin]::End) | Out-Null
        $stream.WriteByte($value -bxor 0x7f)
    } finally { $stream.Dispose() }
    Assert-PlayerFailure $corrupt "corrupt Content.pak"

    $missingScene = Copy-Package $package "MissingScene"
    $config = Get-Content -Raw (Join-Path $missingScene "MyEngine.project.json") |
        ConvertFrom-Json
    $config.startupScene = "Content/Scenes/Missing.scene.json"
    Write-JsonFile (Join-Path $missingScene "MyEngine.project.json") $config
    Assert-PlayerRuns $missingScene "d3d11" "" 3
    Write-Output "PASS recoverable failure: missing startup scene kept Player responsive"

    $missingDll = Copy-Package $package "MissingRuntime"
    Remove-Item -LiteralPath (Join-Path $missingDll "runtime.dll")
    Assert-PlayerFailure $missingDll "missing runtime.dll" -AllowLoaderHang

    $transitiveName = @($runtimeFiles | Where-Object {
        $_ -notin @("MyEnginePlayer.exe", "runtime.dll", "SDL3.dll")
    } | Select-Object -First 1)
    if ($transitiveName.Count -gt 0) {
        $missingTransitive = Copy-Package $package "MissingTransitiveRuntime"
        Remove-Item -LiteralPath (Join-Path $missingTransitive $transitiveName[0])
        Assert-PlayerFailure $missingTransitive "missing transitive DLL"
    }

    $tamperedDependencies = Copy-Package $package "TamperedDependencies"
    Add-Content -LiteralPath (Join-Path $tamperedDependencies "RuntimeDependencies.json") `
        -Value " " -NoNewline
    Assert-PlayerFailure $tamperedDependencies "tampered runtime dependency manifest"

    $invalidConfig = Copy-Package $package "InvalidConfig"
    [IO.File]::WriteAllText((Join-Path $invalidConfig "MyEngine.project.json"), "{ invalid", $utf8)
    Assert-PlayerFailure $invalidConfig "invalid project config"

    Write-Output "[PASS] Release publish smoke passed"
    $succeeded = $true
}
finally {
    Set-Location $repoRoot
    if ($succeeded -or -not $KeepTempOnFailure) {
        Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
    } else {
        Write-Warning "Release smoke failed; retained diagnostics at $tempRoot"
    }
}
