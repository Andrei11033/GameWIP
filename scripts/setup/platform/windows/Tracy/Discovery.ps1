# ------------------------------------------------------------
# Tracy installed tool discovery and verification
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
        throw "The locked Tracy source is not ready at $($status[0].SourcePath). Run 'setup.bat deps' first."
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
    $tracyRoot = Get-GameWipTracySourcePath

    $header = Join-Path $tracyRoot 'public\common\TracyVersion.hpp'

    if (-not (Test-Path -LiteralPath $header))
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

function Get-GameWipTracyExecutableSet
{
    return @(
        'tracy-profiler.exe'
        'tracy-capture.exe'
        'tracy-csvexport.exe'
        'tracy-import-chrome.exe'
        'tracy-import-fuchsia.exe'
        'tracy-update.exe'
    )
}

function Get-GameWipTracyToolRoot
{
    return (Join-Path (Get-GameWipManagedToolRoot) 'tools\tracy')
}

function Test-GameWipTracyToolSet
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [string]$ToolRoot = (Get-GameWipTracyToolRoot)
    )

    foreach ($executable in Get-GameWipTracyExecutableSet)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $ToolRoot $executable)))
        {
            return $false
        }
    }

    $versionFile = Join-Path $ToolRoot 'version.txt'

    if (-not (Test-Path -LiteralPath $versionFile))
    {
        # Executable presence alone cannot prove that the tools match the
        # locked client. Rebuild once to establish a trustworthy marker.
        return $false
    }

    try
    {
        $expected = "source-$(Get-GameWipTracyVersion)"
    }

    catch
    {
        return $false
    }

    return (Get-Content -LiteralPath $versionFile -Raw).Trim() -eq $expected
}
