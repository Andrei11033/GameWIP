# Linux uninstall orchestration.

# ------------------------------------------------------------
# Load ownership and inventory helpers
# ------------------------------------------------------------

Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Uninstall/Markers.ps1')
. (Join-Path $PSScriptRoot 'Uninstall/Inventory.ps1')

# ------------------------------------------------------------
# Linux uninstall
# ------------------------------------------------------------

function Invoke-GameWipUninstall
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$Preview
    )

    # Gather setup state and the verified Linux tool inventory.
    Write-GameWipSection "Uninstall GameWIP development environment$(if ($Preview) { ' (preview)' })"
    $state = Get-GameWipSetupState
    $stateNeedsRepair = $script:SetupStateReadStatus -in @('needs-repair', 'corrupt')
    $inventory = Get-GameWipLinuxUninstallInventory -RepositoryRoot $RepositoryRoot
    $distribution = Get-GameWipLinuxDistribution
    $manager = [string]$distribution.PackageManager
    $recordedSystemPackages = @(
        $state.systemPackages |
            Where-Object { $_.manager -eq $manager } |
            ForEach-Object { [string]$_.package } |
            Sort-Object -Unique
    )
    $installedSystemPackages = if ($recordedSystemPackages.Count -ne 0)
    {
        @(Get-GameWipLinuxInstalledPackages -Manager $manager -Packages $recordedSystemPackages)
    }
    else
    {
        @()
    }
    $installedSystemPackages = @($installedSystemPackages)
    $recorded = [System.Collections.Generic.List[string]]::new()
    $planned = [System.Collections.Generic.List[string]]::new()
    $preserved = [System.Collections.Generic.List[string]]::new()

    # Build the reviewable removal plan.
    $proven = [System.Collections.Generic.List[string]]::new()
    $proven.Add((Join-Path $RepositoryRoot 'build')) | Out-Null
    $proven.Add('GameWIP VS Code workflow extension and managed keybindings') | Out-Null
    $planned.Add('GameWIP VS Code workflow extension and managed keybinding block') | Out-Null
    $planned.AddRange([string[]]$inventory.Planned)
    $preserved.AddRange([string[]]$inventory.Preserved)

    foreach ($extension in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
    {
        $recorded.Add("VS Code extension $extension") | Out-Null
        $planned.Add("VS Code extension $extension") | Out-Null
    }

    foreach ($package in $installedSystemPackages)
    {
        $recorded.Add("$manager package $package (installed by GameWIP)") | Out-Null
        $planned.Add("$manager package $package") | Out-Null
    }

    foreach ($entry in @($state.systemPackages | Where-Object { $_.manager -ne $manager }))
    {
        $preserved.Add("$($entry.manager) package $($entry.package) (not managed by the current package manager)") | Out-Null
    }

    $preserved.Add('Pre-existing or unrecorded system packages and unrelated software') | Out-Null
    $planned.Add("$(Join-Path $RepositoryRoot 'build') (all build outputs, tools, caches, setup state, receipts, and diagnostics)") | Out-Null

    if ($stateNeedsRepair)
    {
        $planned.Add('Normalize recoverable setup ownership state before uninstall.') | Out-Null
        Add-GameWipOperationWarning -Message "Setup state requires repair: $script:SetupStateReadError"
    }

    $preserved.Add('Checkout source and configuration files') | Out-Null
    $preserved.Add('Existing installations and packages outside build/ that are not recorded as GameWIP-owned') | Out-Null

    # Show ownership and require approval before making changes.
    Write-GameWipUninstallSection -Title 'Proven GameWIP-owned' -Items (@($proven.ToArray()) + @($inventory.Proven))
    Write-GameWipUninstallSection -Title 'Recorded GameWIP-owned' -Items $recorded.ToArray()
    Write-GameWipUninstallSection -Title 'Ownership unknown' -Items $inventory.Unknown
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

    $approved = Confirm-GameWipMutation `
        -Summary 'Remove recorded GameWIP resources and the checkout build directory.' `
        -Risk destructive `
        -Plan $planned.ToArray()

    if (-not $approved)
    {
        Add-GameWipOperationPreserved -Message 'Uninstall was cancelled; no resources were removed.'
        return
    }

    Invoke-GameWipMutationBody -Body {
        # Normalize recoverable setup state before removing owned resources.
        if ($stateNeedsRepair)
        {
            $null = Repair-GameWipSetupState
            Add-GameWipOperationChange -Message 'Normalized recoverable setup ownership state.'
        }

        Invoke-GameWipEditorIntegrationRemoval

        if (Test-GameWipSetupCommand -Name code)
        {
            foreach ($extension in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
            {
                Invoke-GameWipSetupNative -FilePath code -ArgumentList @('--uninstall-extension', $extension) -AllowedExitCodes @(0, 1) | Out-Null
            }
        }

        # Remove marked tool data and packages recorded as GameWIP-installed.
        foreach ($path in $inventory.RemovePaths)
        {
            Invoke-GameWipLinuxOwnedTreeRemoval -Path $path -OwnedRoot $inventory.ManagedToolRoot
            Add-GameWipOperationChange -Message "Removed managed Linux tool data: $path"
        }

        if ($installedSystemPackages.Count -ne 0)
        {
            Remove-GameWipLinuxSystemPackages -Manager $manager -Packages $installedSystemPackages
            Add-GameWipOperationChange -Message "Removed GameWIP-added $manager packages: $($installedSystemPackages -join ', ')"
        }

        Add-GameWipOperationChange -Message 'Scheduled build/ removal after setup exits.'
        Add-GameWipOperationPreserved -Message 'Checkout source and unrecorded external resources were preserved.'
        Write-GameWipBuildUninstallRequest -RepositoryRoot $RepositoryRoot -LaunchToken ([string]$env:GAMEWIP_SETUP_LAUNCH_TOKEN)
    }
}
