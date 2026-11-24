# Command catalog lookups and shared validation helpers.

Set-StrictMode -Version Latest

function Resolve-GameWipCommandConfigValue
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $value = $CommandConfig

    foreach ($segment in ($Path -split '\.'))
    {
        if ($value -isnot [System.Collections.IDictionary] -or -not $value.Contains($segment))
        {
            throw "Command configuration value '$Path' does not exist."
        }

        $value = $value[$segment]
    }

    return $value
}

# ------------------------------------------------------------
# Commands and bundles
# ------------------------------------------------------------

function Get-GameWipVisiblePresetName
{
    param([Parameter(Mandatory = $true)][string]$Kind)
    $property = switch ($Kind)
    {
        'configure'
        {
            'configurePresets'
        }
        'build'
        {
            'buildPresets'
        }
        'test'
        {
            'testPresets'
        }
        default
        {
            throw "Unknown preset kind '$Kind'."
        }
    }

    return @($PresetData.$property | Where-Object { -not ($_.PSObject.Properties.Name -contains 'hidden' -and $_.hidden) } | ForEach-Object { $_.name })
}

function Assert-GameWipValidPreset
{
    param([Parameter(Mandatory = $true)][string]$Kind, [Parameter(Mandatory = $true)][string]$Name)

    if ((Get-GameWipVisiblePresetName -Kind $Kind) -notcontains $Name)
    {
        throw "Unknown $Kind preset '$Name'. Run 'gamewip list' to see available presets."
    }
}

function Assert-GameWipValidModule
{
    param([Parameter(Mandatory = $true)][string]$Name)

    if ($Name -ne 'all' -and @(Get-GameWipValidationModuleName) -notcontains $Name)
    {
        throw "Unknown validation module '$Name'. Run 'gamewip list' to see available modules."
    }
}

function Get-GameWipValidationModuleName
{
    return @($CommandConfig.Modules | ForEach-Object { [string]$_.Id })
}

# Read module registrations; support folders also own CMake files.
function Get-GameWipRegisteredValidationModuleName
{
    param([string]$Root = (Join-Path $RepositoryRoot 'game\validation\tests'))

    $registrationPattern = [regex]::new('(?im)^[ \t]*gamewip_add_test_module[ \t\r\n]*\([ \t\r\n]*NAME[ \t\r\n]+([A-Za-z0-9_-]+)')
    $moduleNames = [System.Collections.Generic.List[string]]::new()

    foreach ($moduleDirectory in Get-ChildItem -LiteralPath $Root -Directory)
    {
        $cmakePath = Join-Path $moduleDirectory.FullName 'CMakeLists.txt'

        if (-not (Test-Path -LiteralPath $cmakePath -PathType Leaf))
        {
            continue
        }

        $cmakeSource = Get-Content -Raw -LiteralPath $cmakePath

        foreach ($registration in $registrationPattern.Matches($cmakeSource))
        {
            $moduleNames.Add($registration.Groups[1].Value)
        }
    }

    return @($moduleNames | Sort-Object)
}

function Get-GameWipValidationModule
{
    param([Parameter(Mandatory = $true)][string]$Name)
    $module = @($CommandConfig.Modules | Where-Object { $_.Id -eq $Name } | Select-Object -First 1)

    if ($module.Count -eq 0)
    {
        throw "Unknown validation module '$Name'. Run 'gamewip list' to see available modules."
    }

    return $module[0]
}

function Get-GameWipProjectCommand
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $command = @($CommandConfig.ProjectCommands | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)

    if ($command.Count -eq 0)
    {
        throw "Unknown project command '$Id'. Run 'gamewip list' to see available commands."
    }

    return $command[0]
}

function Get-GameWipProjectBundle
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $bundleInfo = @($CommandConfig.Bundles | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)

    if ($bundleInfo.Count -eq 0)
    {
        throw "Unknown bundle '$Id'. Run 'gamewip list' to see available bundles."
    }

    return $bundleInfo[0]
}

function Assert-GameWipUniqueId
{
    param([Parameter(Mandatory = $true)][string]$Label, [Parameter(Mandatory = $true)][object[]]$Items)
    $duplicates = @($Items | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object { $_.Count -gt 1 } | ForEach-Object { $_.Name })

    if ($duplicates.Count -ne 0)
    {
        throw "Duplicate $Label IDs: $($duplicates -join ', ')."
    }
}

function Assert-GameWipCommandBuilderOptionSet
{
    param(
        [Parameter(Mandatory = $true)][string]$Label,
        [Parameter(Mandatory = $true)][object[]]$Options,
        [string[]]$ValidCommonCapabilities = @(),
        [string[]]$ValidPlatforms = @()
    )

    Assert-GameWipUniqueId -Label "$Label option" -Items $Options
    $validKinds = @('boolean', 'choice', 'integer', 'text', 'multi-choice')

    foreach ($option in $Options)
    {
        $optionLabel = "$Label option '$($option.Id)'"

        if ($validKinds -notcontains [string]$option.Kind)
        {
            throw "$optionLabel has unknown kind '$($option.Kind)'."
        }

        # Match argument placeholders to each option kind.
        $hasValue = @($option.Arguments | Where-Object { ([string]$_).Contains('{value}') }).Count -gt 0

        if ($option.Kind -eq 'boolean' -and $hasValue)
        {
            throw "$optionLabel must not contain '{value}'."
        }

        if ($option.Kind -ne 'boolean' -and -not $hasValue)
        {
            throw "$optionLabel must contain '{value}'."
        }

        # Validate common capability and platform requirements.
        $requiredCapabilities = if ($option.ContainsKey('requiresCommon'))
        {
            @($option.requiresCommon)
        }
        else
        {
            @()
        }

        foreach ($capability in $requiredCapabilities)
        {
            if ($ValidCommonCapabilities -notcontains [string]$capability)
            {
                throw "$optionLabel requires unknown common capability '$capability'."
            }
        }

        $requiredPlatforms = if ($option.ContainsKey('requiresPlatforms'))
        {
            @($option.requiresPlatforms)
        }
        else
        {
            @()
        }

        foreach ($platform in $requiredPlatforms)
        {
            if ($ValidPlatforms -notcontains [string]$platform)
            {
                throw "$optionLabel requires unknown platform '$platform'."
            }
        }

        # Validate choice values and defaults.
        if ($option.Kind -in @('choice', 'multi-choice'))
        {
            $choices = @($option.Choices)

            if ($choices.Count -eq 0)
            {
                throw "$optionLabel must declare choices."
            }
        }

        if ($option.Kind -eq 'choice' -and $choices -notcontains [string]$option.Default)
        {
            throw "Default for $optionLabel is not one of its choices."
        }

        if ($option.Kind -eq 'multi-choice')
        {
            if ([string]::IsNullOrEmpty([string]$option.ValueSeparator))
            {
                throw "$optionLabel must declare a value separator."
            }

            foreach ($default in @($option.Default))
            {
                if ($choices -notcontains [string]$default)
                {
                    throw "Default for $optionLabel contains unknown choice '$default'."
                }
            }

            foreach ($exclusive in @($option.ExclusiveChoices))
            {
                if ($choices -notcontains [string]$exclusive)
                {
                    throw "$optionLabel marks unknown choice '$exclusive' as exclusive."
                }
            }
        }
    }
}

function Assert-GameWipBundleAcyclic
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [Parameter(Mandatory = $true)]$Lookup,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Visiting,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Visited
    )

    if ($Visited.Contains($Id))
    {
        return
    }

    if (-not $Visiting.Add($Id))
    {
        throw "Bundle cycle detected at '$Id'."
    }

    foreach ($step in @($Lookup[$Id].Steps | Where-Object { $_.Kind -eq 'Bundle' }))
    {
        Assert-GameWipBundleAcyclic -Id ([string]$step.Bundle) -Lookup $Lookup -Visiting $Visiting -Visited $Visited
    }
    $Visiting.Remove($Id) | Out-Null
    $Visited.Add($Id) | Out-Null
}

function Resolve-GameWipActionName
{
    param([Parameter(Mandatory = $true)][string]$Name)

    $normalized = $Name.ToLowerInvariant()

    foreach ($action in @($CommandConfig.Actions))
    {
        if ([string]$action.Id -ieq $normalized)
        {
            return [string]$action.Id
        }
    }

    return $null
}

function Get-GameWipOptionDefinition
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $optionMatches = @($CommandConfig.Options | Where-Object { [string]$_.Id -ieq $Id })

    if ($optionMatches.Count -ne 1)
    {
        throw "Unknown project option '$Id'."
    }

    return $optionMatches[0]
}

function Get-GameWipActionOptionIds
{
    param([Parameter(Mandatory = $true)][string]$Action)

    $ids = @($CommandConfig.Options | Where-Object { $_.ContainsKey('Global') -and [bool]$_.Global } | ForEach-Object { [string]$_.Id })

    if ($CommandConfig.ActionOptions.ContainsKey($Action))
    {
        $ids += @($CommandConfig.ActionOptions[$Action])
    }

    return @($ids | Select-Object -Unique)
}

function Assert-GameWipOptionCatalog
{
    $options = @($CommandConfig.Options)
    Assert-GameWipUniqueId -Label 'project option' -Items $options
    $optionIds = @{}

    foreach ($option in $options)
    {
        $id = [string]$option.Id

        if ($id -notmatch '^[A-Za-z][A-Za-z0-9]*$')
        {
            throw "Project option '$id' has an invalid identifier."
        }

        $optionIds[$id.ToLowerInvariant()] = $id

        if ($option.Kind -in @('string', 'integer', 'choice', 'string-list') -and [string]::IsNullOrWhiteSpace([string]$option.ValueName))
        {
            throw "Project option '$id' must declare valueName."
        }

        if ($option.Kind -eq 'choice' -and @($option.Choices).Count -eq 0)
        {
            throw "Project option '$id' must declare choices."
        }
    }
    $actionIds = @($CommandConfig.Actions | ForEach-Object { [string]$_.Id })

    # Validate each action-to-option mapping.
    foreach ($actionName in $CommandConfig.ActionOptions.Keys)
    {
        if ($actionIds -notcontains [string]$actionName)
        {
            throw "Project option mapping references unknown action '$actionName'."
        }

        foreach ($optionId in @($CommandConfig.ActionOptions[$actionName]))
        {
            if (-not $optionIds.ContainsKey(([string]$optionId).ToLowerInvariant()))
            {
                throw "Project action '$actionName' references unknown option '$optionId'."
            }
        }
    }

    # Require a mapping for every configured action.
    foreach ($actionId in $actionIds)
    {
        if (-not $CommandConfig.ActionOptions.ContainsKey($actionId))
        {
            throw "Project action '$actionId' is missing an option mapping."
        }
    }
}

function Assert-GameWipActionOptions
{
    param(
        [Parameter(Mandatory = $true)][string]$Action,
        [Parameter(Mandatory = $true)][hashtable]$BoundParameters
    )

    $allowed = @{}

    foreach ($optionId in @(Get-GameWipActionOptionIds -Action $Action))
    {
        $allowed[([string]$optionId).ToLowerInvariant()] = $true
    }

    foreach ($boundName in $BoundParameters.Keys)
    {
        $boundOption = @($CommandConfig.Options | Where-Object { [string]$_.Id -ieq [string]$boundName })

        if ($boundOption.Count -eq 0)
        {
            continue
        }

        if (-not $allowed.ContainsKey(([string]$boundName).ToLowerInvariant()))
        {
            throw "Option '-$boundName' is not supported for project action '$Action'."
        }
    }
}
