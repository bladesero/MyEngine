[CmdletBinding()]
param(
    [string[]]$Paths,
    [switch]$Fix,
    [string]$Executable
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$versionFile = Join-Path $repoRoot ".clang-format-version"
$styleFile = Join-Path $repoRoot ".clang-format"
$styleArgument = "--style=file:$styleFile"
$requiredVersion = (Get-Content -LiteralPath $versionFile -Raw).Trim()
$sourceExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
)
@(".c", ".cc", ".cpp", ".h", ".hpp", ".m", ".mm") |
    ForEach-Object { [void]$sourceExtensions.Add($_) }

function Resolve-ClangFormat {
    if ($Executable) {
        return (Resolve-Path -LiteralPath $Executable -ErrorAction Stop).Path
    }

    $command = Get-Command clang-format -ErrorAction SilentlyContinue
    if ($command) {
        return $command.Source
    }

    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (Test-Path -LiteralPath $vswhere -PathType Leaf) {
        $installationPath = & $vswhere -latest -products * `
            -requires Microsoft.VisualStudio.Component.VC.Llvm.Clang `
            -property installationPath
        if ($LASTEXITCODE -eq 0 -and $installationPath) {
            $candidate = Join-Path $installationPath "VC\Tools\Llvm\x64\bin\clang-format.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    $visualStudioRoot = Join-Path $env:ProgramFiles "Microsoft Visual Studio\2022"
    if (Test-Path -LiteralPath $visualStudioRoot -PathType Container) {
        foreach ($edition in Get-ChildItem -LiteralPath $visualStudioRoot -Directory) {
            $candidate = Join-Path $edition.FullName "VC\Tools\Llvm\x64\bin\clang-format.exe"
            if (Test-Path -LiteralPath $candidate -PathType Leaf) {
                return $candidate
            }
        }
    }

    throw "clang-format was not found. Install clang-format $requiredVersion or pass -Executable."
}

function Test-SourceFile {
    param([string]$Path)

    return $sourceExtensions.Contains([System.IO.Path]::GetExtension($Path))
}

function Test-ExcludedPath {
    param([string]$Path)

    $normalized = $Path.Replace("\", "/")
    return $normalized -match `
        "(^|/)(thirdparty|packages|build|Builds|vs2022|Saved|Library|logs|\.git|\.vs|\.xmake)(/|$)"
}

function Get-DefaultFiles {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) {
        throw "git was not found on PATH."
    }

    $listed = & $git.Source -C $repoRoot ls-files --cached --others --exclude-standard -- src tests
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed while discovering clang-format inputs."
    }

    foreach ($relativePath in $listed) {
        if ([string]::IsNullOrWhiteSpace($relativePath) -or
            (Test-ExcludedPath -Path $relativePath) -or
            -not (Test-SourceFile -Path $relativePath)) {
            continue
        }

        $fullPath = Join-Path $repoRoot $relativePath
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            (Resolve-Path -LiteralPath $fullPath).Path
        }
    }
}

function Get-RequestedFiles {
    param([string[]]$RequestedPaths)

    foreach ($requestedPath in $RequestedPaths) {
        $resolved = Resolve-Path -LiteralPath $requestedPath -ErrorAction Stop
        foreach ($item in $resolved) {
            if (Test-Path -LiteralPath $item.Path -PathType Container) {
                Get-ChildItem -LiteralPath $item.Path -File -Recurse |
                    Where-Object {
                        -not (Test-ExcludedPath -Path $_.FullName) -and
                        (Test-SourceFile -Path $_.FullName)
                    } |
                    ForEach-Object { $_.FullName }
            } elseif (Test-SourceFile -Path $item.Path) {
                $item.Path
            } else {
                throw "Unsupported clang-format input: $($item.Path)"
            }
        }
    }
}

function Get-DisplayPath {
    param([string]$FullPath)

    $rootWithSeparator = $repoRoot.TrimEnd("\", "/") + [System.IO.Path]::DirectorySeparatorChar
    if ($FullPath.StartsWith($rootWithSeparator, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $FullPath.Substring($rootWithSeparator.Length).Replace("\", "/")
    }
    return $FullPath
}

$clangFormat = Resolve-ClangFormat
$versionOutput = (& $clangFormat --version 2>&1) -join "`n"
if ($LASTEXITCODE -ne 0 -or $versionOutput -notmatch "clang-format version\s+([0-9]+(?:\.[0-9]+)+)") {
    throw "Failed to determine clang-format version from '$clangFormat'."
}
$actualVersion = $Matches[1]
if ($actualVersion -ne $requiredVersion) {
    throw "clang-format version $actualVersion does not match required version $requiredVersion."
}

$files = if ($Paths -and $Paths.Count -gt 0) {
    @(Get-RequestedFiles -RequestedPaths $Paths)
} else {
    @(Get-DefaultFiles)
}
$files = @($files | Sort-Object -Unique)
if ($files.Count -eq 0) {
    throw "No clang-format input files were found."
}

$failureCount = 0
foreach ($file in $files) {
    $displayPath = Get-DisplayPath -FullPath $file
    $previousErrorActionPreference = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try {
        if ($Fix) {
            $output = & $clangFormat -i $styleArgument --fallback-style=none -- $file 2>&1
        } else {
            $output = & $clangFormat --dry-run --Werror $styleArgument --fallback-style=none -- $file 2>&1
        }
        $exitCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0) {
        $failureCount++
        if ($output) {
            $output | ForEach-Object { Write-Host $_ -ForegroundColor Red }
        } else {
            Write-Host "$displayPath`: clang-format failed" -ForegroundColor Red
        }
    }
}

if ($failureCount -gt 0) {
    $operation = if ($Fix) { "format" } else { "check" }
    Write-Host ""
    Write-Host "clang-format $operation failed for $failureCount file(s)." -ForegroundColor Red
    exit 1
}

$operation = if ($Fix) { "formatted" } else { "check passed for" }
Write-Host "clang-format $operation $($files.Count) file(s) with version $requiredVersion."
