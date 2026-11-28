# Linux Tracy tool discovery and verification.

# ------------------------------------------------------------
# Managed tool location
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

function Test-GameWipLinuxTracyOwnershipMarker
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

        # The version marker identifies the managed directory for uninstall.
        return (Get-Content -LiteralPath $versionFile -Raw).Trim() -match '^source-\d+\.\d+\.\d+$'
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxTracyToolSet
{
    param([string]$ToolRoot = (Get-GameWipLinuxTracyToolRoot))

    if (-not (Test-GameWipLinuxTracyOwnershipMarker -ToolRoot $ToolRoot))
    {
        return $false
    }

    # Require every installed tool before reporting the set as ready.
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
