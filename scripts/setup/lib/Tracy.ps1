# Tracy source identity shared by host profiler builders.

# ------------------------------------------------------------
# Locked dependency source
# ------------------------------------------------------------

function Get-GameWipTracySourcePath
{
    $status = @(Get-GameWipDependencyCacheStatus | Where-Object Id -eq 'tracy')

    if ($status.Count -ne 1)
    {
        throw "The Tracy dependency is not declared in scripts/config/dependencies.json."
    }

    if (-not $status[0].Ready)
    {
        throw "The locked Tracy source is not ready at $($status[0].SourcePath). Run '$(Get-GameWipSetupHelperCommand) deps' first."
    }

    $sourcePath = [IO.Path]::GetFullPath([string]$status[0].SourcePath)

    if (-not (Test-Path -LiteralPath (Join-Path $sourcePath 'CMakeLists.txt') -PathType Leaf))
    {
        throw "The cached Tracy source is incomplete: $sourcePath"
    }

    return $sourcePath
}

function Get-GameWipTracyVersion
{
    $header = Join-Path (Get-GameWipTracySourcePath) 'public/common/TracyVersion.hpp'

    if (-not (Test-Path -LiteralPath $header -PathType Leaf))
    {
        throw "The cached Tracy source is missing its version header: $header"
    }

    $text = Get-Content -LiteralPath $header -Raw
    $parts = foreach ($name in @('Major', 'Minor', 'Patch'))
    {
        $match = [regex]::Match($text, "(?m)^\s*constexpr\s+int\s+$name\s*=\s*(\d+)\s*;")

        if (-not $match.Success)
        {
            throw "Could not read Tracy $name version from $header."
        }

        $match.Groups[1].Value
    }

    return $parts -join '.'
}
