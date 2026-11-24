# ------------------------------------------------------------
# Tracy installed tool discovery and verification
# ------------------------------------------------------------

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
    return (Join-Path (Get-GameWipManagedToolRoot) 'tools/tracy/windows')
}

function Test-GameWipTracyToolSet
{
    param([string]$ToolRoot = (Get-GameWipTracyToolRoot))

    foreach ($executable in Get-GameWipTracyExecutableSet)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $ToolRoot $executable) -PathType Leaf))
        {
            return $false
        }
    }

    $versionFile = Join-Path $ToolRoot 'version.txt'

    if (-not (Test-Path -LiteralPath $versionFile -PathType Leaf))
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
