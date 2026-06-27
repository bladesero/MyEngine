$ErrorActionPreference = "Stop"

$root = Split-Path -Parent $PSScriptRoot
$renderer = Join-Path $root "src\Runtime\Renderer"
$backendPatterns = @(
    "D3D11Context.cpp", "D3D11Context.h", "D3D12Context.cpp", "D3D12Context.h",
    "VulkanContext.cpp", "VulkanContext.h",
    "ShaderCompilerD3D11.cpp", "ShaderCompilerD3D11.h",
    "ShaderCompilerD3D12.cpp", "ShaderCompilerD3D12.h"
)
$violations = @()
Get-ChildItem $renderer -Recurse -File -Include *.h,*.cpp | ForEach-Object {
    if ($backendPatterns -contains $_.Name) { return }
    $matches = Select-String -Path $_.FullName -Pattern `
        'Renderer/D3D11Context|Renderer/D3D12Context|Renderer/VulkanContext|<d3d11.h>|<d3d12.h>|<vulkan/|dynamic_cast<D3D|static_cast<D3D|dynamic_cast<Vulkan|static_cast<Vulkan'
    if ($matches) { $violations += $matches }
}
if ($violations.Count -gt 0) {
    $violations | ForEach-Object { Write-Error "$($_.Path):$($_.LineNumber): $($_.Line.Trim())" }
    throw "RHI boundary check failed: backend types leaked outside backend/legacy migration files"
}
Write-Host "RHI boundary check passed (no render-pass backend allowlist)"
