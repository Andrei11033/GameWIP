# Linux managed tool marker validation and link-safe removal.

# ------------------------------------------------------------
# Managed tool marker validation
# ------------------------------------------------------------

function Get-GameWipLinuxPathAttributes
{
    param([Parameter(Mandatory = $true)][string]$Path)

    try
    {
        return [IO.File]::GetAttributes($Path)
    }

    catch
    {
        return $null
    }
}

function Test-GameWipLinuxOwnedDirectory
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $attributes = Get-GameWipLinuxPathAttributes -Path $Path

    if ($null -eq $attributes)
    {
        return $false
    }

    $isDirectory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
    $isPlainDirectory = ($attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0
    return $isDirectory -and $isPlainDirectory
}

# Remove links as links so managed environments such as Python venvs can be removed safely.
function Invoke-GameWipLinuxOwnedTreeRemoval
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$OwnedRoot
    )

    if ($null -eq (Get-GameWipLinuxPathAttributes -Path $Path))
    {
        return
    }

    $canonicalPath = Assert-GameWipSafeChildPath -Path $Path -OwnedRoot $OwnedRoot
    $rootAttributes = Get-GameWipLinuxPathAttributes -Path $canonicalPath

    if ($null -eq $rootAttributes -or
        ($rootAttributes -band [IO.FileAttributes]::Directory) -eq 0 -or
        ($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing to remove unmanaged Linux tool path '$canonicalPath'."
    }

    # Refuse linked ancestors so traversal cannot leave the managed root.
    $canonicalRoot = Get-GameWipCanonicalPath -Path $OwnedRoot
    $relativePath = [IO.Path]::GetRelativePath($canonicalRoot, $canonicalPath)
    $segments = @($relativePath -split [regex]::Escape([IO.Path]::DirectorySeparatorChar) | Where-Object { $_ })
    $currentPath = $canonicalRoot

    for ($index = 0; $index -lt $segments.Count - 1; ++$index)
    {
        $currentPath = Join-Path $currentPath $segments[$index]
        $attributes = Get-GameWipLinuxPathAttributes -Path $currentPath

        if ($null -ne $attributes -and ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing to remove Linux tool data through linked directory '$currentPath'."
        }
    }

    Set-GameWipMutationStarted

    Remove-GameWipOwnedTreeEntries -Path $canonicalPath
}

function Test-GameWipLinuxJsonToolMarker
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$MarkerName,
        [Parameter(Mandatory = $true)][string]$ToolId,
        [string]$Version,
        [hashtable]$Tool
    )

    $markerPath = Join-Path $Path $MarkerName

    if (-not (Test-GameWipPlainFile -Path $markerPath))
    {
        return $false
    }

    try
    {
        $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json

        if ($marker.schemaVersion -ne 1 -or $marker.toolId -ne $ToolId)
        {
            return $false
        }

        if (-not [string]::IsNullOrWhiteSpace($Version) -and $marker.version -ne $Version)
        {
            return $false
        }

        if ($marker.sha256 -notmatch '^[a-fA-F0-9]{64}$')
        {
            return $false
        }

        if ($null -ne $Tool)
        {
            $declaredAssets = @($Tool.provider.assets.Values | Where-Object {
                    [string]$_.archive -eq [string]$marker.archive -and
                    [string]$_.sha256 -eq [string]$marker.sha256
                })

            if ($declaredAssets.Count -eq 0)
            {
                return $false
            }
        }

        return -not [string]::IsNullOrWhiteSpace([string]$marker.archive)
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxPowerShellMarker
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $markerPath = Join-Path $Path '.gamewip-tool'

    if (-not (Test-GameWipPlainFile -Path $markerPath))
    {
        return $false
    }

    try
    {
        $metadata = @{}

        foreach ($line in Get-Content -LiteralPath $markerPath)
        {
            if ([string]$line -match '^([^=]+)=(.*)$')
            {
                $metadata[$Matches[1]] = $Matches[2]
            }
        }
        $version = Split-Path -Leaf $Path

        $archivePattern = '^powershell-' + [regex]::Escape($version) + '-linux-(x64|arm64)\.tar\.gz$'

        if ($metadata.version -ne $version -or $metadata.archive -notmatch $archivePattern)
        {
            return $false
        }

        if ($metadata.sha256 -notmatch '^[a-fA-F0-9]{64}$')
        {
            return $false
        }

        return Test-Path -LiteralPath (Join-Path $Path 'pwsh') -PathType Leaf
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxCurrentPowerShell
{
    param([Parameter(Mandatory = $true)][string]$Path)

    try
    {
        $powerShellHome = [IO.Path]::GetFullPath($PSHOME)
        return Test-GameWipPathWithinRoot -Path $powerShellHome -Root $Path
    }

    catch
    {
        return $true
    }
}
