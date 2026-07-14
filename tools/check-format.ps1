param(
    [string[]]$Paths
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$sourceExtensions = [System.Collections.Generic.HashSet[string]]::new(
    [System.StringComparer]::OrdinalIgnoreCase
)
@(
    ".as", ".bat", ".c", ".cc", ".cpp", ".h", ".hpp", ".hlsl", ".hlsli",
    ".lua", ".m", ".mm", ".ps1", ".rcss", ".rml", ".sh", ".shader",
    ".yaml", ".yml"
) | ForEach-Object { [void]$sourceExtensions.Add($_) }

function Get-DefaultFiles {
    $git = Get-Command git -ErrorAction SilentlyContinue
    if (-not $git) {
        throw "git was not found on PATH."
    }

    $listed = & $git.Source -C $repoRoot ls-files --cached --others --exclude-standard -- `
        src tests tools EngineContent Content .github xmake `
        xmake.lua .clang-format .clang-format-version .editorconfig .gitattributes `
        build_vulkan.bat gensln.bat
    if ($LASTEXITCODE -ne 0) {
        throw "git ls-files failed while discovering format-check inputs."
    }

    foreach ($relativePath in $listed) {
        if ([string]::IsNullOrWhiteSpace($relativePath)) {
            continue
        }

        $normalized = $relativePath.Replace("\", "/")
        if ($normalized -match "(^|/)(thirdparty|packages|build|Builds|vs2022|Saved|Library|logs|\.git|\.vs|\.xmake)(/|$)") {
            continue
        }

        $extension = [System.IO.Path]::GetExtension($relativePath)
        $rootFiles = @(
            "xmake.lua", ".clang-format", ".clang-format-version", ".editorconfig", ".gitattributes"
        )
        if ($rootFiles -notcontains $relativePath -and -not $sourceExtensions.Contains($extension)) {
            continue
        }

        $fullPath = Join-Path $repoRoot $relativePath
        if (Test-Path -LiteralPath $fullPath -PathType Leaf) {
            $fullPath
        }
    }
}

function Get-RequestedFiles {
    param([string[]]$RequestedPaths)

    foreach ($requestedPath in $RequestedPaths) {
        $resolved = Resolve-Path -LiteralPath $requestedPath -ErrorAction Stop
        foreach ($item in $resolved) {
            if (Test-Path -LiteralPath $item.Path -PathType Container) {
                Get-ChildItem -LiteralPath $item.Path -File -Recurse | ForEach-Object { $_.FullName }
            } else {
                $item.Path
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

function Add-Failure {
    param(
        [string]$DisplayPath,
        [int]$LineNumber,
        [string]$Message
    )

    $script:failureCount++
    Write-Host ("{0}:{1}: {2}" -f $DisplayPath, $LineNumber, $Message) -ForegroundColor Red
}

$files = if ($Paths -and $Paths.Count -gt 0) {
    @(Get-RequestedFiles -RequestedPaths $Paths)
} else {
    @(Get-DefaultFiles)
}
$files = @($files | Sort-Object -Unique)

$failureCount = 0
foreach ($file in $files) {
    $fullPath = (Resolve-Path -LiteralPath $file).Path
    $displayPath = Get-DisplayPath -FullPath $fullPath
    $bytes = [System.IO.File]::ReadAllBytes($fullPath)

    $lineNumber = 1
    $reportedCarriageReturn = $false
    foreach ($byte in $bytes) {
        if ($byte -eq 13 -and -not $reportedCarriageReturn) {
            Add-Failure -DisplayPath $displayPath -LineNumber $lineNumber `
                -Message "carriage return found; repository text must use LF line endings"
            $reportedCarriageReturn = $true
        }
        if ($byte -eq 10) {
            $lineNumber++
        }
    }

    if ($bytes.Length -gt 0 -and $bytes[$bytes.Length - 1] -ne 10) {
        Add-Failure -DisplayPath $displayPath -LineNumber $lineNumber -Message "missing final newline"
    }

    $text = [System.Text.Encoding]::UTF8.GetString($bytes)
    $lines = [System.Text.RegularExpressions.Regex]::Split($text, "\n")
    $blankRun = 0
    for ($index = 0; $index -lt $lines.Count; $index++) {
        $line = $lines[$index].TrimEnd("`r")
        $currentLine = $index + 1

        if ($line -match "[ `t]+$") {
            Add-Failure -DisplayPath $displayPath -LineNumber $currentLine -Message "trailing whitespace"
        }
        if ($line -match "^`t+") {
            Add-Failure -DisplayPath $displayPath -LineNumber $currentLine -Message "leading tab indentation"
        }

        if ([string]::IsNullOrWhiteSpace($line)) {
            $blankRun++
            if ($blankRun -eq 2) {
                Add-Failure -DisplayPath $displayPath -LineNumber $currentLine `
                    -Message "consecutive blank lines"
            }
        } else {
            $blankRun = 0
        }
    }
}

if ($failureCount -gt 0) {
    Write-Host ""
    Write-Host "Format hygiene check failed with $failureCount issue(s)." -ForegroundColor Red
    exit 1
}

Write-Host "Format hygiene check passed for $($files.Count) file(s)."
