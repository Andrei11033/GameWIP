# ------------------------------------------------------------
# Install planning and verification
# ------------------------------------------------------------

function Get-GameWipPlannedInstallTool
{
    param([Parameter(Mandatory = $true)]$PlanItem, [Parameter(Mandatory = $true)]$TrackedPlan)

    $tool = ConvertTo-GameWipHashtable -Value $PlanItem.Tool
    $registryTool = @($TrackedPlan.Registry.tools | Where-Object { $_.id -eq $tool.id } | Select-Object -First 1)

    if ($registryTool.Count -eq 1)
    {
        return ConvertTo-GameWipHashtable -Value $registryTool[0]
    }

    return $tool
}

function Test-GameWipToolPlanRequiresInstall
{
    param([Parameter(Mandatory = $true)]$PlanItem, [Parameter(Mandatory = $true)]$TrackedPlan)

    $toolId = [string]$PlanItem.Tool.id

    if ($TrackedPlan.UpdatedToolIds.ContainsKey($toolId))
    {
        return $true
    }

    $installTool = Get-GameWipPlannedInstallTool -PlanItem $PlanItem -TrackedPlan $TrackedPlan
    $currentStatus = Get-GameWipToolStatus -Tool $installTool

    if (-not $currentStatus.Ready)
    {
        return $true
    }

    $providerOk = Test-GameWipDetectedToolFromDeclaredProvider -Tool $installTool -Detected $currentStatus.Detected
    $reuseExternal = $installTool.capabilities.Contains('reuseCompatibleExternal') -and
    [bool]$installTool.capabilities.reuseCompatibleExternal

    if (-not $providerOk -and -not $reuseExternal)
    {
        return $true
    }

    return (
        $PlanItem.Query.State -eq 'resolved' -and
        -not [string]::IsNullOrWhiteSpace([string]$PlanItem.Latest) -and
        [string]$PlanItem.Installed -ne [string]$PlanItem.Latest
    )
}

function Assert-GameWipToolInstallVerified
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $status = Get-GameWipToolStatus -Tool $Tool
    $detected = $status.Detected
    $providerOk = Test-GameWipDetectedToolFromDeclaredProvider -Tool $Tool -Detected $detected

    if ($status.Ready -and $providerOk)
    {
        return $status
    }

    $details = [System.Collections.Generic.List[string]]::new()

    foreach ($dependency in @($status.DependencyFailures))
    {
        $reason = if ($dependency.Reason)
        {
            " - $($dependency.Reason)"
        }
        else
        {
            ''
        }
        $details.Add("$($dependency.Package): $($dependency.State)$reason") | Out-Null
    }

    if ($status.Error)
    {
        $details.Add([string]$status.Error) | Out-Null
    }

    if (-not $providerOk)
    {
        $details.Add('tool was not found under its declared provider') | Out-Null
    }

    if ($details.Count -eq 0)
    {
        $details.Add("compatibility: $($status.Compatibility)") | Out-Null
    }

    throw (
        "Tool '$($Tool.id)' failed post-install verification " +
        "(state=$($status.State), location=$($detected.Location), details=$($details -join '; '))."
    )
}

function Invoke-GameWipToolInstallPlan
{
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][object[]]$InstallPlan,
        [Parameter(Mandatory = $true)]$TrackedPlan
    )

    if ($InstallPlan.Count -eq 0)
    {
        return
    }

    Initialize-GameWipManagedToolRoot

    # Install each planned tool and verify its declared provider afterward.
    foreach ($item in $InstallPlan)
    {
        Assert-GameWipNotCancelled
        $installTool = Get-GameWipPlannedInstallTool -PlanItem $item -TrackedPlan $TrackedPlan
        $version = if ($installTool.Contains('requiredVersion'))
        {
            [string]$installTool.requiredVersion
        }
        else
        {
            $null
        }
        Write-GameWipOperationEvent `
            -Phase execute `
            -Step $item.Tool.id `
            -Severity progress `
            -Message "Installing/verifying $($item.Tool.name) $(if ($version) { $version } else { '' })..."
        $functionName = Get-GameWipProviderFunction -Tool $installTool -Operation Install
        & $functionName -Tool $installTool -Version $version
        $verifiedStatus = Assert-GameWipToolInstallVerified -Tool $installTool
        $verified = $verifiedStatus.Detected

        Add-GameWipOperationChange -Message "Installed/verified $($installTool.id) at $($verified.Location)"
    }
}
