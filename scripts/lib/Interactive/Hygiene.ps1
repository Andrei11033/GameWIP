# ------------------------------------------------------------
# Hygiene checks
# ------------------------------------------------------------

function Show-GameWipHygieneMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId hygiene

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'hygiene-standard'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'hygiene-standard' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run the optional standard hygiene audit.' `
                        -Risk local `
                        -Plan @(
                        'Resolve the selected providers and ensure their local analysis state.'
                        'Run configured advisory checks.'
                        'Retain normalized evidence without editing tracked files.'
                    ) `
                        -Body {
                        Invoke-GameWipHygieneAudit -Selector standard
                    } |
                        Out-Null
                    } |
                        Out-Null
            }

            'hygiene-deep'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'hygiene-deep' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Run the optional deep hygiene audit.' `
                        -Risk local `
                        -Plan @(
                        'Resolve the selected providers and ensure their local analysis state.'
                        'Run available deep checks.'
                        'Report planned providers without claiming results.'
                    ) `
                        -Body {
                        Invoke-GameWipHygieneAudit -Selector deep
                    } |
                        Out-Null
                    } |
                        Out-Null
            }

            'hygiene-check'
            {
                $id = Read-GameWipNamedChoice `
                    -Prompt 'Hygiene check' `
                    -Choices @(
                    $HygieneConfig.Checks |
                        Where-Object Availability -eq available |
                        ForEach-Object { $_.Id }
                ) `
                    -Default unused-includes

                if ($null -ne $id)
                {
                    Invoke-GameWipInteractiveOperation `
                        -Label "hygiene-$id" `
                        -Body {
                        Invoke-GameWipMutation `
                            -Summary "Run optional hygiene check '$id'." `
                            -Risk local `
                            -Plan @(
                            'Ensure the analyze compilation database.'
                            'Run the selected advisory check.'
                            'Retain normalized evidence without editing tracked files.'
                        ) `
                            -Body {
                            Invoke-GameWipHygieneAudit -Selector $id
                        } |
                            Out-Null
                        } |
                            Out-Null
                }
            }

            'hygiene-list'
            {
                Show-GameWipHygieneList
            }

            'hygiene-status'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'hygiene-status' `
                    -Body {
                    Show-GameWipHygieneStatus
                } |
                    Out-Null
            }
        }
    }
}
