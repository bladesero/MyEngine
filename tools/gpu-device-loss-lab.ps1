param(
    [Parameter(Mandatory = $true)]
    [ValidateScript({ Test-Path -LiteralPath $_ -PathType Container })]
    [string]$Package,

    [Parameter(Mandatory = $true)]
    [ValidateSet("d3d11", "d3d12")]
    [string]$Backend,

    [ValidateRange(15, 900)]
    [int]$TimeoutSeconds = 120,

    [string]$EvidenceDirectory = "",

    [switch]$NonInteractive,

    [string]$FaultMethod = "operator-induced driver/device removal"
)

$ErrorActionPreference = "Stop"
$packageRoot = (Resolve-Path -LiteralPath $Package).Path
$player = Join-Path $packageRoot "MyEnginePlayer.exe"
if (-not (Test-Path -LiteralPath $player -PathType Leaf)) {
    throw "MyEnginePlayer.exe was not found in package: $packageRoot"
}

if ([string]::IsNullOrWhiteSpace($EvidenceDirectory)) {
    $stamp = Get-Date -Format "yyyyMMdd-HHmmss"
    $EvidenceDirectory = Join-Path $packageRoot "lab-evidence\$stamp-$Backend"
}
$evidenceRoot = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $evidenceRoot -Force | Out-Null

$logs = Join-Path $packageRoot "logs"
if (Test-Path -LiteralPath $logs) {
    Remove-Item -LiteralPath $logs -Recurse -Force
}

$controllers = @(Get-CimInstance Win32_VideoController | ForEach-Object {
    [ordered]@{
        name = $_.Name
        pnpDeviceId = $_.PNPDeviceID
        driverVersion = $_.DriverVersion
        driverDate = if ($_.DriverDate) { $_.DriverDate.ToString("o") } else { "" }
        status = $_.Status
    }
})

$metadata = [ordered]@{
    schemaVersion = 1
    startedUtc = (Get-Date).ToUniversalTime().ToString("o")
    completedUtc = ""
    backend = $Backend
    faultMethod = $FaultMethod
    package = $packageRoot
    playerSha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $player).Hash.ToLowerInvariant()
    host = $env:COMPUTERNAME
    os = [Environment]::OSVersion.VersionString
    videoControllers = $controllers
    processExitCode = $null
    report = ""
    reportSha256 = ""
    result = "running"
}

function Write-EvidenceMetadata {
    $metadata.completedUtc = (Get-Date).ToUniversalTime().ToString("o")
    $metadata | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $evidenceRoot "device-loss-lab.json") `
            -Encoding UTF8
}

$oldPath = $env:PATH
$process = $null
try {
    $systemRoot = $env:SystemRoot
    $pathParts = @($packageRoot)
    if ($systemRoot) {
        $pathParts += @((Join-Path $systemRoot "System32"), $systemRoot)
    }
    $env:PATH = $pathParts -join [IO.Path]::PathSeparator

    Write-Output "Starting real device-loss lab: backend=$Backend timeout=${TimeoutSeconds}s"
    Write-Output "This command intentionally does NOT pass --rhi-test-inject-device-loss."
    $process = Start-Process -FilePath $player -WorkingDirectory $packageRoot `
        -ArgumentList @("--backend", $Backend, "--rhi-conformance",
                        "--auto-quit-seconds", $TimeoutSeconds) `
        -WindowStyle Hidden -PassThru

    if (-not $NonInteractive) {
        Write-Output "Induce a real driver/device removal now using the isolated lab's approved method."
        Write-Output "Do not disable the display adapter hosting an interactive desktop session."
    }

    $waitMilliseconds = ($TimeoutSeconds + 15) * 1000
    if (-not $process.WaitForExit($waitMilliseconds)) {
        Stop-Process -Id $process.Id -Force
        throw "Player did not exit within the lab timeout"
    }
    $metadata.processExitCode = $process.ExitCode
    if ($process.ExitCode -ne 3) {
        throw "Player exited with $($process.ExitCode); expected device-loss exit code 3"
    }

    $report = Get-ChildItem -LiteralPath $logs -Filter "crash-*.log" -ErrorAction Stop |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1
    if (-not $report) { throw "Device loss did not produce a crash diagnostic" }
    $content = Get-Content -Raw -LiteralPath $report.FullName
    $required = @(
        "reason=diagnostic: RHI device lost backend=$Backend",
        "build_id=",
        "git_commit=",
        "nativeCode=",
        "generation="
    )
    foreach ($needle in $required) {
        if (-not $content.Contains($needle)) {
            throw "Device-loss report is missing '$needle'"
        }
    }
    $nativeCodeMatch = [regex]::Match($content, "nativeCode=(-?[0-9]+)")
    if (-not $nativeCodeMatch.Success) {
        throw "Device-loss report has no parseable native code"
    }
    $nativeCode = [int64]::Parse($nativeCodeMatch.Groups[1].Value,
                                [Globalization.CultureInfo]::InvariantCulture)
    if ($nativeCode -eq -1 -or
        $content.Contains("release-gate synthetic device removal")) {
        throw "Report came from the synthetic injection path, not a real driver/device loss"
    }

    $copiedReport = Join-Path $evidenceRoot $report.Name
    Copy-Item -LiteralPath $report.FullName -Destination $copiedReport
    $metadata.report = $report.Name
    $metadata.reportSha256 =
        (Get-FileHash -Algorithm SHA256 -LiteralPath $copiedReport).Hash.ToLowerInvariant()
    $metadata.result = "passed"
    Write-EvidenceMetadata
    Write-Output "[PASS] Real $Backend device loss reached the clean-exit path."
    Write-Output "Evidence: $evidenceRoot"
}
catch {
    $metadata.result = "failed: $($_.Exception.Message)"
    Write-EvidenceMetadata
    throw
}
finally {
    $env:PATH = $oldPath
    if ($process -and -not $process.HasExited) {
        Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue
    }
}
