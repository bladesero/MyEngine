param()

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$tempRoot = Join-Path ([IO.Path]::GetTempPath()) ("myengine_editor_smoke_" + [guid]::NewGuid().ToString("N"))
$packageRoot = Join-Path $tempRoot "EditorPackage"
$projectRoot = Join-Path $tempRoot "SmokeProject"

function Assert-True([bool]$Condition, [string]$Message) {
    if (-not $Condition) {
        throw $Message
    }
}

try {
    Set-Location $repoRoot

    Write-Output "==> Build portable editor package"
    powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $repoRoot "tools\publish-editor.ps1") `
        -Mode release -Output $packageRoot
    if ($LASTEXITCODE -ne 0) { throw "publish-editor.ps1 failed" }

    $editorExe = Join-Path $packageRoot "MyEngineEditor.exe"
    Assert-True (Test-Path -LiteralPath $editorExe -PathType Leaf) "Packaged editor executable is missing"
    Assert-True (Test-Path -LiteralPath (Join-Path $packageRoot "ProjectTemplates\Default\Content\Scenes\Main.scene.json") -PathType Leaf) `
        "Packaged editor is missing project templates"

    Write-Output "==> Run packaged editor automation"
    $arguments = @(
        "--backend", "d3d11",
        "--create-project", $projectRoot,
        "--project-name", "SmokeProject",
        "--publish-project"
    )
    Push-Location $packageRoot
    try {
        & $editorExe @arguments
        if ($LASTEXITCODE -ne 0) {
            throw "Packaged editor automation failed with exit code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }

    Write-Output "==> Validate created project"
    $projectJsonPath = Join-Path $projectRoot "MyEngine.project.json"
    $scenePath = Join-Path $projectRoot "Content\Scenes\Main.scene.json"
    Assert-True (Test-Path -LiteralPath $projectJsonPath -PathType Leaf) "Project manifest was not created"
    Assert-True (Test-Path -LiteralPath $scenePath -PathType Leaf) "Startup scene was not created"

    $projectJson = Get-Content -Raw $projectJsonPath | ConvertFrom-Json
    $publishOutput = $projectJson.publish.outputDirectory
    $publishTarget = $projectJson.publish.target
    $packageName = ($projectJson.name -replace '[^A-Za-z0-9_-]', '_') + "-" + $publishTarget
    $publishedProject = Join-Path (Join-Path $projectRoot $publishOutput) $packageName

    Write-Output "==> Validate published project package"
    Assert-True (Test-Path -LiteralPath $publishedProject -PathType Container) "Published project package was not created"
    foreach ($required in @("Content.pak", "CookManifest.json", "RuntimeDependencies.json", "MyEngine.project.json", "MyEnginePlayer.exe")) {
        Assert-True (Test-Path -LiteralPath (Join-Path $publishedProject $required) -PathType Leaf) "Published package is missing $required"
    }

    Write-Output "[PASS] Editor release smoke passed"
}
finally {
    Set-Location $repoRoot
    Remove-Item -LiteralPath $tempRoot -Recurse -Force -ErrorAction SilentlyContinue
}
