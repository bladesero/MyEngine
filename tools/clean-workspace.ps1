param(
    [switch]$Apply,
    [switch]$IncludeBuildArtifacts,
    [switch]$IncludeGeneratedReports
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$repoPrefix = $repoRoot.TrimEnd("\", "/") + [IO.Path]::DirectorySeparatorChar
$targets = [System.Collections.Generic.Dictionary[string, string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
)

function Add-CleanupTarget {
    param(
        [string]$Path,
        [string]$Reason
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    if (-not $fullPath.StartsWith($repoPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "Cleanup target escapes the repository: $fullPath"
    }
    if (Test-Path -LiteralPath $fullPath) {
        $item = Get-Item -LiteralPath $fullPath -Force
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "Refusing to clean a symbolic link or junction: $fullPath"
        }
        $targets[$fullPath] = $Reason
    }
}

function Add-IgnoredFiles {
    param(
        [string]$PathSpec,
        [string]$Reason,
        [scriptblock]$Filter = { $true }
    )

    $git = Get-Command git -ErrorAction Stop
    $ignored = @(& $git.Source -C $repoRoot ls-files --others --ignored --exclude-standard -- $PathSpec)
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed while discovering $Reason."
    }
    foreach ($relativePath in $ignored) {
        if (-not [string]::IsNullOrWhiteSpace($relativePath) -and (& $Filter $relativePath)) {
            Add-CleanupTarget -Path (Join-Path $repoRoot $relativePath) -Reason $Reason
        }
    }
}

# These directories contain per-user runtime/editor state and diagnostics only.
Add-CleanupTarget -Path (Join-Path $repoRoot ".myengine") -Reason "editor-generated state"
Add-CleanupTarget -Path (Join-Path $repoRoot "logs") -Reason "runtime logs"

# Discover only ignored scratch files so source assets and tracked fixtures are never selected.
Add-IgnoredFiles -PathSpec "." -Reason "transactional backup or temporary file" -Filter {
    param($relativePath)
    $name = [IO.Path]::GetFileName($relativePath)
    $name.EndsWith(".bak", [StringComparison]::OrdinalIgnoreCase) -or
        $name.EndsWith(".tmp", [StringComparison]::OrdinalIgnoreCase) -or
        $name.IndexOf(".tmp.", [StringComparison]::OrdinalIgnoreCase) -ge 0
}

if ($IncludeGeneratedReports) {
    Add-CleanupTarget -Path (Join-Path $repoRoot "Saved\VisualChecks") -Reason "generated visual checks"
    Add-IgnoredFiles -PathSpec "Saved/PerformanceReports" -Reason "generated performance report"
}

if ($IncludeBuildArtifacts) {
    @(".xmake", "build", "out", "Builds", "vs2022", "Library") | ForEach-Object {
        Add-CleanupTarget -Path (Join-Path $repoRoot $_) -Reason "build artifact"
    }
    Get-ChildItem -LiteralPath $repoRoot -Directory -Filter "build_*" -ErrorAction SilentlyContinue |
        ForEach-Object { Add-CleanupTarget -Path $_.FullName -Reason "build artifact" }
}

$orderedTargets = @($targets.GetEnumerator() | Sort-Object Key)
if ($orderedTargets.Count -eq 0) {
    Write-Output "Workspace cleanup found no matching files."
    exit 0
}

foreach ($target in $orderedTargets) {
    $relativePath = $target.Key.Substring($repoPrefix.Length).Replace("\", "/")
    $action = if ($Apply) { "REMOVE" } else { "WOULD REMOVE" }
    Write-Output ("[{0}] {1} ({2})" -f $action, $relativePath, $target.Value)
}

if (-not $Apply) {
    Write-Output ""
    Write-Output "Preview only. Re-run with -Apply to remove these targets."
    exit 0
}

# Delete deepest paths first so files discovered inside a selected directory are harmless.
$orderedTargets |
    Sort-Object { $_.Key.Length } -Descending |
    ForEach-Object {
        if (Test-Path -LiteralPath $_.Key) {
            Remove-Item -LiteralPath $_.Key -Recurse -Force
        }
    }

Write-Output "Workspace cleanup removed $($orderedTargets.Count) target(s)."
