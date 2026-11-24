# ------------------------------------------------------------
# Quality checks
# ------------------------------------------------------------

function Show-GameWipQualityMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId quality

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'quality-check'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'quality-check' `
                    -Body {
                        Invoke-GameWipQuality -Mode check
                    } |
                    Out-Null
            }

            'quality-fix'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'quality-fix' `
                    -Body {
                        Invoke-GameWipMutation `
                            -Summary 'Validate quality tools, apply deterministic formatters, and normalize LF line endings.' `
                            -Risk tracked `
                            -Plan @(
                                'Validate every tool required by the quality workflow before mutation.'
                                'Format maintained sources/configuration and normalize maintained text to LF.'
                                'Run the complete quality gate.'
                            ) `
                            -Body {
                                Invoke-GameWipQuality -Mode fix
                            } |
                            Out-Null
                    } |
                    Out-Null
            }

            'quality-status'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'quality-status' `
                    -Body {
                        Show-GameWipQualityCoverageStatus
                    } |
                    Out-Null
            }

            'format-check'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'format-check' `
                    -Body {
                        Invoke-GameWipFormat -Mode check
                    } |
                    Out-Null
            }

            'format-apply'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'format-apply' `
                    -Body {
                        Invoke-GameWipMutation `
                            -Summary 'Apply C/C++ formatting.' `
                            -Risk tracked `
                            -Plan @('Rewrite maintained C/C++ files with repository clang-format.') `
                            -Body {
                                Invoke-GameWipFormat -Mode apply
                            } |
                            Out-Null
                    } |
                    Out-Null
            }

            'analyze'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'analyze' `
                    -Body {
                        Invoke-GameWipMutation `
                            -Summary 'Run C++ static analysis.' `
                            -Risk local `
                            -Plan @(
                                'Configure analyze preset.'
                                'Build analyze preset.'
                            ) `
                            -Body {
                                Invoke-GameWipConfigurePreset -Name analyze
                                Invoke-GameWipBuildPreset -Name analyze
                            } |
                            Out-Null
                    } |
                    Out-Null
            }

            'links'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'links' `
                    -Body {
                        Invoke-GameWipMarkdownLink
                    } |
                    Out-Null
            }
        }
    }
}
