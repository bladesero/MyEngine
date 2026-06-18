param(
    [string]$Project = (Resolve-Path (Join-Path $PSScriptRoot ".."))
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceProject = (Resolve-Path $Project).Path
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("myengine_release_smoke_" + [guid]::NewGuid().ToString("N"))
$testProject = Join-Path $tempRoot "Project"
$publishBase = Join-Path $tempRoot "Published"
$utf8 = New-Object Text.UTF8Encoding($false)

function Write-JsonFile([string]$Path, $Value) {
    [IO.File]::WriteAllText($Path, ($Value | ConvertTo-Json -Depth 20), $utf8)
}

function Copy-Package([string]$Source, [string]$Name) {
    $destination = Join-Path $tempRoot $Name
    Copy-Item -LiteralPath $Source -Destination $destination -Recurse
    return $destination
}

function Assert-PlayerFailure([string]$Package, [string]$Label) {
    $exe = Join-Path $Package "MyEnginePlayer.exe"
    try {
        $process = Start-Process -FilePath $exe -WorkingDirectory $Package `
            -ArgumentList @("--backend", "d3d11") -WindowStyle Hidden -PassThru
    }
    catch {
        Write-Output "PASS expected failure: $Label ($($_.Exception.Message))"
        return
    }
    if (-not $process.WaitForExit(10000)) {
        Stop-Process -Id $process.Id -Force
        throw "Player unexpectedly kept running for failure case: $Label"
    }
    if ($process.ExitCode -eq 0) {
        throw "Player returned success for failure case: $Label"
    }
    Write-Output "PASS expected failure: $Label (exit $($process.ExitCode))"
}

function Assert-PlayerRuns([string]$Package, [string]$Backend, [string]$Scene = "") {
    $exe = Join-Path $Package "MyEnginePlayer.exe"
    $arguments = @("--backend", $Backend)
    if ($Scene) { $arguments += @("--scene", $Scene) }
    $process = Start-Process -FilePath $exe -WorkingDirectory $Package `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru
    Start-Sleep -Seconds 3
    if ($process.HasExited) {
        throw "Published Player exited early for $Backend with code $($process.ExitCode)"
    }
    Stop-Process -Id $process.Id -Force
    $process.WaitForExit()
    $suffix = if ($Scene) { " with CLI scene override" } else { "" }
    Write-Output "PASS published Player $Backend$suffix"
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
    & $xmake.Source f -m release
    if ($LASTEXITCODE -ne 0) { throw "xmake release configure failed." }
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
    $allowed = @(
        "Content.pak", "CookManifest.json", "MyEngine.project.json",
        "MyEnginePlayer.exe", "runtime.dll", "SDL3.dll"
    )
    $actual = @(Get-ChildItem -LiteralPath $package -Force | ForEach-Object Name)
    $unexpected = @($actual | Where-Object { $_ -notin $allowed })
    $missing = @($allowed | Where-Object { $_ -notin $actual })
    if ($unexpected.Count -gt 0 -or $missing.Count -gt 0) {
        throw "package layout mismatch; missing=[$($missing -join ', ')], unexpected=[$($unexpected -join ', ')]"
    }
    if (Test-Path -LiteralPath (Join-Path $package "Content")) {
        throw "published package contains loose Content"
    }

    Write-Output "==> Launch D3D11 and D3D12"
    Assert-PlayerRuns $package "d3d11" "Content/Scenes/Main.scene.json"
    Assert-PlayerRuns $package "d3d12"

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
    Assert-PlayerFailure $missingScene "missing startup scene"

    $missingDll = Copy-Package $package "MissingRuntime"
    Remove-Item -LiteralPath (Join-Path $missingDll "runtime.dll")
    Assert-PlayerFailure $missingDll "missing runtime.dll"

    $invalidConfig = Copy-Package $package "InvalidConfig"
    [IO.File]::WriteAllText((Join-Path $invalidConfig "MyEngine.project.json"), "{ invalid", $utf8)
    Assert-PlayerFailure $invalidConfig "invalid project config"

    Write-Output "[PASS] Release publish smoke passed"
}
finally {
    Set-Location $repoRoot
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
