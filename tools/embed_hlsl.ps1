# Generate ShaderBytecodeWindows.h / ShaderBytecodeWindows.cpp from HLSL via dxc.exe.
# Called from xmake.lua (on_load) 閿?avoids Lua sandbox io.* and binary corruption.
$ErrorActionPreference = "Stop"

$ProjectDir = $args[0]
if (-not $ProjectDir) {
    Write-Error "Usage: embed_hlsl.ps1 <projectRoot>"
}

function Find-Dxc {
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    )
    foreach ($binroot in $roots) {
        if (-not (Test-Path $binroot)) { continue }
        $dirs = Get-ChildItem -Path $binroot -Directory -ErrorAction SilentlyContinue | Sort-Object Name
        foreach ($d in $dirs) {
            $exe = Join-Path $d.FullName "x64\dxc.exe"
            if (Test-Path $exe) { return $exe }
        }
    }
    return $null
}

function Find-Fxc {
    $roots = @(
        "${env:ProgramFiles(x86)}\Windows Kits\10\bin",
        "$env:ProgramFiles\Windows Kits\10\bin"
    )
    foreach ($binroot in $roots) {
        if (-not (Test-Path $binroot)) { continue }
        $dirs = Get-ChildItem -Path $binroot -Directory -ErrorAction SilentlyContinue | Sort-Object Name
        foreach ($d in $dirs) {
            $exe = Join-Path $d.FullName "x64\fxc.exe"
            if (Test-Path $exe) { return $exe }
        }
    }
    return $null
}

function Assert-Dxbc([byte[]]$bytes, [string]$label) {
    if ($null -eq $bytes -or $bytes.Length -lt 4) {
        throw "empty shader blob: $label"
    }
    if ($bytes[0] -ne 0x44 -or $bytes[1] -ne 0x58 -or $bytes[2] -ne 0x42 -or $bytes[3] -ne 0x43) {
        $g = "{0:x2} {1:x2} {2:x2} {3:x2}" -f $bytes[0], $bytes[1], $bytes[2], $bytes[3]
        throw "not DXBC in $label (got $g)"
    }
}

function Format-CByteArray([byte[]]$data) {
    if ($null -eq $data -or $data.Length -eq 0) { return "    0x00`n" }
    $lines = @()
    $line = @()
    for ($i = 0; $i -lt $data.Length; $i++) {
        $line += ("0x{0:x2}" -f $data[$i])
        if ($line.Length -ge 14) {
            $lines += "    " + ($line -join ", ") + ","
            $line = @()
        }
    }
    if ($line.Length -gt 0) {
        $lines += "    " + ($line -join ", ")
    }
    return ($lines -join "`n") + "`n"
}

$dxc = Find-Dxc
if (-not $dxc) { throw "dxc.exe not found (install Windows SDK x64 tools)" }

$units = @(
    @{ Symbol = "Triangle"; RelPath = "src\Runtime\Renderer\Shaders\Triangle.hlsl" },
    @{ Symbol = "Mesh"; RelPath = "src\Runtime\Renderer\Shaders\Mesh.hlsl" },
    @{ Symbol = "ShadowDepth"; RelPath = "src\Runtime\Renderer\Shaders\ShadowDepth.hlsl" },
    @{ Symbol = "ShadowedMainPass"; RelPath = "src\Runtime\Renderer\Shaders\ShadowedMainPass.hlsl" },
    @{ Symbol = "PostProcessFXAA"; RelPath = "src\Runtime\Renderer\Shaders\PostProcessFXAA.hlsl" },
    @{ Symbol = "PostProcessSSAO"; RelPath = "src\Runtime\Renderer\Shaders\PostProcessSSAO.hlsl" },
    @{ Symbol = "PostProcessSSAOBlur"; RelPath = "src\Runtime\Renderer\Shaders\PostProcessSSAOBlur.hlsl" },
    @{ Symbol = "AtmosphereCubemap"; RelPath = "src\Runtime\Renderer\Shaders\AtmosphereCubemap.hlsl" },
    @{ Symbol = "EnvironmentMipmap"; RelPath = "src\Runtime\Renderer\Shaders\EnvironmentMipmap.hlsl" }
)

$computeUnits = @(
    @{ Symbol = "AtmosphereSH"; RelPath = "src\Runtime\Renderer\Shaders\AtmosphereSH.hlsl" }
)

$genDir = Join-Path $ProjectDir "build\hlsl_generated"
$csoDir = Join-Path $genDir "cso"
New-Item -ItemType Directory -Force -Path $csoDir | Out-Null

foreach ($u in $units) {
    $src = Join-Path $ProjectDir $u.RelPath
    if (-not (Test-Path $src)) { throw "missing HLSL: $src" }
    $base = Join-Path $csoDir $u.Symbol
    $vsCso = $base + "_vs.cso"
    $psCso = $base + "_ps.cso"
    & $dxc "-nologo" "-T" "vs_5_0" "-E" "VSMain" "-Fo" $vsCso $src
    if ($LASTEXITCODE -ne 0) { throw "dxc VS failed for $($u.Symbol)" }
    & $dxc "-nologo" "-T" "ps_5_0" "-E" "PSMain" "-Fo" $psCso $src
    if ($LASTEXITCODE -ne 0) { throw "dxc PS failed for $($u.Symbol)" }
}

foreach ($u in $computeUnits) {
    $src = Join-Path $ProjectDir $u.RelPath
    if (-not (Test-Path $src)) { throw "missing HLSL: $src" }
    $csCso = Join-Path $csoDir ($u.Symbol + "_cs.cso")
    $fxc = Find-Fxc
    if (-not $fxc) { throw "fxc.exe not found (install Windows SDK x64 tools)" }
    & $fxc "-nologo" "-T" "cs_5_0" "-E" "CSMain" "-Fo" $csCso $src
    if ($LASTEXITCODE -ne 0) { throw "fxc CS failed for $($u.Symbol)" }
}

$hPath = Join-Path $genDir "ShaderBytecodeWindows.h"
$hLines = @("#pragma once", "#include <cstddef>", "")
foreach ($u in $units) {
    $s = $u.Symbol
    $hLines += "extern const unsigned char k_${s}VsBytecode[];"
    $hLines += "extern const std::size_t k_${s}VsBytecodeSize;"
    $hLines += "extern const unsigned char k_${s}PsBytecode[];"
    $hLines += "extern const std::size_t k_${s}PsBytecodeSize;"
    $hLines += ""
}
foreach ($u in $computeUnits) {
    $s = $u.Symbol
    $hLines += "extern const unsigned char k_${s}CsBytecode[];"
    $hLines += "extern const std::size_t k_${s}CsBytecodeSize;"
    $hLines += ""
}
$utf8NoBom = New-Object System.Text.UTF8Encoding $false
[System.IO.File]::WriteAllLines($hPath, $hLines, $utf8NoBom)

$cppPath = Join-Path $genDir "ShaderBytecodeWindows.cpp"
$sb = New-Object System.Text.StringBuilder
[void]$sb.AppendLine('#include "ShaderBytecodeWindows.h"')
[void]$sb.AppendLine()

foreach ($u in $units) {
    $s = $u.Symbol
    $base = Join-Path $csoDir $s
    $vsPath = $base + "_vs.cso"
    $psPath = $base + "_ps.cso"
    $vs = [System.IO.File]::ReadAllBytes($vsPath)
    $ps = [System.IO.File]::ReadAllBytes($psPath)
    Assert-Dxbc $vs $vsPath
    Assert-Dxbc $ps $psPath

    [void]$sb.AppendLine("const unsigned char k_${s}VsBytecode[] = {")
    [void]$sb.Append((Format-CByteArray $vs))
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("const std::size_t k_${s}VsBytecodeSize = sizeof(k_${s}VsBytecode);")
    [void]$sb.AppendLine()
    [void]$sb.AppendLine("const unsigned char k_${s}PsBytecode[] = {")
    [void]$sb.Append((Format-CByteArray $ps))
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("const std::size_t k_${s}PsBytecodeSize = sizeof(k_${s}PsBytecode);")
    [void]$sb.AppendLine()
}

foreach ($u in $computeUnits) {
    $s = $u.Symbol
    $csPath = Join-Path $csoDir ($s + "_cs.cso")
    $cs = [System.IO.File]::ReadAllBytes($csPath)
    Assert-Dxbc $cs $csPath

    [void]$sb.AppendLine("const unsigned char k_${s}CsBytecode[] = {")
    [void]$sb.Append((Format-CByteArray $cs))
    [void]$sb.AppendLine("};")
    [void]$sb.AppendLine("const std::size_t k_${s}CsBytecodeSize = sizeof(k_${s}CsBytecode);")
    [void]$sb.AppendLine()
}

[System.IO.File]::WriteAllText($cppPath, $sb.ToString(), $utf8NoBom)

Write-Host "embed_hlsl: wrote $hPath and $cppPath"
