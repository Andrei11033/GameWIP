# ------------------------------------------------------------
# Repository and data menus
# ------------------------------------------------------------

function Show-GameWipGitWorkspaceMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId git-workspace

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'git-status'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-status' `
                    -Body {
                    Invoke-GameWipGitAction -Name status
                } |
                    Out-Null
            }

            'git-log'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-log' `
                    -Body {
                    Invoke-GameWipGitAction -Name log
                } |
                    Out-Null
            }

            'git-fetch'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-fetch' `
                    -Body {
                    Invoke-GameWipGitAction -Name fetch
                } |
                    Out-Null
            }

            'git-switch'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-switch' `
                    -Body {
                    Invoke-GameWipGitAction -Name switch
                } |
                    Out-Null
            }

            'git-create'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-create' `
                    -Body {
                    Invoke-GameWipGitAction -Name create
                } |
                    Out-Null
            }

            'git-update'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-update' `
                    -Body {
                    Invoke-GameWipGitAction -Name update
                } |
                    Out-Null
            }

            'git-push'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-push' `
                    -Body {
                    Invoke-GameWipGitAction -Name push
                } |
                    Out-Null
            }

            'git-cleanup'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'git-cleanup' `
                    -Body {
                    Invoke-GameWipGitAction -Name cleanup
                } |
                    Out-Null
            }
        }
    }
}

function Show-GameWipGitHubWorkflowsMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId github-workflows

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'workflow-list'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'workflow-list' `
                    -Body {
                    Invoke-GameWipWorkflowAction -Name list
                } |
                    Out-Null
            }

            'workflow-status'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'workflow-status' `
                    -Body {
                    Invoke-GameWipWorkflowAction -Name status
                } |
                    Out-Null
            }

            'workflow-run'
            {
                $workflow = Read-GameWipNamedChoice `
                    -Prompt 'Workflow' `
                    -Choices @($CommandConfig.ManualWorkflows | ForEach-Object { $_.Id }) `
                    -Default validation

                if ($null -ne $workflow)
                {
                    Invoke-GameWipInteractiveOperation `
                        -Label "workflow-$workflow" `
                        -Body {
                        Invoke-GameWipWorkflowAction -Name run -WorkflowId $workflow
                    } |
                        Out-Null
                }
            }
        }
    }
}

function Show-GameWipUnicodeDataMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId unicode-data

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'unicode-status'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'unicode-status' `
                    -Body {
                    Show-GameWipUnicodeStatus
                } |
                    Out-Null
            }

            'unicode-verify'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'unicode-verify' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Verify reproducible Unicode generated data.' `
                        -Risk local `
                        -Plan @(
                        'Verify/download pinned Unicode input in the owned cache.'
                        'Generate and format an operation-owned candidate.'
                        'Compare the candidate with the checked-in table.'
                    ) `
                        -Body {
                        Invoke-GameWipUnicodeVerify
                    } |
                        Out-Null
                    } |
                        Out-Null
            }

            'unicode-regenerate'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'unicode-regenerate' `
                    -Body {
                    Invoke-GameWipMutation `
                        -Summary 'Regenerate the tracked Unicode property table.' `
                        -Risk tracked `
                        -Plan @(
                        'Download/verify pinned UCD inputs.'
                        'Generate and format candidate.'
                        'Replace tracked table only when content differs.'
                    ) `
                        -Body {
                        Invoke-GameWipUnicodeRegenerate
                    } |
                        Out-Null
                    } |
                        Out-Null
            }
        }
    }
}

function Show-GameWipRunHistoryMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId run-history

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'history-list'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'history-list' `
                    -Body {
                    Show-GameWipRunList
                } |
                    Out-Null
            }

            'history-show'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'history-show-latest' `
                    -Body {
                    Show-GameWipRun -Selector latest
                } |
                    Out-Null
            }

            'history-clean'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'history-clean' `
                    -Body {
                    Invoke-GameWipRunCleanup -Selector all
                } |
                    Out-Null
            }
        }
    }
}

function Show-GameWipDependencyMenu
{
    while ($true)
    {
        $choice = Read-GameWipConfiguredMenuItem -MenuId dependencies

        if ($choice.Status -eq 'Cancelled')
        {
            return
        }

        switch ([string]$choice.Value)
        {
            'deps-check'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'deps-check' `
                    -Body {
                    Test-GameWipDependencyCache -ThrowOnFailure | Out-Null
                } |
                    Out-Null
            }

            'deps-prepare'
            {
                Invoke-GameWipInteractiveOperation `
                    -Label 'deps-prepare' `
                    -Body {
                    Invoke-GameWipDependencyPreparationOperation
                } |
                    Out-Null
            }
        }
    }
}
