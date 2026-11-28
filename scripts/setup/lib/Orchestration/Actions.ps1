# ------------------------------------------------------------
# Action catalog and presentation
# ------------------------------------------------------------

function Get-GameWipSetupActionOptionIds
{
    param([Parameter(Mandatory = $true)][string]$Action)

    $ids = @(
        $SetupActionConfig.Options |
            Where-Object { $_.ContainsKey('Global') -and [bool]$_.Global } |
            ForEach-Object { [string]$_.Id }
    )

    if ($SetupActionConfig.ActionOptions.ContainsKey($Action))
    {
        $ids += @($SetupActionConfig.ActionOptions[$Action])
    }

    return @($ids | Select-Object -Unique)
}

# ------------------------------------------------------------
# Option declarations and action mappings
# ------------------------------------------------------------

function Assert-GameWipSetupOptionCatalog
{
    # Validate option identifiers and required values.
    $options = @($SetupActionConfig.Options)
    Assert-GameWipUniqueId -Items $options -Label 'setup option'
    $optionIds = @{}

    foreach ($option in $options)
    {
        $id = [string]$option.Id

        if ($id -notmatch '^[A-Za-z][A-Za-z0-9]*$')
        {
            throw "Setup option '$id' has an invalid identifier."
        }

        $optionIds[$id.ToLowerInvariant()] = $id

        if ($option.Kind -in @('string', 'integer', 'choice', 'string-list') -and [string]::IsNullOrWhiteSpace([string]$option.ValueName))
        {
            throw "Setup option '$id' must declare valueName."
        }

        if ($option.Kind -eq 'choice' -and @($option.Choices).Count -eq 0)
        {
            throw "Setup option '$id' must declare choices."
        }
    }

    # Validate action-option mappings.
    $actionIds = @($SetupActionConfig.Actions | ForEach-Object { [string]$_.Id })

    foreach ($actionName in $SetupActionConfig.ActionOptions.Keys)
    {
        if ($actionIds -notcontains [string]$actionName)
        {
            throw "Setup option mapping references unknown action '$actionName'."
        }

        foreach ($optionId in @($SetupActionConfig.ActionOptions[$actionName]))
        {
            if (-not $optionIds.ContainsKey(([string]$optionId).ToLowerInvariant()))
            {
                throw "Setup action '$actionName' references unknown option '$optionId'."
            }
        }
    }

    foreach ($actionId in $actionIds)
    {
        if (-not $SetupActionConfig.ActionOptions.ContainsKey($actionId))
        {
            throw "Setup action '$actionId' is missing an option mapping."
        }
    }
}

function Assert-GameWipSetupActionOptions
{
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][hashtable]$BoundParameters
    )

    $allowed = @{}

    foreach ($optionId in @(Get-GameWipSetupActionOptionIds -Action $Action))
    {
        $allowed[([string]$optionId).ToLowerInvariant()] = $true
    }

    foreach ($boundName in $BoundParameters.Keys)
    {
        $boundOption = @($SetupActionConfig.Options | Where-Object { [string]$_.Id -ieq [string]$boundName })

        if ($boundOption.Count -eq 0)
        {
            continue
        }

        if (-not $allowed.ContainsKey(([string]$boundName).ToLowerInvariant()))
        {
            throw "Option '-$boundName' is not supported for setup action '$Action'."
        }
    }
}

function Assert-GameWipSetupActionCatalog
{
    # Validate the required action set and menu keys.
    $actions = @($SetupActionConfig.Actions)
    Assert-GameWipSetupOptionCatalog
    Assert-GameWipUniqueId -Items $actions -Label 'setup action'
    $requiredActionIds = @(
        'menu', 'full', 'check', 'update', 'repair', 'deps', 'uninstall', 'tool',
        'vs', 'msys2', 'system', 'repo', 'profiler', 'editor', 'doc', 'list', 'help'
    )
    $actionIds = @($actions | ForEach-Object { [string]$_.Id })

    if ((($requiredActionIds | Sort-Object) -join "`n") -cne (($actionIds | Sort-Object) -join "`n"))
    {
        $missingActions = @($requiredActionIds | Where-Object { $actionIds -cnotcontains $_ })
        $unexpectedActions = @($actionIds | Where-Object { $requiredActionIds -cnotcontains $_ })
        $differences = @()

        if ($missingActions.Count -gt 0)
        {
            $differences += "missing required actions: $($missingActions -join ', ')"
        }

        if ($unexpectedActions.Count -gt 0)
        {
            $differences += "unexpected configured actions: $($unexpectedActions -join ', ')"
        }

        throw "Setup action catalog drift: $($differences -join '; ')."
    }

    $duplicateKeys = @(
        $actions |
            Where-Object { $_.ContainsKey('Key') } |
            ForEach-Object { [string]$_.Key } |
            Group-Object |
            Where-Object Count -gt 1
    )

    if ($duplicateKeys.Count -ne 0)
    {
        throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')."
    }

    # Confirm bootstrap tools and host declarations are registered.
    foreach ($toolId in @($SetupActionConfig.BootstrapToolIds))
    {
        if (@($ProjectTools.tools.id) -notcontains $toolId)
        {
            throw "Setup bootstrap tool '$toolId' is not registered."
        }
    }

    foreach ($action in $actions)
    {
        if (-not $action.ContainsKey('Platforms'))
        {
            continue
        }

        foreach ($platform in @($action.Platforms))
        {
            if ([string]$platform -notin @('windows', 'linux'))
            {
                throw "Setup action '$($action.Id)' has an unsupported host platform '$platform'."
            }
        }
    }
}

# ------------------------------------------------------------
# Action lookup and presentation
# ------------------------------------------------------------

function Get-GameWipAvailableSetupActions
{
    $hostPlatform = if (Test-GameWipWindowsHost)
    {
        'windows'
    }
    else
    {
        'linux'
    }

    return @($SetupActionConfig.Actions | Where-Object {
            (-not $_.ContainsKey('Platforms')) -or @($_.Platforms) -contains $hostPlatform
        })
}

function Resolve-GameWipSetupActionName
{
    param([Parameter(Mandatory = $true)][string]$Name)

    foreach ($action in @(Get-GameWipAvailableSetupActions))
    {
        if ([string]$action.Id -ieq $Name)
        {
            return [string]$action.Id
        }
    }

    return $null
}

function Get-GameWipSetupAction
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $canonicalId = Resolve-GameWipSetupActionName -Name $Id
    $actionMatches = @($SetupActionConfig.Actions | Where-Object { $_.Id -eq $canonicalId })

    if ($actionMatches.Count -ne 1)
    {
        $listCommand = "$(Get-GameWipSetupHelperCommand) list"
        throw "Unknown setup action '$Id'. Run '$listCommand'."
    }

    return $actionMatches[0]
}

function Show-GameWipSetupActionCatalog
{
    Write-GameWipSection 'Setup actions'

    foreach ($actionInfo in @(Get-GameWipAvailableSetupActions))
    {
        Write-Host ('  {0,-16} [{1,-11}] {2}' -f $actionInfo.Id, $actionInfo.Risk, $actionInfo.Description)
    }
}

function Show-GameWipSetupHelp
{
    Write-Host 'Usage:'

    if (Test-GameWipWindowsHost)
    {
        Write-Host '  setup.bat <action> [options]'
    }
    else
    {
        Write-Host '  bash ./setup.sh <action> [options]'
    }
    Write-Host ''
    Show-GameWipCommonControlHelp -OptionDefinitions $SetupActionConfig.Options
    Write-Host ''
    Write-Host 'Action-specific options:'
    Show-GameWipOptionDefinitions -OptionDefinitions $SetupActionConfig.Options -OptionIds @('Branch', 'SkipDocs')
    Write-Host ''
    Show-GameWipSetupActionCatalog
}

function Show-GameWipSetupSizeEstimate
{
    Write-GameWipSection 'Estimated resource use'
    Write-Host '  Download: approximately 1-4 GB, depending on editor selection and existing packages.'
    Write-Host '  Installed disk: approximately 4-15 GB.'
    Write-Host '  Temporary build space: up to approximately 6 GB, primarily Tracy and documentation.'
}
