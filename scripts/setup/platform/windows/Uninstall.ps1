# Uninstall inventory, ownership classification, consent, and conservative removal.

# ------------------------------------------------------------
# Uninstall inventory and ownership checks
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipMsys2Ownership
{
    $path = Join-Path (Get-GameWipMsys2Root) '.gamewip-managed.json'
    return Read-GameWipOwnershipMarker -Path $path -Resource 'msys2'
}

function Invoke-GameWipUninstall
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$Preview
    )

    Write-GameWipSection "Uninstall GameWIP development environment$(if ($Preview) { ' (preview)' })"
    $state = Get-GameWipSetupState
    $msysRoot = Get-GameWipMsys2Root
    $toolRoot = Get-GameWipManagedToolRoot
    $toolMarker = Get-GameWipManagedToolRootOwnership -Root $toolRoot
    $msysState = Get-GameWipMsys2Ownership

    $proven = [System.Collections.Generic.List[string]]::new()
    $recorded = [System.Collections.Generic.List[string]]::new()
    $unknown = [System.Collections.Generic.List[string]]::new()
    $planned = [System.Collections.Generic.List[string]]::new()
    $preserved = [System.Collections.Generic.List[string]]::new()

    $proven.Add('GameWIP VS Code workflow integration and managed keybinding block') | Out-Null
    $planned.Add('GameWIP VS Code workflow integration and managed keybinding block') | Out-Null

    if ($null -ne $toolMarker)
    {
        $proven.Add("$toolRoot ($($toolMarker.origin))") | Out-Null
        $planned.Add($toolRoot) | Out-Null
    }
    elseif (Test-Path -LiteralPath $toolRoot)
    {
        $unknown.Add("$toolRoot has no valid ownership marker") | Out-Null
        $preserved.Add("$toolRoot (ownership unknown)") | Out-Null
    }

    if ($msysState.Status -eq 'valid' -and $msysState.Marker.payload.installedBySetup -eq $true)
    {
        $proven.Add("$msysRoot installation marker") | Out-Null
    }
    elseif (Test-Path -LiteralPath $msysRoot)
    {
        $preserved.Add("$msysRoot (shared/user-modifiable environment; review manually)") | Out-Null
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

    foreach ($tree in @((Join-Path (Get-GameWipStorageAreaPath -Area cache) 'tracy'), 'build/dev', 'build/docs', 'disposable setup/editor state'))
    {
        $planned.Add($tree) | Out-Null
    }
    $preserved.Add('Repository and user-created files') | Out-Null
    $preserved.Add('Pre-existing or ownership-unknown applications') | Out-Null

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

    if (-not (Confirm-GameWipMutation -Summary 'Remove only the inventoried GameWIP-owned development resources.' -Risk destructive -Plan $planned.ToArray()))
    {
        Add-GameWipOperationPreserved -Message 'Uninstall was cancelled; no resources were removed.'
        return
    }

    Invoke-GameWipMutationBody -Body {
        Invoke-GameWipEditorIntegrationRemoval

        if (Test-GameWipSetupCommand -Name code)
        {
            foreach ($id in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
            {
                Invoke-GameWipSetupNative -FilePath code -ArgumentList @('--uninstall-extension', $id) -AllowedExitCodes @(0, 1) | Out-Null
            }
        }

        if ($null -ne $toolMarker)
        {
            $toolRootParent = Split-Path -Parent $toolRoot
            Invoke-GameWipOwnedTreeRemoval -Path $toolRoot -OwnedRoot $toolRootParent -RequireMarker -MarkerName '.gamewip-managed.json'
            Add-GameWipOperationChange -Message "Removed managed tool root: $toolRoot"
        }
        $cacheRoot = Get-GameWipStorageAreaPath -Area cache
        $tracy = Join-Path $cacheRoot 'tracy'

        if (Test-Path -LiteralPath $tracy)
        {
            Invoke-GameWipOwnedTreeRemoval -Path $tracy -OwnedRoot $cacheRoot
        }
        $buildRoot = Join-Path $RepositoryRoot 'build'

        foreach ($tree in @('dev', 'docs'))
        {
            $path = Join-Path $buildRoot $tree

            if (Test-Path -LiteralPath $path)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $path -OwnedRoot $buildRoot
            }
        }

        foreach ($id in @($state.wingetPackages | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
        {
            Uninstall-GameWipWingetPackage -Id $id
        }

        if ($msysState.Status -eq 'valid' -and $msysState.Marker.payload.installedBySetup -eq $true -and (Test-Path -LiteralPath $msysRoot))
        {
            Add-GameWipOperationWarning -Message "MSYS2 was installed by GameWIP but may now contain user data. Review '$msysRoot' manually before removing it."
        }

        foreach ($path in @($script:SetupStatePath, (Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot)))
        {
            if (Test-Path -LiteralPath $path -PathType Leaf)
            {
                Set-GameWipMutationStarted
                Remove-Item -LiteralPath $path -Force
            }
        }
        Add-GameWipOperationChange -Message 'Removed setup-owned integrations, packages, and disposable setup state.'
        Add-GameWipOperationPreserved -Message 'Repository, unknown resources, and MSYS2 user data were preserved.'
    }
}
