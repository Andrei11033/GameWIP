# Safe removal of GameWIP-owned directory trees.

# ------------------------------------------------------------
# Ownership and recursive removal
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Assert-GameWipSafeChildPath
{
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$OwnedRoot)
    $comparison = Get-GameWipPathComparison
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

function Get-GameWipFileSystemAttributes
{
    param([Parameter(Mandatory = $true)][string]$Path)

    try
    {
        return [IO.File]::GetAttributes($Path)
    }

    catch [IO.FileNotFoundException]
    {
        return $null
    }

    catch [IO.DirectoryNotFoundException]
    {
        return $null
    }
}

function Test-GameWipPlainFile
{
    param([Parameter(Mandatory = $true)][string]$Path)

    try
    {
        $attributes = Get-GameWipFileSystemAttributes -Path $Path
        if ($null -eq $attributes)
        {
            return $false
        }

        return (
            ($attributes -band [IO.FileAttributes]::Directory) -eq 0 -and
            ($attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0
        )
    }

    catch
    {
        return $false
    }
}

function Assert-GameWipNoReparsePointInOwnedPath
{
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$OwnedRoot)
    $canonicalRoot = Get-GameWipCanonicalPath -Path $OwnedRoot
    $canonicalPath = Get-GameWipCanonicalPath -Path $Path

    # Validate the owned root before checking the target path.
    $rootAttributes = Get-GameWipFileSystemAttributes -Path $canonicalRoot

    if ($null -eq $rootAttributes -or
        ($rootAttributes -band [IO.FileAttributes]::Directory) -eq 0 -or
        ($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing recursive deletion through linked or invalid owned root '$canonicalRoot'."
    }

    # Refuse linked or missing ancestors before touching the owned tree.
    $relativePath = $canonicalPath.Substring($canonicalRoot.Length).TrimStart([char[]]@(
            [IO.Path]::DirectorySeparatorChar,
            [IO.Path]::AltDirectorySeparatorChar
        ))
    $segments = @($relativePath -split '[\\/]' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
    $currentPath = $canonicalRoot

    foreach ($segment in $segments)
    {
        $currentPath = Join-Path $currentPath $segment
        $attributes = Get-GameWipFileSystemAttributes -Path $currentPath

        if ($null -eq $attributes -or ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing recursive deletion through linked or missing path '$currentPath'."
        }
    }
}

function Remove-GameWipOwnedTreeEntries
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $pending = [System.Collections.Generic.Stack[object]]::new()
    $pending.Push([pscustomobject]@{ Path = $Path; RemoveDirectory = $false })

    # Process each entry without following links.
    while ($pending.Count -gt 0)
    {
        $entry = $pending.Pop()
        $attributes = Get-GameWipFileSystemAttributes -Path $entry.Path

        if ($null -eq $attributes)
        {
            continue
        }

        $isDirectory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
        $isLink = ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0

        # Remove links directly instead of enumerating their targets.
        if ($isLink)
        {
            if ($isDirectory)
            {
                [IO.Directory]::Delete($entry.Path)
            }
            else
            {
                [IO.File]::Delete($entry.Path)
            }
        }
        elseif ($isDirectory -and -not $entry.RemoveDirectory)
        {
            # Remove a real directory after its children.
            $pending.Push([pscustomobject]@{ Path = $entry.Path; RemoveDirectory = $true })

            foreach ($child in [IO.Directory]::EnumerateFileSystemEntries($entry.Path))
            {
                $pending.Push([pscustomobject]@{ Path = $child; RemoveDirectory = $false })
            }
        }
        elseif ($isDirectory)
        {
            [IO.Directory]::Delete($entry.Path)
        }
        else
        {
            if (($attributes -band [IO.FileAttributes]::ReadOnly) -ne 0)
            {
                $writableAttributes = $attributes -band (-bnot [IO.FileAttributes]::ReadOnly)
                [IO.File]::SetAttributes($entry.Path, $writableAttributes)
            }

            [IO.File]::Delete($entry.Path)
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

    # Validate the target and every ancestor before deletion.
    $canonicalPath = Assert-GameWipSafeChildPath -Path $Path -OwnedRoot $OwnedRoot
    Assert-GameWipNoReparsePointInOwnedPath -Path $canonicalPath -OwnedRoot $OwnedRoot

    # Confirm ownership when the caller requires a marker.
    if ($RequireMarker)
    {
        $markerPath = Join-Path $canonicalPath $MarkerName
        $markerAttributes = Get-GameWipFileSystemAttributes -Path $markerPath

        if ($null -eq $markerAttributes -or
            ($markerAttributes -band [IO.FileAttributes]::Directory) -ne 0 -or
            ($markerAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing to delete unmarked GameWIP tree '$canonicalPath'."
        }
    }

    # Track and remove the owned tree.
    if (-not $SuppressMutationTracking)
    {
        Set-GameWipMutationStarted
    }
    Remove-GameWipOwnedTreeEntries -Path $canonicalPath
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
