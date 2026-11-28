# Validation, stress, and benchmark menus.

# ------------------------------------------------------------
# Validation menu
# ------------------------------------------------------------

function Show-GameWipValidationMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId validation

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'test'
            {
                $preset = Read-GameWipNamedChoice `
                    -Prompt 'CTest preset' `
                    -Choices (Get-GameWipVisiblePresetName -Kind test) `
                    -Default $CommandConfig.DefaultTestPreset

                if ($null -ne $preset)
                {
                    $buildDirectory = Get-GameWipRepositoryRelativePath -Path (Get-GameWipBuildDirectory -PresetName $preset)

                    Invoke-GameWipInteractiveOperation `
                        -Label "test-$preset" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Ensure and run CTest preset '$preset'." `
                            -Risk local `
                            -Plan @(
                            "Recreate '$buildDirectory' when -CleanBuild is active."
                            'Build missing prerequisites unless -SkipBuild is used.'
                            "Run CTest in '$buildDirectory'."
                        ) `
                            -Body {
                            Invoke-GameWipTestPreset `
                                -Name $preset `
                                -UseWorkspaceTemp `
                                -NoBuild:$NoBuild `
                                -Fresh:$Fresh
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'module'
            {
                $module = Read-GameWipNamedChoice `
                    -Prompt 'Validation module' `
                    -Choices (@('all') + @(Get-GameWipValidationModuleName)) `
                    -Default $CommandConfig.DefaultModule

                if ($null -ne $module)
                {
                    Invoke-GameWipInteractiveOperation `
                        -Label "module-$module" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Run validation module '$module'." `
                            -Risk local `
                            -Plan @(
                            'Ensure the validation executable unless -SkipBuild is used.'
                            'Execute the selected correctness module.'
                        ) `
                            -Body {
                            Invoke-GameWipValidationModule -Name $module -NoBuild:$NoBuild
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'stress'
            {
                $module = Read-GameWipNamedChoice `
                    -Prompt 'Stress module' `
                    -Choices (@('all') + @(Get-GameWipValidationModuleName)) `
                    -Default $CommandConfig.DefaultModule

                if ($null -ne $module)
                {
                    $runs = Read-GameWipIntegerValue -Prompt 'Run count' -Default ([int]$CommandConfig.DefaultStressCount)
                    $workers = Read-GameWipIntegerValue -Prompt 'Parallel workers' -Default ([int]$CommandConfig.DefaultStressParallel)

                    Invoke-GameWipInteractiveOperation `
                        -Label "stress-$module" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Stress validation module '$module'." `
                            -Risk local `
                            -Plan @(
                            'Ensure the validation executable unless -SkipBuild is used.'
                            "Run up to $runs validation processes with at most $workers workers."
                        ) `
                            -Body {
                            Invoke-GameWipStressModule `
                                -Name $module `
                                -RunCount $runs `
                                -MaxParallel $workers `
                                -NoBuild:$NoBuild
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'wizard'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'validation-wizard' `
                    -Body {
                    Invoke-GameWipValidationCommandWizard -NoBuild:$NoBuild
                } |
                    Out-Null
            }

            'bench'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'benchmark-standard' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Build/run standard benchmark profile.' `
                        -Risk local `
                        -Plan @(
                        'Ensure benchmark executable unless -SkipBuild is used.'
                        'Collect retained benchmark result.'
                    ) `
                        -Body {
                        Invoke-GameWipBenchmark `
                            -Mode run `
                            -ProfileId standard `
                            -RepeatCount 0 `
                            -MinimumTime '' `
                            -RequestedOutput '' `
                            -Format json `
                            -SkipBuild:$NoBuild
                    } |
                        Out-Null
                    } |
                        Out-Null
            }

            'menu-advanced-validation'
            {
                Show-GameWipAdvancedValidationMenu
            }

            'bundle'
            {
                $bundle = Read-GameWipNamedChoice `
                    -Prompt 'Bundle' `
                    -Choices @($CommandConfig.Bundles | ForEach-Object { $_.Id }) `
                    -Default quick

                if ($null -ne $bundle)
                {
                    Invoke-GameWipInteractiveOperation `
                        -Label "bundle-$bundle" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Run bundle '$bundle'." `
                            -Risk local `
                            -Plan @(
                            'Recreate declared preset trees when required by the bundle or -CleanBuild.'
                            'Execute the declared bundle steps in order.'
                        ) `
                            -Body {
                            Invoke-GameWipBundle -Id $bundle -NoBuild:$NoBuild -Fresh:$Fresh
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'cov'
            {
                $buildDirectory = Get-GameWipRepositoryRelativePath `
                    -Path (Get-GameWipBuildDirectory -PresetName coverage)

                Invoke-GameWipInteractiveOperation `
                    -Label 'cov' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run coverage validation from a clean build tree.' `
                        -Risk local `
                        -Plan @(
                        "Remove '$buildDirectory' completely."
                        'Configure/build coverage.'
                        'Run CTest.'
                        'Generate coverage target.'
                    ) `
                        -Body {
                        Invoke-GameWipConfigurePreset -Name coverage -Fresh
                        Invoke-GameWipBuildPreset -Name coverage
                        Invoke-GameWipTestPreset -Name coverage -UseWorkspaceTemp -NoBuild
                        Invoke-GameWipBuildTarget -Name coverage -Target coverage
                    } |
                        Out-Null
                    } |
                        Out-Null
            }

            'asan'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'asan' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run AddressSanitizer validation from a clean build tree.' `
                        -Risk local `
                        -Plan @(
                        "Remove '$(Get-GameWipRepositoryRelativePath -Path (Get-GameWipBuildDirectory -PresetName asan))' completely."
                        'Configure/build asan.'
                        'Run CTest.'
                    ) `
                        -Body {
                        Invoke-GameWipConfigurePreset -Name asan -Fresh
                        Invoke-GameWipBuildPreset -Name asan
                        Invoke-GameWipTestPreset -Name asan -UseWorkspaceTemp -NoBuild
                    } |
                        Out-Null
                    } |
                        Out-Null
            }

            'ubsan'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'ubsan' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run UndefinedBehaviorSanitizer validation from a clean build tree.' `
                        -Risk local `
                        -Plan @(
                        "Remove '$(Get-GameWipRepositoryRelativePath -Path (Get-GameWipBuildDirectory -PresetName ubsan))' completely."
                        'Configure/build ubsan.'
                        'Run CTest.'
                    ) `
                        -Body {
                        Invoke-GameWipConfigurePreset -Name ubsan -Fresh
                        Invoke-GameWipBuildPreset -Name ubsan
                        Invoke-GameWipTestPreset -Name ubsan -UseWorkspaceTemp -NoBuild
                    } |
                        Out-Null
                    } |
                        Out-Null
            }
        }
    }
}
