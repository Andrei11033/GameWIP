# ------------------------------------------------------------
# Setup operations
# ------------------------------------------------------------

function Invoke-GameWipCompleteSetup
{
    param(
        [switch]$Update,
        [switch]$RefreshMsys2
    )

    # Ensure the editor selection exists before preparing integrations.
    $preference = Get-GameWipEditorPreferencePath

    if (-not (Test-Path -LiteralPath $preference))
    {
        if ($NonInteractive)
        {
            Save-GameWipEditorSelection -Editors @($EditorConfig.Default)
        }
        elseif (-not (Select-GameWipEditor -EditorConfig $EditorConfig))
        {
            throw 'Complete setup was cancelled before editor selection.'
        }
    }

    # Prepare the host toolchain and project tools.
    if (Test-Path -LiteralPath (Get-GameWipManagedToolRoot))
    {
        Initialize-GameWipSetupManagedToolRoot
    }
    if (Test-GameWipWindowsHost)
    {
        Invoke-GameWipSetupMsys2Step -Update:$RefreshMsys2
    }
    else
    {
        Invoke-GameWipSetupLinuxPrerequisites
    }

    # Prepare the checkout, dependencies, and editor integration.
    Invoke-GameWipSetupToolStep
    Invoke-GameWipSetupRepositoryStep -Update:$Update
    Invoke-GameWipSetupDependencyStep
    Invoke-GameWipSetupEditorStep -Update:$Update

    # Build the host profiler and documentation tools, then verify readiness.
    Invoke-GameWipSetupTracyStep -PrerequisitesReady

    if (-not $SkipDocs)
    {
        Invoke-GameWipSetupDocumentationStep
    }
    Invoke-GameWipSetupEnvironmentCheck
}

function Invoke-GameWipSetupActionBody
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    switch ($SelectedAction)
    {
        'full'
        {
            Invoke-GameWipCompleteSetup -RefreshMsys2
        }
        'repair'
        {
            Invoke-GameWipCompleteSetup
        }
        'update'
        {
            Invoke-GameWipCompleteSetup -Update -RefreshMsys2
        }
        'deps'
        {
            Invoke-GameWipSetupDependencyStep
        }
        'check'
        {
            Invoke-GameWipSetupEnvironmentCheck
        }
        'tool'
        {
            Invoke-GameWipSetupToolStep
        }
        'vs'
        {
            Invoke-GameWipSetupVisualStudioStep
        }
        'msys2'
        {
            Invoke-GameWipSetupMsys2Step
        }
        'system'
        {
            Invoke-GameWipSetupLinuxPrerequisites
        }
        'repo'
        {
            Invoke-GameWipSetupRepositoryStep
        }
        'profiler'
        {
            Invoke-GameWipSetupTracyStep
        }
        'editor'
        {
            Invoke-GameWipSetupEditorStep -Choose:(-not $NonInteractive)
        }
        'doc'
        {
            Invoke-GameWipSetupDocumentationStep -Open:(-not $NonInteractive)
        }
        'uninstall'
        {
            Invoke-GameWipUninstall -RepositoryRoot $RepositoryRoot -Preview:$Preview
        }
        'list'
        {
            Show-GameWipSetupActionCatalog
        }
        'help'
        {
            Show-GameWipSetupHelp
        }
        default
        {
            throw "Setup action '$SelectedAction' has no implementation."
        }
    }
}

function Invoke-GameWipSetupOperation
{
    param([Parameter(Mandatory = $true)][string]$SelectedAction)

    # Validate the action before opening its operation record.
    Assert-GameWipSetupHost
    Assert-GameWipSetupRepository -RepositoryRoot $RepositoryRoot
    Assert-GameWipSetupActionCatalog
    $actionInfo = Get-GameWipSetupAction -Id $SelectedAction

    return Invoke-GameWipOperation `
        -Label "setup-$SelectedAction" `
        -NonInteractive:$NonInteractive `
        -Yes:$Yes `
        -Preview:$Preview `
        -OutputMode $OutputMode `
        -NoColor:$NoColor `
        -SuppressReceipt:$Quiet `
        -SuppressOutput:$Quiet `
        -ScriptBlock {
        # Run read-only actions without creating a mutation plan.
        if ($SelectedAction -in @('help', 'list', 'check'))
        {
            Invoke-GameWipSetupActionBody -SelectedAction $SelectedAction
            return
        }

        if ($SelectedAction -eq 'uninstall')
        {
            # Uninstall owns its confirmation so inventory is always printed first.
            Invoke-GameWipSetupActionBody -SelectedAction uninstall
            return
        }

        # Gather the selected action's approval plan.
        if ($SelectedAction -in @('full', 'repair', 'update'))
        {
            Show-GameWipSetupSizeEstimate
        }

        if ($SelectedAction -in @('full', 'repair', 'update', 'msys2', 'tool'))
        {
            Select-GameWipExistingMsys2
        }

        $null = Get-GameWipSetupState
        $setupStateNeedsRepair = $script:SetupStateReadStatus -in @('needs-repair', 'corrupt')
        $plan = @(Get-GameWipSetupPlan -SelectedAction $SelectedAction)

        if ($setupStateNeedsRepair)
        {
            $plan += 'Update the disposable setup ownership state in build/gamewip.'
            Add-GameWipOperationWarning -Message "Setup state requires repair: $script:SetupStateReadError"
        }

        # Apply the plan and repair disposable state before the requested work.
        Invoke-GameWipMutation -Summary $actionInfo.Description -Risk ([string]$actionInfo.Risk) -Plan $plan -Body {
            if ($setupStateNeedsRepair)
            {
                $null = Repair-GameWipSetupState
                Add-GameWipOperationChange -Message 'Normalized setup ownership state.'
            }

            Invoke-GameWipSetupActionBody -SelectedAction $SelectedAction
        } | Out-Null
    }
}
