# Repository-owned storage and conservative recursive-deletion policy.

# ------------------------------------------------------------
# Path resolution and storage roots
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipFileSha256
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)

    if (-not (Test-Path -LiteralPath $fullPath -PathType Leaf))
    {
        throw "Cannot hash missing file '$fullPath'."
    }

    $stream = [IO.File]::OpenRead($fullPath)

    try
    {
        $sha256 = [System.Security.Cryptography.SHA256]::Create()

        try
        {
            return ([BitConverter]::ToString($sha256.ComputeHash($stream))).Replace('-', '').ToLowerInvariant()
        }

        finally
        {
            $sha256.Dispose()
        }
    }

    finally
    {
        $stream.Dispose()
    }
}

function Update-GameWipProcessPath
{
    if (-not [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
    {
        return
    }

    $entries = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($pathValue in @(
            $env:Path,
            [Environment]::GetEnvironmentVariable('Path', 'Machine'),
            [Environment]::GetEnvironmentVariable('Path', 'User')
        ))
    {
        foreach ($entry in @([string]$pathValue -split [IO.Path]::PathSeparator))
        {
            $trimmed = $entry.Trim().Trim('"')

            if (-not [string]::IsNullOrWhiteSpace($trimmed) -and $seen.Add($trimmed))
            {
                $entries.Add($trimmed) | Out-Null
            }
        }
    }

    $env:Path = $entries -join [IO.Path]::PathSeparator
}

function Test-GameWipPathWithinRoot
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Root
    )

    $canonicalPath = Get-GameWipCanonicalPath -Path $Path
    $canonicalRoot = Get-GameWipCanonicalPath -Path $Root
    $comparison = if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }

    if ($canonicalPath.Equals($canonicalRoot, $comparison))
    {
        return $true
    }

    return $canonicalPath.StartsWith($canonicalRoot + [IO.Path]::DirectorySeparatorChar, $comparison)
}

function Resolve-GameWipStoragePath
{
    param([Parameter(Mandatory = $true)][string]$RelativePath)

    # Reject absolute paths and any path that escapes the storage root.
    if ([IO.Path]::IsPathRooted($RelativePath))
    {
        throw "GameWIP storage paths must be relative to the configured storage root: '$RelativePath'."
    }

    $storageRoot = Get-GameWipStorageRoot
    $nativeRelativePath = $RelativePath.Replace('/', [IO.Path]::DirectorySeparatorChar).Replace('\', [IO.Path]::DirectorySeparatorChar)
    $resolvedPath = [IO.Path]::GetFullPath((Join-Path $storageRoot $nativeRelativePath))

    if (-not (Test-GameWipPathWithinRoot -Path $resolvedPath -Root $storageRoot))
    {
        throw "GameWIP storage path '$RelativePath' escapes the configured storage root '$storageRoot'."
    }

    return $resolvedPath
}

function Get-GameWipStorageRoot
{
    # Resolve the configured path relative to the checkout when needed.
    $configuredRoot = if (-not [string]::IsNullOrWhiteSpace([string]$env:GAMEWIP_STORAGE_ROOT))
    {
        [string]$env:GAMEWIP_STORAGE_ROOT
    }
    else
    {
        [string]$ProjectConfig.storage.root
    }

    $storageRoot = if ([IO.Path]::IsPathRooted($configuredRoot))
    {
        [IO.Path]::GetFullPath($configuredRoot)
    }
    else
    {
        [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $configuredRoot))
    }
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))
    $pathComparison = if (Test-GameWipWindowsHost)
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }

    # Keep shared helper storage beneath build/.
    if ($storageRoot.Equals($buildRoot, $pathComparison) -or
        -not (Test-GameWipPathWithinRoot -Path $storageRoot -Root $buildRoot))
    {
        throw "GameWIP storage root must be a child of '$buildRoot'; found '$storageRoot'."
    }

    return $storageRoot
}

function Get-GameWipStorageAreaPath
{
    param([Parameter(Mandatory = $true)][ValidateSet('cache', 'state', 'temp', 'runs')][string]$Area)
    return Resolve-GameWipStoragePath -RelativePath ([string]$ProjectConfig.storage[$Area])
}

function Resolve-GameWipManagedRoot
{
    param(
        [Parameter(Mandatory = $true)][string]$RelativePath,
        [Parameter(Mandatory = $true)][string]$EnvironmentVariable
    )

    # Resolve the default beneath shared storage or use the explicit override.
    $configuredPath = [Environment]::GetEnvironmentVariable($EnvironmentVariable, 'Process')
    $resolvedPath = if ([string]::IsNullOrWhiteSpace($configuredPath))
    {
        Resolve-GameWipStoragePath -RelativePath $RelativePath
    }
    elseif ([IO.Path]::IsPathRooted($configuredPath))
    {
        [IO.Path]::GetFullPath($configuredPath)
    }
    else
    {
        Resolve-GameWipStoragePath -RelativePath $configuredPath
    }
    $filesystemRoot = [IO.Path]::GetPathRoot($resolvedPath)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))
    $pathComparison = if (Test-GameWipWindowsHost)
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }
    $repositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)

    # Reject unsafe roots and require external destinations to exist.
    if ($resolvedPath.Equals($filesystemRoot, $pathComparison) -or $resolvedPath.Equals($repositoryRoot, $pathComparison))
    {
        throw "$EnvironmentVariable cannot point to a filesystem root or the repository root."
    }

    $pathIsUnderBuild = Test-GameWipPathWithinRoot -Path $resolvedPath -Root $buildRoot

    if (-not $pathIsUnderBuild -and -not (Test-Path -LiteralPath $resolvedPath -PathType Container))
    {
        throw "$EnvironmentVariable may point outside build/ only to an existing installation; new GameWIP-managed roots must stay under '$buildRoot'."
    }

    if (-not $pathIsUnderBuild -and (Test-GameWipWindowsHost) -and $EnvironmentVariable -eq 'GAMEWIP_MSYS2_ROOT' -and
        (-not (Test-Path -LiteralPath (Join-Path $resolvedPath 'usr/bin/bash.exe') -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $resolvedPath 'usr/bin/pacman.exe') -PathType Leaf)))
    {
        throw "$EnvironmentVariable must point to an existing MSYS2 installation; new installations must stay under '$buildRoot'."
    }

    if (-not $pathIsUnderBuild -and $EnvironmentVariable -eq 'GAMEWIP_MANAGED_TOOLS_ROOT' -and
        @(Get-ChildItem -LiteralPath $resolvedPath -Force -ErrorAction SilentlyContinue).Count -eq 0)
    {
        throw "$EnvironmentVariable must point to an existing tools directory; new GameWIP-managed roots must stay under '$buildRoot'."
    }

    return $resolvedPath
}

function Get-GameWipMsys2Root
{
    return Resolve-GameWipManagedRoot `
        -RelativePath ([string]$ProjectConfig.managedEnvironment.msys2Root) `
        -EnvironmentVariable GAMEWIP_MSYS2_ROOT
}

function Get-GameWipManagedToolRoot
{
    return Resolve-GameWipManagedRoot `
        -RelativePath ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) `
        -EnvironmentVariable GAMEWIP_MANAGED_TOOLS_ROOT
}

function Get-GameWipTemporaryRoot
{
    if ($null -ne $Script:OperationContext -and -not [string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        return [string]$Script:OperationContext.Temp
    }

    $temporaryRoot = Get-GameWipStorageAreaPath -Area temp
    New-Item -ItemType Directory -Force -Path $temporaryRoot | Out-Null
    return $temporaryRoot
}

function Initialize-GameWipStorage
{
    New-Item -ItemType Directory -Force -Path (Get-GameWipStorageRoot) | Out-Null

    foreach ($area in @('cache', 'state', 'temp', 'runs'))
    {
        New-Item -ItemType Directory -Force -Path (Get-GameWipStorageAreaPath -Area $area) | Out-Null
    }
}

function Get-GameWipCanonicalPath
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $canonical = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($canonical)

    if ($canonical -eq $root)
    {
        return $canonical
    }

    [char[]]$trimCharacters = @([IO.Path]::DirectorySeparatorChar, [IO.Path]::AltDirectorySeparatorChar) | Select-Object -Unique
    return $canonical.TrimEnd($trimCharacters)
}

function Assert-GameWipSafeChildPath
{
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$OwnedRoot)
    $comparison = if (Test-GameWipWindowsHost)
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }
    $canonicalPath = Get-GameWipCanonicalPath -Path $Path
    $canonicalRoot = Get-GameWipCanonicalPath -Path $OwnedRoot
    $driveRoot = Get-GameWipCanonicalPath -Path ([IO.Path]::GetPathRoot($canonicalPath))
    $repositoryRoot = Get-GameWipCanonicalPath -Path $RepositoryRoot
    $profileRoot = if (-not [string]::IsNullOrWhiteSpace([string]$env:USERPROFILE))
    {
        Get-GameWipCanonicalPath -Path $env:USERPROFILE
    }
    else
    {
        $null
    }
    $ownedPrefix = if ($canonicalRoot.Equals($driveRoot, $comparison))
    {
        $canonicalRoot
    }
    else
    {
        $canonicalRoot + [IO.Path]::DirectorySeparatorChar
    }

    if ($canonicalPath.Equals($canonicalRoot, $comparison) -or
        $canonicalPath.Equals($driveRoot, $comparison) -or
        $canonicalPath.Equals($repositoryRoot, $comparison) -or
        ($profileRoot -and $canonicalPath.Equals($profileRoot, $comparison)) -or
        -not $canonicalPath.StartsWith($ownedPrefix, $comparison))
    {
        throw "Refusing unsafe recursive deletion target '$canonicalPath' outside owned root '$canonicalRoot'."
    }

    return $canonicalPath
}

function Assert-GameWipNoReparsePointInOwnedTree
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $rootItem = Get-Item -LiteralPath $Path -Force -ErrorAction Stop

    if (($rootItem.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing recursive deletion of reparse point '$Path'."
    }

    foreach ($item in @(Get-ChildItem -LiteralPath $Path -Force -Recurse -ErrorAction Stop))
    {
        if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing recursive deletion because '$($item.FullName)' is a symlink/junction/reparse point."
        }
    }
}

function Invoke-GameWipOwnedTreeRemoval
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$OwnedRoot,
        [switch]$RequireMarker,
        [string]$MarkerName = '.gamewip-owned.json',
        [switch]$SuppressMutationTracking
    )

    if (-not (Test-Path -LiteralPath $Path))
    {
        return
    }

    $canonicalPath = Assert-GameWipSafeChildPath -Path $Path -OwnedRoot $OwnedRoot

    if ($RequireMarker -and -not (Test-Path -LiteralPath (Join-Path $canonicalPath $MarkerName) -PathType Leaf))
    {
        throw "Refusing to delete unmarked GameWIP tree '$canonicalPath'."
    }

    Assert-GameWipNoReparsePointInOwnedTree -Path $canonicalPath

    if (-not $SuppressMutationTracking)
    {
        Set-GameWipMutationStarted
    }
    Remove-Item -LiteralPath $canonicalPath -Recurse -Force
}

function Complete-GameWipOperationTemp
{
    if ([string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        return
    }

    $tempRoot = Get-GameWipStorageAreaPath -Area temp
    Invoke-GameWipOwnedTreeRemoval -Path $Script:OperationContext.Temp -OwnedRoot $tempRoot -RequireMarker -SuppressMutationTracking
    $Script:OperationContext.Temp = $null
}
