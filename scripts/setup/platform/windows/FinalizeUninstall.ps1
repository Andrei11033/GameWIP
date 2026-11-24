[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$RepositoryRoot,
    [Parameter(Mandatory = $true)][string]$LaunchToken,
    [Parameter(Mandatory = $true)][int]$SetupExitCode
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Validate the launcher request and checkout paths.
if ($LaunchToken -notmatch '^[0-9]+-[0-9]+-[0-9]+$')
{
    throw 'The setup launcher supplied an invalid uninstall token.'
}

$RepositoryRoot = [IO.Path]::GetFullPath($RepositoryRoot)
$buildRoot = Join-Path $RepositoryRoot 'build'
$markerPath = Join-Path $buildRoot ".gamewip-uninstall-$LaunchToken.pending"

if (-not (Test-Path -LiteralPath $markerPath))
{
    exit 0
}

# Load the shared path validation and link-aware tree removal.
. (Join-Path $RepositoryRoot 'scripts/lib/Tools.ps1')
. (Join-Path $RepositoryRoot 'scripts/lib/Storage.ps1')

$buildAttributes = Get-GameWipFileSystemAttributes -Path $buildRoot
$markerAttributes = Get-GameWipFileSystemAttributes -Path $markerPath

if ($null -eq $buildAttributes -or
    ($buildAttributes -band [IO.FileAttributes]::Directory) -eq 0 -or
    ($buildAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0 -or
    $null -eq $markerAttributes -or
    ($markerAttributes -band [IO.FileAttributes]::Directory) -ne 0 -or
    ($markerAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
{
    throw 'Refusing uninstall finalization through a missing or linked build path.'
}

# Confirm that this request belongs to the current setup run.
$markerToken = Get-Content -LiteralPath $markerPath -Raw

if ($markerToken -cne $LaunchToken)
{
    Write-Warning 'The build cleanup request belongs to another setup run; build/ was preserved.'
    exit 0
}

# Preserve the build tree when setup failed.
if ($SetupExitCode -ne 0)
{
    Remove-Item -LiteralPath $markerPath -Force
    Write-Warning 'Setup did not finish cleanly; build/ was preserved for diagnosis.'
    exit 0
}

# Remove the checkout-owned build tree.
try
{
    Invoke-GameWipOwnedTreeRemoval `
        -Path $buildRoot `
        -OwnedRoot $RepositoryRoot `
        -SuppressMutationTracking
}

catch
{
    Write-Error "Could not remove checkout build/: $($_.Exception.Message)"
    exit 1
}

Write-Host "Removed checkout build directory: $buildRoot"
