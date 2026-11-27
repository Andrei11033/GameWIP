# Linux Tracy profiler discovery and verification.

# ------------------------------------------------------------
# Managed profiler location
# ------------------------------------------------------------

function Get-GameWipLinuxTracyToolRoot
{
    return (Join-Path (Get-GameWipManagedToolRoot) 'tools/tracy/linux')
}

function Test-GameWipLinuxTracyExecutable
{
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return $false
    }

    try
    {
        $attributes = [IO.File]::GetAttributes($Path)

        if (($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($attributes -band [IO.FileAttributes]::Directory) -ne 0)
        {
            return $false
        }

        $executeModes = [IO.UnixFileMode]::UserExecute -bor
        [IO.UnixFileMode]::GroupExecute -bor
        [IO.UnixFileMode]::OtherExecute

        return ([IO.File]::GetUnixFileMode($Path) -band $executeModes) -ne 0
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxTracyInstallation
{
    param([string]$ToolRoot = (Get-GameWipLinuxTracyToolRoot))

    $versionFile = Join-Path $ToolRoot 'version.txt'

    if (-not (Test-Path -LiteralPath $ToolRoot -PathType Container) -or
        -not (Test-Path -LiteralPath $versionFile -PathType Leaf))
    {
        return $false
    }

    try
    {
        $rootAttributes = [IO.File]::GetAttributes($ToolRoot)
        $versionAttributes = [IO.File]::GetAttributes($versionFile)

        if (($rootAttributes -band [IO.FileAttributes]::Directory) -eq 0 -or
            ($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($versionAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
            ($versionAttributes -band [IO.FileAttributes]::Directory) -ne 0)
        {
            return $false
        }

        $executables = @(Get-GameWipLinuxTracyExecutableSet)
        $currentInstallation = $true

        foreach ($executable in $executables)
        {
            if (-not (Test-GameWipLinuxTracyExecutable -Path (Join-Path $ToolRoot $executable)))
            {
                $currentInstallation = $false
                break
            }
        }

        # Recognize the earlier single-profiler layout for uninstall cleanup.
        $previousInstallation = Test-GameWipLinuxTracyExecutable -Path (Join-Path $ToolRoot 'tracy-profiler')

        if (-not $currentInstallation -and -not $previousInstallation)
        {
            return $false
        }

        return (Get-Content -LiteralPath $versionFile -Raw).Trim() -match '^source-\d+\.\d+\.\d+$'
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxTracyProfiler
{
    param([string]$ToolRoot = (Get-GameWipLinuxTracyToolRoot))

    if (-not (Test-GameWipLinuxTracyInstallation -ToolRoot $ToolRoot))
    {
        return $false
    }

    foreach ($executable in Get-GameWipLinuxTracyExecutableSet)
    {
        if (-not (Test-GameWipLinuxTracyExecutable -Path (Join-Path $ToolRoot $executable)))
        {
            return $false
        }
    }

    try
    {
        $expected = "source-$(Get-GameWipTracyVersion)"
        return (Get-Content -LiteralPath (Join-Path $ToolRoot 'version.txt') -Raw).Trim() -eq $expected
    }

    catch
    {
        return $false
    }
}
