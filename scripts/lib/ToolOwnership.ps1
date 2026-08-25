# GameWIP managed-tool ownership and persistent root policy.

Set-StrictMode -Version Latest

function Copy-GameWipValue
{
    param([AllowNull()]$Value)
    if ($null -eq $Value -or $Value -is [string] -or $Value.GetType().IsValueType)
    {
        return $Value
    }
    if ($Value -is [System.Collections.IDictionary])
    {
        $copy = @{}
        foreach ($key in $Value.Keys)
        {
            $copy[[string]$key] = Copy-GameWipValue -Value $Value[$key]
        }
        return $copy
    }
    if ($Value -is [System.Collections.IEnumerable])
    {
        return , @($Value | ForEach-Object { Copy-GameWipValue -Value $_ })
    }
    return ConvertTo-GameWipHashtable -Value $Value
}

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
    $root = (Get-GameWipManagedToolRoot)
    $created = $false
    $origin = 'created'
    if (Test-Path -LiteralPath $root)
    {
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue | Where-Object { $_.Name -ne '.gamewip-managed.json' })
        if ((Test-GameWipWindowsHost) -and -not (Test-GameWipManagedToolRootOwnership -Root $root) -and $entries.Count -ne 0)
        {
            if (-not $AdoptExisting)
            {
                throw "Refusing to adopt non-empty GameWIPTools root without persistent ownership proof: '$root'."
            }
            $origin = 'adopted'
        }
        elseif (-not (Test-GameWipManagedToolRootOwnership -Root $root))
        {
            $origin = 'claimedEmpty'
        }
    }
    else
    {
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        $created = $true
        $origin = 'created'
    }

    $marker = New-GameWipOwnershipMarker -Resource 'project-tools' -Origin $origin -Payload ([ordered]@{
            installedBySetup = $created -or $origin -ne 'adopted'
            adoptedByUser = $origin -eq 'adopted'
        })
    Write-GameWipJsonAtomic -Path (Join-Path $root '.gamewip-managed.json') -Value $marker
    foreach ($name in @('bin', 'tools', 'npm', 'python', 'powershell'))
    {
        New-Item -ItemType Directory -Force -Path (Join-Path $root $name) | Out-Null
    }
}
