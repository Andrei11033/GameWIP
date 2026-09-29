# Help, command catalogs, and structured diagnostic output.

# ------------------------------------------------------------
# Catalog and diagnostic presentation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Show-GameWipProjectCatalog
{
    Write-GameWipSection 'Project helper actions'
    foreach ($group in @('development', 'validation', 'quality', 'tools', 'repository', 'maintenance', 'navigation'))
    {
        $items = @($CommandConfig.Actions | Where-Object { $_.Visible -and $_.Group -eq $group })
        if ($items.Count -eq 0)
        {
            continue
        }
        Write-Host "  $((Get-Culture).TextInfo.ToTitleCase($group)):"
        foreach ($action in $items)
        {
            if ($null -ne $action.Aliases -and @($action.Aliases).Count -gt 0)
            {
                $aliases = " (aliases: $(@($action.Aliases) -join ', '))"
            }
            else
            {
                $aliases = ''
            }
            Write-Host ('    {0,-12} [{1,-11}] {2}{3}' -f $action.Id, $action.Risk, $action.Description, $aliases)
        }
    }

    foreach ($kind in @('configure', 'build', 'test'))
    {
        Write-GameWipSection ((Get-Culture).TextInfo.ToTitleCase($kind) + ' presets')
        Get-GameWipVisiblePresetName -Kind $kind | ForEach-Object { Write-Host "  $_" }
    }
    Write-GameWipSection 'Validation modules'
    Write-Host '  all'
    @(Get-GameWipValidationModuleName) | ForEach-Object { Write-Host "  $_" }
    $builderModules = [System.Collections.Generic.List[object]]::new()
    foreach ($moduleName in @(Get-GameWipValidationModuleName))
    {
        $module = Get-GameWipValidationModule -Name $moduleName
        $options = @(Get-GameWipValidationBuilderOptions -ModuleName $moduleName -CommonCapabilities @('manual-tests', 'verbose-tests', 'test-support-child-process', 'report'))
        if ($options.Count -gt 0)
        {
            $builderModules.Add($module) | Out-Null
        }
    }
    if ($builderModules.Count -gt 0)
    {
        Write-GameWipSection 'Validation builder options'
        foreach ($module in $builderModules)
        {
            Write-Host "  $($module.Id)"
            foreach ($option in $module.BuilderOptions)
            {
                Write-Host ('    {0,-16} [{1}] {2}' -f $option.Id, $option.Kind, $option.Title)
            }
        }
    }
    Write-GameWipSection 'Project commands'
    foreach ($command in $CommandConfig.ProjectCommands)
    {
        Write-Host ("  {0,-24} {1}" -f $command.Id, $command.Name)
    }
    Write-GameWipSection 'Bundles'
    foreach ($bundleInfo in $CommandConfig.Bundles)
    {
        Write-Host ("  {0,-24} {1}" -f $bundleInfo.Id, $bundleInfo.Name)
    }
    Write-GameWipSection 'Benchmark profiles'
    foreach ($benchmarkProfile in $CommandConfig.BenchmarkProfiles)
    {
        Write-Host ("  {0,-12} {1}" -f $benchmarkProfile.Id, $benchmarkProfile.Name)
    }
    Write-GameWipSection 'Hygiene profiles'
    foreach ($hygieneProfile in $HygieneConfig.Profiles)
    {
        Write-Host ("  {0,-12} {1}" -f $hygieneProfile.Id, $hygieneProfile.Title)
    }
    Write-GameWipSection 'Hygiene checks'
    foreach ($check in $HygieneConfig.Checks)
    {
        Write-Host ("  {0,-24} [{1,-9}] {2}" -f $check.Id, $check.Availability, $check.Title)
    }
    Show-GameWipWorkflowCatalog
}

function Show-GameWipActionFailure
{
    param([Parameter(Mandatory = $true)][System.Management.Automation.ErrorRecord]$ErrorRecord)

    $exception = $ErrorRecord.Exception
    $code = if ($exception.Data.Contains('GameWipCode'))
    {
        [string]$exception.Data['GameWipCode']
    }
    else
    {
        'operation-failed'
    }
    $details = if ($exception.Data.Contains('GameWipDetails'))
    {
        [string]$exception.Data['GameWipDetails']
    }
    else
    {
        ''
    }
    $suggestions = @(if ($exception.Data.Contains('GameWipSuggestedActions'))
        {
            $exception.Data['GameWipSuggestedActions']
        })
    $logPath = if ($exception.Data.Contains('GameWipLogPath'))
    {
        [string]$exception.Data['GameWipLogPath']
    }
    else
    {
        ''
    }

    Write-Host ''
    Write-GameWipHost "Action failed [$code]" -ForegroundColor Red
    Write-GameWipHost $exception.Message -ForegroundColor Red
    if (-not [string]::IsNullOrWhiteSpace($details))
    {
        Write-Host $details
    }
    if (-not [string]::IsNullOrWhiteSpace($logPath))
    {
        Write-Host "Log: $logPath"
    }
    if ($suggestions.Count -eq 0)
    {
        $suggestions = @('Inspect the retained run/step log.', 'Rerun the smallest focused GameWIP command that reproduces the failure.')
    }
    Write-GameWipHost 'What to do next:' -ForegroundColor Cyan
    foreach ($suggestion in $suggestions)
    {
        Write-Host "  - $suggestion"
    }
}

function Show-GameWipOptionDefinitions
{
    param(
        [Parameter(Mandatory = $true)][object[]]$OptionDefinitions,
        [Parameter(Mandatory = $true)][string[]]$OptionIds
    )

    foreach ($optionId in $OptionIds)
    {
        $option = @($OptionDefinitions | Where-Object { [string]$_.Id -ieq $optionId }) | Select-Object -First 1
        if ($null -eq $option)
        {
            continue
        }
        $signature = "-$($option.Id)"
        if ($option.Kind -ne 'switch')
        {
            $signature += " <$($option.ValueName)>"
        }
        $description = [string]$option.Description
        if ($option.Kind -eq 'choice')
        {
            $description += " Choices: $(@($option.Choices) -join '|')."
        }
        Write-Host ('  {0,-34} {1}' -f $signature, $description)
    }
}

function Show-GameWipCommonControlHelp
{
    param(
        [object[]]$OptionDefinitions = @(),
        [string[]]$AdditionalOptions = @()
    )

    Write-Host 'Common control options:'
    if ($OptionDefinitions.Count -eq 0 -and $null -ne (Get-Variable -Name CommandConfig -ErrorAction SilentlyContinue))
    {
        $OptionDefinitions = @($CommandConfig.Options)
    }
    $globalOptions = @($OptionDefinitions | Where-Object { $_.ContainsKey('Global') -and [bool]$_.Global } | ForEach-Object { [string]$_.Id })
    Show-GameWipOptionDefinitions -OptionDefinitions $OptionDefinitions -OptionIds $globalOptions
    foreach ($option in $AdditionalOptions)
    {
        Write-Host $option
    }
}

function Show-GameWipHelp
{
    Write-Host 'Usage:'
    Write-Host '  .\gamewip.bat <action> [command] [target] [options]'
    Write-Host '  .\gamewip.bat                         Open the interactive UI.'
    Write-Host ''
    Write-Host 'Available commands:'
    Write-Host '  ready'
    Write-Host '  git <status|fetch|switch|update|cleanup|create|push|log> [branch]'
    Write-Host '  workflow <list|status|run> [workflow-id]'
    Write-Host '  unicode <status|verify|regenerate>'
    Write-Host '  format <check|apply>'
    Write-Host '  quality <check|fix|status> [-ChangedOnly] [-FailFast]'
    Write-Host '  quality hygiene [standard|deep|check-id|list|status] [-FailOnFindings]'
    Write-Host '  tool <list|status|check-updates|ensure|update> [tool-id|category|all]'
    Write-Host '  deps <check|prepare>'
    Write-Host '  config [preset] [-CleanBuild] [-Offline]'
    Write-Host '  build [preset] [-CleanBuild] [-Offline]'
    Write-Host '  test [preset] [-SkipBuild] [-CleanBuild]'
    Write-Host '  module [name] [-SkipBuild] [-PassThroughArgs <arguments>]'
    Write-Host '  wizard [-SkipBuild]'
    Write-Host '  stress [name] [-RunCount N] [-WorkerCount N] [-FailFast] [-SkipBuild]'
    Write-Host '  run [project-command] [-SkipBuild] [-PassThroughArgs <arguments>]'
    Write-Host '  bundle [id] [-SkipBuild] [-CleanBuild]'
    Write-Host '  doc | analyze | cov | asan | ubsan | links'
    Write-Host '  bench <run|dry-run|list|compare> [options]'
    Write-Host '  history list [all] | history show [latest|run-name] | history clean [run-name|all]'
    Write-Host '  list | help'
    Write-Host ''
    Write-Host 'Short and compatibility aliases:'
    foreach ($action in @($CommandConfig.Actions | Where-Object { $null -ne $_.Aliases -and @($_.Aliases).Count -gt 0 }))
    {
        Write-Host "  $($action.Id): $(@($action.Aliases) -join ', ')"
    }
    Write-Host ''
    Show-GameWipCommonControlHelp -OptionDefinitions $CommandConfig.Options
    Write-Host ''
    Write-Host 'Action-specific options:'
    foreach ($action in @($CommandConfig.Actions | Where-Object { $CommandConfig.ActionOptions.ContainsKey([string]$_.Id) }))
    {
        $specific = @($CommandConfig.ActionOptions[[string]$action.Id] | Where-Object { @($CommandConfig.Options | Where-Object Id -eq $_ | Where-Object { $_.ContainsKey('Global') -and [bool]$_.Global }).Count -eq 0 })
        if ($specific.Count -eq 0)
        {
            continue
        }
        Write-Host "  $($action.Id):"
        Show-GameWipOptionDefinitions -OptionDefinitions $CommandConfig.Options -OptionIds $specific
    }
    Write-Host ''
    Write-Host 'Use .\gamewip.bat list to discover valid IDs.'
}
