# Validation command construction and interactive options.

# ------------------------------------------------------------
# Validation command options
# ------------------------------------------------------------

function Split-GameWipExtraArgument
{
    param([AllowEmptyString()][string]$Text)

    if ([string]::IsNullOrWhiteSpace($Text))
    {
        return @()
    }

    return @($Text -split ' ' | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
}

function Get-GameWipValidationBuilderCapabilities
{
    param([bool]$VerboseTests, [bool]$ManualTests, [bool]$ChildProcesses, [bool]$WriteReport)
    $capabilities = [System.Collections.Generic.List[string]]::new()

    if ($VerboseTests)
    {
        $capabilities.Add('verbose-tests') | Out-Null
    }

    if ($ManualTests)
    {
        $capabilities.Add('manual-tests') | Out-Null
    }

    if ($ChildProcesses)
    {
        $capabilities.Add('test-support-child-process') | Out-Null
    }

    if ($WriteReport)
    {
        $capabilities.Add('report') | Out-Null
    }

    return $capabilities.ToArray()
}

function Get-GameWipValidationBuilderOptions
{
    param([Parameter(Mandatory = $true)][string]$ModuleName, [string[]]$CommonCapabilities = @())
    $module = Get-GameWipValidationModule -Name $ModuleName
    $options = @(if ($module -is [hashtable] -and $module.ContainsKey('builderOptions'))
        {
            @($module['builderOptions'])
        }
        else
        {
            @()
        })
    return Get-GameWipCommandBuilderOptions -Options $options -CommonCapabilities $CommonCapabilities
}

# ------------------------------------------------------------
# Validation command wizard
# ------------------------------------------------------------

function Invoke-GameWipValidationCommandWizard
{
    param([switch]$NoBuild)
    Write-GameWipSection 'Validation command builder'

    # Select the validation scope and optional exclusions.
    $moduleNames = @(Get-GameWipValidationModuleName)
    $moduleResult = Read-GameWipMenuChoiceResult -Prompt 'Module selection' -Choices (@('all') + $moduleNames) -Default 'all'

    if ($moduleResult.Status -eq 'Cancelled')
    {
        return
    }

    $selectedModule = [string]$moduleResult.Value

    $skippedModules = @()

    if ($selectedModule -eq 'all')
    {
        $skipResult = Read-GameWipMultiChoiceResult -Prompt 'Modules to skip' -Choices $moduleNames

        if ($skipResult.Status -eq 'Cancelled')
        {
            return
        }

        $skippedModules = @($skipResult.Value)
    }
    $verboseTests = Read-GameWipYesNo -Prompt 'Mirror full test output to stdout?' -Default $false
    $manualTests = Read-GameWipYesNo -Prompt 'Enable manual tests?' -Default $false
    $childProcesses = Read-GameWipYesNo -Prompt 'Enable TestSupport child-process checks?' -Default $true
    $writeReport = Read-GameWipYesNo -Prompt 'Write retained test report?' -Default $false
    $reportPath = if ($writeReport)
    {
        Read-GameWipTextValue -Prompt 'Report path' -Default 'logs/validation/latest_test_report.txt'
    }
    else
    {
        ''
    }

    # Collect module-specific options that match the selected capabilities.
    $capabilities = @(Get-GameWipValidationBuilderCapabilities -VerboseTests:$verboseTests -ManualTests:$manualTests -ChildProcesses:$childProcesses -WriteReport:$writeReport)
    $builderArguments = [System.Collections.Generic.List[string]]::new()
    $candidateModules = @(if ($selectedModule -eq 'all')
        {
            @($moduleNames | Where-Object { $skippedModules -notcontains $_ })
        }
        else
        {
            @($selectedModule)
        })

    if ($candidateModules.Count -gt 0)
    {
        $configurableModules = @($candidateModules | Where-Object { @(Get-GameWipValidationBuilderOptions -ModuleName $_ -CommonCapabilities $capabilities).Count -gt 0 })

        if ($selectedModule -eq 'all' -and $configurableModules.Count -gt 0)
        {
            $configureResult = Read-GameWipMultiChoiceResult -Prompt 'Modules with additional options' -Choices $configurableModules

            if ($configureResult.Status -eq 'Cancelled')
            {
                return
            }
            $candidateModules = @($configureResult.Value)
        }

        foreach ($moduleName in $candidateModules)
        {
            foreach ($option in @(Get-GameWipValidationBuilderOptions -ModuleName $moduleName -CommonCapabilities $capabilities))
            {
                $value = Read-GameWipCommandBuilderOptionResult -Option $option

                if ($value -is [psobject] -and $value.PSObject.Properties.Name -contains 'Status' -and $value.Status -eq 'Cancelled')
                {
                    return
                }

                if ($option.Kind -eq 'multi-choice')
                {
                    $value = @($value.Value)
                }

                if ($null -ne $value)
                {
                    foreach ($argument in @(ConvertTo-GameWipCommandBuilderArguments -Option $option -Value $value))
                    {
                        $builderArguments.Add($argument) | Out-Null
                    }
                }
            }
        }
    }

    $extraText = Read-GameWipTextValue -Prompt 'Extra validation args, space-separated' -Default ''

    # Assemble common, module-specific, and additional arguments.
    $arguments = [System.Collections.Generic.List[string]]::new()

    if ($selectedModule -ne 'all')
    {
        $arguments.Add("--test-module=$selectedModule") | Out-Null
    }

    foreach ($item in $skippedModules)
    {
        $arguments.Add("--skip-test-module=$item") | Out-Null
    }

    if ($verboseTests)
    {
        $arguments.Add('--verbose-tests') | Out-Null
    }

    if ($manualTests)
    {
        $arguments.Add('--manual-tests') | Out-Null
    }

    if (-not $childProcesses)
    {
        $arguments.Add('--no-test-support-child-process') | Out-Null
    }

    if ($writeReport)
    {
        $arguments.Add("--test-report=$reportPath") | Out-Null
    }
    else
    {
        $arguments.Add('--no-test-report') | Out-Null
    }

    foreach ($argument in $builderArguments)
    {
        $arguments.Add($argument) | Out-Null
    }

    foreach ($argument in (Split-GameWipExtraArgument -Text $extraText))
    {
        $arguments.Add($argument) | Out-Null
    }

    # Show the resolved command and run it only after the user accepts.
    $command = Get-GameWipProjectCommand -Id 'test-all'
    $executable = Resolve-GameWipProjectExecutable -Command $command
    Write-GameWipSection 'Built command'
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $arguments.ToArray())

    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        Write-GameWipSemanticText -Object 'Preview: validation command will not be built or executed.' -Semantic Accent
        return
    }

    if (Read-GameWipYesNo -Prompt 'Run this command now?' -Default $true)
    {
        Assert-GameWipSelectedTargetRunnable -Action 'Validation command wizard'
        Invoke-GameWipMutation -Summary 'Run the composed validation command.' -Risk local -Plan @('Ensure the validation executable unless -SkipBuild is used.', 'Execute the composed correctness command.') -Body {
            Initialize-GameWipProjectCommandBuild -Command $command -NoBuild:$NoBuild
            Invoke-GameWipNative `
                -Name 'validation-wizard' `
                -FilePath $executable `
                -Arguments $arguments.ToArray() `
                -UseWorkspaceTemp `
                -InheritConsole:$manualTests
        } | Out-Null
    }
}
