# Development menus and the runtime command builder.

# ------------------------------------------------------------
# Development menu
# ------------------------------------------------------------

function Invoke-GameWipRuntimeCommandWizard
{
    param([switch]$NoBuild)

    Write-GameWipSection 'Game runtime command builder'

    $gameCommands = @($CommandConfig.ProjectCommands | Where-Object { $_.ContainsKey('RuntimeOptions') -and [bool]$_.RuntimeOptions })

    if ($gameCommands.Count -eq 0)
    {
        throw 'No game executable is registered for the runtime command builder.'
    }

    $id = Read-GameWipNamedChoice `
        -Prompt 'Game executable' `
        -Choices @($gameCommands | ForEach-Object { $_.Id }) `
        -Default ([string]$CommandConfig.DefaultRuntimeCommand)

    if ($null -ne $id)
    {
        Invoke-GameWipRuntimeCommandBuilder -Id $id -NoBuild:$NoBuild
    }
}

function Invoke-GameWipRuntimeCommandBuilder
{
    param([Parameter(Mandatory = $true)][string]$Id, [switch]$NoBuild)

    $command = Get-GameWipProjectCommand -Id $Id

    if (-not $command.ContainsKey('RuntimeOptions') -or -not [bool]$command.RuntimeOptions)
    {
        throw "Project command '$Id' does not accept game runtime options."
    }

    # Choose whether to reuse or rebuild the selected executable.
    $buildBeforeRun = $false

    if (-not $NoBuild)
    {
        $buildBeforeRun = Read-GameWipYesNo -Prompt 'Always configure and build before running?' -Default $true
    }

    # Collect the selected runtime options from the command catalog.
    $target = Get-GameWipSelectedBuildTarget
    $options = @(Get-GameWipCommandBuilderOptions `
            -Options @($CommandConfig.RuntimeBuilderOptions) `
            -Platform ([string]$target.platform))
    $runtimeArguments = [System.Collections.Generic.List[string]]::new()

    foreach ($option in $options)
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

        foreach ($argument in @(ConvertTo-GameWipCommandBuilderArguments -Option $option -Value $value))
        {
            $runtimeArguments.Add([string]$argument) | Out-Null
        }
    }

    # Add extra executable arguments after the declared runtime options.
    $extraText = Read-GameWipTextValue -Prompt 'Additional game arguments, space-separated' -Default ''
    $extraArguments = @(Split-GameWipExtraArgument -Text $extraText)
    $arguments = @($command.Arguments) + $runtimeArguments.ToArray() + $extraArguments
    $executable = Resolve-GameWipProjectExecutable -Command $command

    Write-GameWipSection 'Game command'
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath $executable -Arguments $arguments)

    $plan = if ($NoBuild)
    {
        @('Run the existing executable without building.', 'Run the composed game command.')
    }
    elseif ($buildBeforeRun)
    {
        @("Configure and build preset '$($command.BuildPreset)' before execution.", 'Run the composed game command.')
    }
    else
    {
        @('Build the executable only if it is missing.', 'Run the composed game command.')
    }

    if (-not $Preview -and -not (Read-GameWipYesNo -Prompt 'Run this command now?' -Default $true))
    {
        return
    }

    Invoke-GameWipMutation `
        -Summary "Run game command '$Id'." `
        -Risk local `
        -Plan $plan `
        -Body {
        Invoke-GameWipProjectCommand `
            -Id $Id `
            -Arguments (@($runtimeArguments.ToArray()) + $extraArguments) `
            -NoBuild:$NoBuild `
            -ForceBuild:$buildBeforeRun
    } | Out-Null
}

function Show-GameWipDevelopmentMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId development

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'target'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'target' `
                    -Body {
                    Invoke-GameWipBuildTargetSelection -NonInteractive:$NonInteractive
                } |
                    Out-Null
            }

            'config'
            {
                $preset = Read-GameWipNamedChoice `
                    -Prompt 'Configure preset' `
                    -Choices (Get-GameWipVisiblePresetName -Kind configure) `
                    -Default $CommandConfig.DefaultConfigurePreset

                if ($null -ne $preset)
                {
                    $buildDirectory = Get-GameWipRepositoryRelativePath -Path (Get-GameWipBuildDirectory -PresetName $preset)

                    Invoke-GameWipInteractiveOperation `
                        -Label "configure-$preset" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Configure preset '$preset'." `
                            -Risk local `
                            -Plan @(
                            "Recreate '$buildDirectory' when -CleanBuild is active."
                            "Configure preset '$preset' in '$buildDirectory'."
                        ) `
                            -Body {
                            Invoke-GameWipConfigurePreset -Name $preset -Fresh:$Fresh
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'build'
            {
                $preset = Read-GameWipNamedChoice `
                    -Prompt 'Build preset' `
                    -Choices (Get-GameWipVisiblePresetName -Kind build) `
                    -Default $CommandConfig.DefaultBuildPreset

                if ($null -ne $preset)
                {
                    $buildDirectory = Get-GameWipRepositoryRelativePath -Path (Get-GameWipBuildDirectory -PresetName $preset)

                    Invoke-GameWipInteractiveOperation `
                        -Label "build-$preset" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Build preset '$preset'." `
                            -Risk local `
                            -Plan @(
                            "Recreate '$buildDirectory' when -CleanBuild is active."
                            'Ensure configuration if missing.'
                            "Build '$buildDirectory'."
                        ) `
                            -Body {
                            Invoke-GameWipBuildPreset -Name $preset -Fresh:$Fresh
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'run'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'runtime' `
                    -Body {
                    Invoke-GameWipRuntimeCommandWizard -NoBuild:$NoBuild
                } |
                    Out-Null
            }

            'doc'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'doc' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Build generated documentation.' `
                        -Risk local `
                        -Plan @(
                        'Configure docs preset.'
                        'Build Doxygen documentation.'
                    ) `
                        -Body {
                        Invoke-GameWipConfigurePreset -Name docs
                        Invoke-GameWipBuildTarget -Name docs -Target docs
                    } |
                        Out-Null
                    } |
                        Out-Null
            }
        }
    }
}
