param(
    [ValidateSet("debug", "release")]
    [string]$Mode = "debug",
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
& $xmake.Source f -m $Mode $vulkanOption
if ($LASTEXITCODE -ne 0) { throw "xmake configure failed." }

& $xmake.Source build MyEnginePlayer
if ($LASTEXITCODE -ne 0) { throw "initial Player build failed." }

$outputRoot = Join-Path $repoRoot "build\windows\x64\$Mode"
$tracked = @(
    (Join-Path $outputRoot "MyEnginePlayer.exe"),
    (Join-Path $outputRoot "runtime.dll"),
    (Join-Path $outputRoot ".myengine\content.manifest"),
    (Join-Path $repoRoot "build\generated\icons\editor.ico"),
    (Join-Path $repoRoot "build\generated\icons\player.ico"),
    (Join-Path $repoRoot "build\generated\icons\cooker.ico"),
    (Join-Path $repoRoot "build\generated\icons\icons.stamp")
)
$tracked += Get-ChildItem (Join-Path $repoRoot "build\.myengine\architecture") -Filter *.stamp |
    ForEach-Object { $_.FullName }
foreach ($directory in @("Content", "EngineContent", "ProjectTemplates")) {
    $destination = Join-Path $outputRoot $directory
    if (Test-Path -LiteralPath $destination -PathType Container) {
        $tracked += Get-ChildItem -LiteralPath $destination -Recurse -File |
            ForEach-Object { $_.FullName }
    }
}

$before = @{}
foreach ($file in $tracked | Sort-Object -Unique) {
    if (-not (Test-Path -LiteralPath $file -PathType Leaf)) {
        throw "incremental build tracked output is missing: $file"
    }
    $item = Get-Item -LiteralPath $file
    $before[$file] = "$($item.Length):$($item.LastWriteTimeUtc.Ticks)"
}

$secondOutput = @(& $xmake.Source build MyEnginePlayer 2>&1)
$secondOutput | ForEach-Object { Write-Host $_ }
if ($LASTEXITCODE -ne 0) { throw "second Player build failed." }
if (($secondOutput -join "`n") -match "(?i)\b(compiling|linking|archiving)\.") {
    throw "second Player build was not a no-op."
}

foreach ($entry in $before.GetEnumerator()) {
    $item = Get-Item -LiteralPath $entry.Key
    $after = "$($item.Length):$($item.LastWriteTimeUtc.Ticks)"
    if ($after -ne $entry.Value) {
        throw "incremental build rewrote an unchanged output: $($entry.Key)"
    }
}

Write-Host "Incremental Player build is a no-op across binaries, icons, content, and architecture stamps."
