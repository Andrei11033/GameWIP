# GameWIP repository-owned storage and conservative recursive-deletion policy.

Set-StrictMode -Version Latest

function Resolve-GameWipStoragePath
{
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $RelativePath))
}

function Initialize-GameWipStorage
{
    foreach ($relativePath in @($ProjectConfig.storage.cache, $ProjectConfig.storage.state, $ProjectConfig.storage.temp, $ProjectConfig.storage.runs))
    {
        New-Item -ItemType Directory -Force -Path (Resolve-GameWipStoragePath -RelativePath $relativePath) | Out-Null
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
    $ownedPrefix = $canonicalRoot + [IO.Path]::DirectorySeparatorChar
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
        [string]$MarkerName = '.gamewip-owned.json'
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
    Remove-Item -LiteralPath $canonicalPath -Recurse -Force
}

function Complete-GameWipOperationTemp
{
    if ([string]::IsNullOrWhiteSpace([string]$Script:OperationContext.Temp))
    {
        return
    }
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    Invoke-GameWipOwnedTreeRemoval -Path $Script:OperationContext.Temp -OwnedRoot $tempRoot -RequireMarker
    $Script:OperationContext.Temp = $null
}
