# Setup ownership state and recovery.

# ------------------------------------------------------------
# Ownership state
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipSetupState
{
    $defaultState = [ordered]@{
        schemaVersion = 1
        wingetPackages = @()
        vscodeExtensions = @()
        systemPackages = @()
        msys2InstalledBySetup = $false
    }

    $script:SetupStateReadError = ''

    if (-not (Test-Path -LiteralPath $script:SetupStatePath -PathType Leaf))
    {
        $script:SetupStateReadStatus = 'missing'
        return $defaultState
    }

    try
    {
        $storedState = Get-Content -LiteralPath $script:SetupStatePath -Raw | ConvertFrom-Json -ErrorAction Stop
    }

    catch
    {
        $script:SetupStateReadStatus = 'corrupt'
        $script:SetupStateReadError = $_.Exception.Message
        return $defaultState
    }

    if ($null -eq $storedState)
    {
        $script:SetupStateReadStatus = 'corrupt'
        $script:SetupStateReadError = 'The state document must contain a JSON object.'
        return $defaultState
    }

    if ($storedState -isnot [pscustomobject])
    {
        $script:SetupStateReadStatus = 'corrupt'
        $script:SetupStateReadError = 'The state document must contain a JSON object.'
        return $defaultState
    }

    $repairReasons = [System.Collections.Generic.List[string]]::new()
    $schemaVersionProperty = $storedState.PSObject.Properties['schemaVersion']

    if ($null -eq $schemaVersionProperty -or $schemaVersionProperty.Value -ne 1)
    {
        $repairReasons.Add('schemaVersion is missing or unsupported') | Out-Null
    }

    # Normalize recorded tool and editor ownership.
    $stringLists = @{}

    foreach ($field in @('wingetPackages', 'vscodeExtensions'))
    {
        $property = $storedState.PSObject.Properties[$field]
        $values = [System.Collections.Generic.List[string]]::new()

        if ($null -eq $property -or $null -eq $property.Value)
        {
            $repairReasons.Add("$field is missing") | Out-Null
        }
        else
        {
            if ($property.Value -isnot [array])
            {
                $repairReasons.Add("$field is not an array") | Out-Null
            }

            foreach ($value in @($property.Value))
            {
                if ($value -is [string] -and -not [string]::IsNullOrWhiteSpace($value))
                {
                    $values.Add($value) | Out-Null
                }
                else
                {
                    $repairReasons.Add("$field contains an invalid entry") | Out-Null
                }
            }
        }

        $stringLists[$field] = @($values | Sort-Object -Unique)
    }

    # Normalize package ownership used by Linux uninstall.
    $systemPackages = [System.Collections.Generic.List[object]]::new()
    $systemPackageKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $systemPackagesProperty = $storedState.PSObject.Properties['systemPackages']

    if ($null -eq $systemPackagesProperty -or $null -eq $systemPackagesProperty.Value)
    {
        $repairReasons.Add('systemPackages is missing') | Out-Null
    }
    else
    {
        if ($systemPackagesProperty.Value -isnot [array])
        {
            $repairReasons.Add('systemPackages is not an array') | Out-Null
        }

        foreach ($entry in @($systemPackagesProperty.Value))
        {
            if ($null -eq $entry)
            {
                $repairReasons.Add('systemPackages contains an invalid entry') | Out-Null
                continue
            }

            $managerProperty = $entry.PSObject.Properties['manager']
            $packageProperty = $entry.PSObject.Properties['package']

            if ($null -eq $managerProperty -or $null -eq $packageProperty)
            {
                $repairReasons.Add('systemPackages contains an invalid entry') | Out-Null
                continue
            }

            $manager = ([string]$managerProperty.Value).ToLowerInvariant()
            $package = [string]$packageProperty.Value
            $key = "$manager`:$package"

            if ($manager -in @('apt', 'pacman') -and $package -match '^[A-Za-z0-9][A-Za-z0-9+_.-]*$')
            {
                if ($systemPackageKeys.Add($key))
                {
                    $systemPackages.Add([ordered]@{ manager = $manager; package = $package }) | Out-Null
                }
                else
                {
                    $repairReasons.Add('systemPackages contains duplicate entries') | Out-Null
                }
            }
            else
            {
                $repairReasons.Add('systemPackages contains an invalid entry') | Out-Null
            }
        }
    }

    # Normalize the MSYS2 ownership flag.
    $msys2InstalledBySetup = $false
    $msys2InstalledProperty = $storedState.PSObject.Properties['msys2InstalledBySetup']

    if ($null -eq $msys2InstalledProperty)
    {
        $repairReasons.Add('msys2InstalledBySetup is missing') | Out-Null
    }
    elseif ($msys2InstalledProperty.Value -is [bool])
    {
        $msys2InstalledBySetup = [bool]$msys2InstalledProperty.Value
    }
    elseif ($msys2InstalledProperty.Value -is [string] -and
        [bool]::TryParse([string]$msys2InstalledProperty.Value, [ref]$msys2InstalledBySetup))
    {
        $repairReasons.Add('msys2InstalledBySetup is not a JSON boolean') | Out-Null
    }
    else
    {
        $repairReasons.Add('msys2InstalledBySetup is invalid') | Out-Null
    }

    if ($repairReasons.Count -eq 0)
    {
        $script:SetupStateReadStatus = 'valid'
    }
    else
    {
        $script:SetupStateReadStatus = 'needs-repair'
        $script:SetupStateReadError = (@($repairReasons | Sort-Object -Unique) -join '; ')
    }

    return [ordered]@{
        schemaVersion = 1

        # External tool and editor ownership
        wingetPackages = @($stringLists.wingetPackages)
        vscodeExtensions = @($stringLists.vscodeExtensions)

        # Linux package ownership
        systemPackages = @($systemPackages.ToArray() | Sort-Object manager, package)

        # MSYS2 ownership
        msys2InstalledBySetup = $msys2InstalledBySetup
    }
}

function Save-GameWipSetupState
{
    param([Parameter(Mandatory = $true)]$State)

    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $script:SetupStatePath) | Out-Null
    Write-GameWipJsonAtomic -Path $script:SetupStatePath -Value $State -Depth 8

    $script:SetupStateReadStatus = 'valid'
}

function Repair-GameWipSetupState
{
    $state = Get-GameWipSetupState

    if ($script:SetupStateReadStatus -in @('needs-repair', 'corrupt'))
    {
        Save-GameWipSetupState -State $state
    }

    return $state
}

function Add-GameWipOwnedVsCodeExtension
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $state = Get-GameWipSetupState
    $state.vscodeExtensions = @(@($state.vscodeExtensions) + @($Id) | Sort-Object -Unique)
    Save-GameWipSetupState -State $state
}

function Add-GameWipOwnedWingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $state = Get-GameWipSetupState
    $state.wingetPackages = @(@($state.wingetPackages) + @($Id) | Sort-Object -Unique)
    Save-GameWipSetupState -State $state
}

function Add-GameWipOwnedSystemPackages
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('apt', 'pacman')][string]$Manager,
        [Parameter(Mandatory = $true)][string[]]$Packages
    )

    $state = Get-GameWipSetupState
    $entries = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($entry in @($state.systemPackages))
    {
        $entries["$($entry.manager):$($entry.package)"] = $entry
    }

    foreach ($package in $Packages)
    {
        if ($package -match '^[A-Za-z0-9][A-Za-z0-9+_.-]*$')
        {
            $key = "$Manager`:$package"
            $entries[$key] = [ordered]@{ manager = $Manager; package = $package }
        }
    }

    $state.systemPackages = @($entries.Values | Sort-Object manager, package)
    Save-GameWipSetupState -State $state
}
