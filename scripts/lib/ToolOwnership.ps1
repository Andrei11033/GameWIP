# Managed-tool ownership and persistent-root policy.

# ------------------------------------------------------------
# Ownership markers and root validation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipManagedToolRootOwnership
{
    param([Parameter(Mandatory = $true)][string]$Root)
    $markerPath = Join-Path $Root '.gamewip-managed.json'
    $state = Read-GameWipOwnershipMarker -Path $markerPath -Resource 'project-tools'

    if ($state.Status -eq 'valid')
    {
        return $state.Marker
    }

    return $null
}

function Test-GameWipManagedToolRootOwnership
{
    param([Parameter(Mandatory = $true)][string]$Root)
    return $null -ne (Get-GameWipManagedToolRootOwnership -Root $Root)
}

function Initialize-GameWipManagedToolRoot
{
    param([switch]$AdoptExisting)

    # Resolve the root and its ownership boundary.
    $root = Get-GameWipManagedToolRoot
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))
    $projectOwnedRoot = Test-GameWipPathWithinRoot -Path $root -Root $buildRoot

    # Track the marker and installation origin.
    $created = $false
    $origin = 'created'
    $ownership = $null

    # Validate the existing root before claiming it.
    if (Test-Path -LiteralPath $root)
    {
        $ownership = Get-GameWipManagedToolRootOwnership -Root $root
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne '.gamewip-managed.json' })

        if ($null -eq $ownership -and $entries.Count -ne 0 -and -not $projectOwnedRoot)
        {
            if (-not $AdoptExisting)
            {
                throw "Refusing to adopt non-empty GameWIPTools root without persistent ownership proof: '$root'."
            }
            $origin = 'adopted'
        }
        elseif ($null -eq $ownership)
        {
            $origin = if ($projectOwnedRoot -and $entries.Count -ne 0)
            {
                'claimedProjectOwned'
            }
            else
            {
                'claimedEmpty'
            }
        }
    }
    else
    {
        Set-GameWipMutationStarted
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        $created = $true
        $origin = 'created'
    }

    # Record ownership when the root has no valid marker.
    if ($null -eq $ownership)
    {
        $marker = New-GameWipOwnershipMarker -Resource 'project-tools' -Origin $origin -Payload ([ordered]@{
                installedBySetup = $created -or $origin -ne 'adopted'
                adoptedByUser = $origin -eq 'adopted'
            })
        Write-GameWipJsonAtomic -Path (Join-Path $root '.gamewip-managed.json') -Value $marker
    }

    # Keep the standard managed-tool directories available.
    foreach ($name in @('bin', 'tools', 'npm', 'python', 'powershell'))
    {
        $directory = Join-Path $root $name

        if (-not (Test-Path -LiteralPath $directory -PathType Container))
        {
            Set-GameWipMutationStarted
            New-Item -ItemType Directory -Path $directory | Out-Null
        }
    }
}
