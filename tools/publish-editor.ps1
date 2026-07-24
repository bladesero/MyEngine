param(
    [string]$Output = (Join-Path (Resolve-Path (Join-Path $PSScriptRoot "..")).Path "Builds\MyEngineEditor-windows-x64"),
    [ValidateSet("debug", "release")]
    [string]$Mode = "release",
    [switch]$Vulkan
)

$ErrorActionPreference = "Stop"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $repoRoot

$xmake = Get-Command xmake -ErrorAction SilentlyContinue
if (-not $xmake) {
    throw "xmake was not found on PATH."
}

$vulkanOption = if ($Vulkan) { "--vulkan=y" } else { "--vulkan=n" }
& $xmake.Source f -y -m $Mode $vulkanOption
if ($LASTEXITCODE -ne 0) { throw "xmake configure failed." }

& $xmake.Source build MyEngineEditor
if ($LASTEXITCODE -ne 0) { throw "Editor build failed." }

& $xmake.Source build MyEnginePlayer
if ($LASTEXITCODE -ne 0) { throw "Player build failed." }

& $xmake.Source build MyEngineCooker
if ($LASTEXITCODE -ne 0) { throw "Cooker build failed." }

& $xmake.Source build MyEngineEditorPackager
if ($LASTEXITCODE -ne 0) { throw "Editor packager build failed." }

& $xmake.Source run MyEngineEditorPackager --output $Output --engine-content (Join-Path $repoRoot "EngineContent") --project-templates (Join-Path $repoRoot "ProjectTemplates") --content (Join-Path $repoRoot "Content")
if ($LASTEXITCODE -ne 0) { throw "Editor packaging failed." }
