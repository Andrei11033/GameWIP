# Tool and environment menus.
# ------------------------------------------------------------
# Tool updates and setup
# ------------------------------------------------------------

function Show-GameWipToolUpdatesMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId tool-updates

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'tool-plan'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'tool-plan' `
                    -Body {
                        Show-GameWipToolUpdatePlan -Plan @(Get-GameWipToolUpdatePlan -ToolId all)
                    } |
                    Out-Null
            }

            'tool-preview'
            {
                $id = Read-GameWipNamedChoice `
                    -Prompt 'Tool(s) to preview' `
                    -Choices @(
                        $ProjectTools.tools |
                            Where-Object { $_.capabilities.update } |
                            ForEach-Object { $_.id }
                    ) `
                    -Default all `
                    -AllowMultiple

                if ($null -ne $id)
                {
                    Invoke-GameWipInteractiveOperation `
                        -Label "tool-preview-$($id -join '-')" `
                        -Body {
                            Invoke-GameWipToolUpdate -ToolId $id -PreviewOnly
                        } |
                        Out-Null
                }
            }

            'tool-update'
            {
                $id = Read-GameWipNamedChoice `
                    -Prompt 'Tool(s) to update' `
                    -Choices @(
                        $ProjectTools.tools |
                            Where-Object { $_.capabilities.update } |
                            ForEach-Object { $_.id }
                    ) `
                    -Default all `
                    -AllowMultiple

                if ($null -ne $id)
                {
                    # One combined plan keeps earlier selected updates from making
                    # the tracked tree dirty before the next tool's preflight.
                    Invoke-GameWipInteractiveOperation `
                        -Label "tool-update-$($id -join '-')" `
                        -Body {
                            Invoke-GameWipToolUpdate -ToolId $id
                        } |
                        Out-Null
                }
            }
        }
    }
}

function Show-GameWipToolsMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId tools

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'ready'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'ready' `
                    -Body {
                        Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
                    } |
                    Out-Null
            }

            'tool-status'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'tool-status' `
                    -Body {
                        Show-GameWipToolStatus
                    } |
                    Out-Null
            }

            'menu-tool-updates'
            {
                Show-GameWipToolUpdatesMenu
            }

            'setup-guidance'
            {
                $setupCommand = Get-GameWipSetupHelperCommand
                Write-Host "Use '$setupCommand check' for environment status or '$setupCommand repair' for an idempotent repair."
            }
        }
    }
}
