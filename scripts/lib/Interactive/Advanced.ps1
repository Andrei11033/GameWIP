# Advanced validation and support menus.

# ------------------------------------------------------------
# Advanced menus
# ------------------------------------------------------------

function Show-GameWipAdvancedValidationMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId advanced-validation

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'wizard'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'validation-wizard' `
                    -Body {
                    Invoke-GameWipValidationCommandWizard -NoBuild:$NoBuild
                } |
                    Out-Null
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
                $buildDirectory = Get-GameWipRepositoryRelativePath `
                    -Path (Get-GameWipBuildDirectory -PresetName asan)

                Invoke-GameWipInteractiveOperation `
                    -Label 'asan' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run AddressSanitizer validation from a clean build tree.' `
                        -Risk local `
                        -Plan @(
                        "Remove '$buildDirectory' completely."
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
                $buildDirectory = Get-GameWipRepositoryRelativePath `
                    -Path (Get-GameWipBuildDirectory -PresetName ubsan)

                Invoke-GameWipInteractiveOperation `
                    -Label 'ubsan' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run UndefinedBehaviorSanitizer validation from a clean build tree.' `
                        -Risk local `
                        -Plan @(
                        "Remove '$buildDirectory' completely."
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

function Show-GameWipAdvancedMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId advanced

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'menu-advanced-validation'
            {
                Show-GameWipAdvancedValidationMenu
            }

            'menu-hygiene'
            {
                Show-GameWipHygieneMenu
            }

            'menu-github-workflows'
            {
                Show-GameWipGitHubWorkflowsMenu
            }

            'menu-dependencies'
            {
                Show-GameWipDependencyMenu
            }

            'menu-unicode-data'
            {
                Show-GameWipUnicodeDataMenu
            }

            'menu-run-history'
            {
                Show-GameWipRunHistoryMenu
            }
        }
    }
}
