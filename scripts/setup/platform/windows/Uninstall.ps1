# Uninstall inventory, ownership classification, consent, and conservative removal.

# ------------------------------------------------------------
# Uninstall inventory and ownership checks
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Invoke-GameWipUninstall
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$Preview
    )

    Write-GameWipSection "Uninstall GameWIP development environment$(if ($Preview) { ' (preview)' })"
    $state = Get-GameWipSetupState
    $stateNeedsRepair = $script:SetupStateReadStatus -in @('needs-repair', 'corrupt')
    $msysRoot = Get-GameWipMsys2Root
    $toolRoot = Get-GameWipManagedToolRoot
    $buildRoot = Join-Path $RepositoryRoot 'build'
    $toolMarker = Get-GameWipManagedToolRootOwnership -Root $toolRoot

    $proven = [System.Collections.Generic.List[string]]::new()
    $recorded = [System.Collections.Generic.List[string]]::new()
    $unknown = [System.Collections.Generic.List[string]]::new()
    $planned = [System.Collections.Generic.List[string]]::new()
    $preserved = [System.Collections.Generic.List[string]]::new()

    $proven.Add("$buildRoot (project-owned build directory)") | Out-Null
    $proven.Add('GameWIP VS Code workflow integration and managed keybindings') | Out-Null
    $planned.Add('GameWIP VS Code workflow integration and managed keybindings') | Out-Null
    $planned.Add("$buildRoot (all build outputs, tools, caches, setup state, receipts, and diagnostics)") | Out-Null

    if ($null -ne $toolMarker)
    {
        if (Test-GameWipPathWithinRoot -Path $toolRoot -Root $buildRoot)
        {
            $proven.Add("$toolRoot (removed with build/)") | Out-Null
        }
        else
        {
            $proven.Add("$toolRoot ($($toolMarker.origin))") | Out-Null
            $planned.Add($toolRoot) | Out-Null
        }
    }
    elseif (Test-Path -LiteralPath $toolRoot)
    {
        if (Test-GameWipPathWithinRoot -Path $toolRoot -Root $buildRoot)
        {
            $proven.Add("$toolRoot (removed with build/)") | Out-Null
        }
        else
        {
            $unknown.Add("$toolRoot has no valid ownership marker") | Out-Null
            $preserved.Add("$toolRoot (ownership unknown)") | Out-Null
        }
    }

    if (Test-GameWipPathWithinRoot -Path $msysRoot -Root $buildRoot)
    {
        if (Test-Path -LiteralPath $msysRoot)
        {
            $proven.Add("$msysRoot (removed with build/)") | Out-Null
        }
    }
    elseif (Test-Path -LiteralPath $msysRoot)
    {
        $preserved.Add("$msysRoot (existing installation outside build/)") | Out-Null
    }

    foreach ($id in @($state.wingetPackages | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
    {
        $recorded.Add("WinGet package $id") | Out-Null
        $planned.Add("WinGet package $id") | Out-Null
    }

    foreach ($id in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
    {
        $recorded.Add("VS Code extension $id") | Out-Null
        $planned.Add("VS Code extension $id") | Out-Null
    }

    if ($stateNeedsRepair)
    {
        $planned.Add('Normalize recoverable setup ownership state before uninstall.') | Out-Null
    }
    $preserved.Add('Checkout source and configuration files') | Out-Null
    $preserved.Add('Existing installations and packages outside build/ that are not recorded as GameWIP-owned') | Out-Null

    if ($stateNeedsRepair)
    {
        Add-GameWipOperationWarning -Message "Setup state requires repair: $script:SetupStateReadError"
    }

    Write-GameWipUninstallSection -Title 'Proven GameWIP-owned' -Items $proven.ToArray()
    Write-GameWipUninstallSection -Title 'Recorded GameWIP-owned' -Items $recorded.ToArray()
    Write-GameWipUninstallSection -Title 'Ownership unknown' -Items $unknown.ToArray()
    Write-GameWipUninstallSection -Title 'Planned automatic removals' -Items $planned.ToArray()
    Write-GameWipUninstallSection -Title 'Preserved resources' -Items $preserved.ToArray()

    if ($Preview -or ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview))
    {
        Write-Host ''
        Write-Host 'Preview complete; no resources were changed.'
        return
    }

    Assert-GameWipBuildUninstallLauncher `
        -RepositoryRoot $RepositoryRoot `
        -LaunchToken ([string]$env:GAMEWIP_SETUP_LAUNCH_TOKEN)

    if (-not (Confirm-GameWipMutation -Summary 'Remove recorded GameWIP resources and the checkout build directory.' -Risk destructive -Plan $planned.ToArray()))
    {
        Add-GameWipOperationPreserved -Message 'Uninstall was cancelled; no resources were removed.'
        return
    }

    Invoke-GameWipMutationBody -Body {
        if ($stateNeedsRepair)
        {
            $null = Repair-GameWipSetupState
            Add-GameWipOperationChange -Message 'Normalized recoverable setup ownership state.'
        }

        Invoke-GameWipEditorIntegrationRemoval

        if (Test-GameWipSetupCommand -Name code)
        {
            foreach ($id in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
            {
                Invoke-GameWipSetupNative -FilePath code -ArgumentList @('--uninstall-extension', $id) -AllowedExitCodes @(0, 1) | Out-Null
            }
        }

        if ($null -ne $toolMarker -and -not (Test-GameWipPathWithinRoot -Path $toolRoot -Root $buildRoot))
        {
            $toolRootParent = Split-Path -Parent $toolRoot
            Invoke-GameWipOwnedTreeRemoval -Path $toolRoot -OwnedRoot $toolRootParent -RequireMarker -MarkerName '.gamewip-managed.json'
            Add-GameWipOperationChange -Message "Removed managed tool root: $toolRoot"
        }
        foreach ($id in @($state.wingetPackages | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
        {
            Uninstall-GameWipWingetPackage -Id $id
        }

        Add-GameWipOperationChange -Message 'Scheduled build/ removal after setup exits.'
        Add-GameWipOperationPreserved -Message 'Checkout source and unrecorded external resources were preserved.'
        Write-GameWipBuildUninstallRequest -RepositoryRoot $RepositoryRoot -LaunchToken ([string]$env:GAMEWIP_SETUP_LAUNCH_TOKEN)
    }
}
