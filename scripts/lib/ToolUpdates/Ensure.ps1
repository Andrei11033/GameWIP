# ------------------------------------------------------------
# Tool ensure workflow
# ------------------------------------------------------------

function Invoke-GameWipToolEnsure
{
    param([string]$Selector = 'all', [switch]$ConsentAlreadyGranted)

    $selected = @(Get-GameWipProjectToolSelection -Selector $Selector)
    $skippedTools = [System.Collections.Generic.List[object]]::new()

    # Remove host providers and tools this host cannot install.
    if (-not (Test-GameWipWindowsHost))
    {
        $hostManaged = @($selected | Where-Object { $_.provider.kind -in @('msys2', 'winget') })

        foreach ($tool in $hostManaged)
        {
            $skippedTools.Add([pscustomobject]@{ Id = [string]$tool.id; Reason = 'Windows-only provider' }) | Out-Null
        }
        $selected = @($selected | Where-Object { $_.provider.kind -notin @('msys2', 'winget') })
    }

    $unsupportedHostTools = @($selected | Where-Object { -not (Test-GameWipToolHostSupport -Tool $_) })

    foreach ($tool in $unsupportedHostTools)
    {
        $skippedTools.Add([pscustomobject]@{
                Id = [string]$tool.id
                Reason = "no release asset for this host ($($tool.provider.hostPlatform))"
            }) | Out-Null
    }
    $selected = @($selected | Where-Object { Test-GameWipToolHostSupport -Tool $_ })

    foreach ($tool in @($selected | Where-Object { -not $_.capabilities.detectInstalled -or -not $_.capabilities.update }))
    {
        $skippedTools.Add([pscustomobject]@{ Id = [string]$tool.id; Reason = 'managed outside this command' }) | Out-Null
    }
    $selected = @($selected | Where-Object { $_.capabilities.detectInstalled -and $_.capabilities.update })
    $plan = [System.Collections.Generic.List[object]]::new()
    $selectedToolIds = @($selected | ForEach-Object { [string]$_.id })

    # Read current tool and dependency state before choosing actions.
    $toolchain = if ($selectedToolIds.Count -eq 0)
    {
        [pscustomobject]@{ Results = @() }
    }
    else
    {
        Test-GameWipToolchain `
            -ToolIds $selectedToolIds `
            -DisplayStatus
    }
    $statuses = @{}

    foreach ($status in @($toolchain.Results))
    {
        $statuses[[string]$status.Tool.id] = $status
    }

    foreach ($tool in $selected)
    {
        $toolStatus = $statuses[[string]$tool.id]
        $detected = $toolStatus.Detected
        $compatibility = $toolStatus.Compatibility
        $providerOk = Test-GameWipDetectedToolFromDeclaredProvider -Tool $tool -Detected $detected
        $providerRepairRequired = -not $providerOk -and
            $tool.provider.kind -in @('python', 'npm', 'powershellGallery', 'githubRelease')
        $action = if (-not $detected.Installed)
        {
            'install'
        }
        elseif ($compatibility -ne 'compatible')
        {
            'update'
        }
        elseif ($toolStatus.DependencyFailures.Count -ne 0 -or $providerRepairRequired)
        {
            'repair'
        }
        else
        {
            'ready'
        }
        $needsInstall = $action -ne 'ready'
        $plan.Add([pscustomobject]@{
                Tool = $tool
                Status = $toolStatus
                Detected = $detected
                Compatibility = $compatibility
                ProviderOk = $providerOk
                Action = $action
                NeedsInstall = $needsInstall
            }) | Out-Null

        if ($needsInstall)
        {
            Assert-GameWipPinnedToolAvailable -Tool $tool
        }
    }

    # Show each action and summarize tools that this command skips.
    Write-GameWipSection "Tool install plan: $Selector"

    foreach ($item in $plan)
    {
        $stateSemantic = if ($item.Action -eq 'ready')
        {
            'Success'
        }
        else
        {
            'Warning'
        }
        $location = if ($item.Detected.Location)
        {
            [string]$item.Detected.Location
        }
        else
        {
            '-'
        }

        Write-Host ('  {0,-20} ' -f $item.Tool.id) -NoNewline
        Write-GameWipSemanticText -Object ('{0,-11}' -f $item.Action) -Semantic $stateSemantic -NoNewline
        Write-Host ' ' -NoNewline
        Write-GameWipSemanticText -Object $location -Semantic Muted
    }

    if ($skippedTools.Count -ne 0)
    {
        foreach ($group in @($skippedTools | Group-Object Reason))
        {
            $toolIds = ($group.Group | ForEach-Object { $_.Id }) -join ', '
            Write-GameWipSemanticText -Object "Skipped: $toolIds ($($group.Name))" -Semantic Warning
        }
    }

    $needed = @($plan | Where-Object { $_.NeedsInstall })

    if ($needed.Count -eq 0)
    {
        $message = if ($skippedTools.Count -eq 0)
        {
            'All selected tools already satisfy the declared policy.'
        }
        else
        {
            'All installable selected tools already satisfy the declared policy.'
        }
        $semantic = 'Success'

        if ($skippedTools.Count -gt 0)
        {
            $semantic = 'Warning'
        }
        Write-GameWipSemanticText -Object $message -Semantic $semantic
        return
    }

    # Stop before mutation when previewing.
    if ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview)
    {
        return
    }

    $risk = if (Test-GameWipWindowsHost)
    {
        'machine'
    }
    else
    {
        'local'
    }

    $installPlan = foreach ($item in $needed)
    {
        if ($item.Tool.provider.kind -eq 'winget')
        {
            "$($item.Action) $($item.Tool.name) through WinGet package '$($item.Tool.provider.package)' outside build/"
        }
        else
        {
            "$($item.Action) $($item.Tool.id): declared $($item.Tool.versionPolicy) version"
        }
    }

    # Confirm the plan, then ask again for each external WinGet install.
    if (-not $ConsentAlreadyGranted -and -not (Confirm-GameWipMutation `
                -Summary "Install/update $($needed.Count) selected tool(s) without changing repository pins?" `
                -Risk $risk `
                -Plan @($installPlan)))
    {
        return
    }

    $approved = [System.Collections.Generic.List[object]]::new()

    foreach ($item in $needed)
    {
        if ($item.Action -eq 'install' -and
            $item.Tool.provider.kind -eq 'winget' -and
            -not (Confirm-GameWipExternalInstall `
                -Name ([string]$item.Tool.name) `
                -Provider WinGet `
                -Package ([string]$item.Tool.provider.package)))
        {
            $skippedTools.Add([pscustomobject]@{ Id = [string]$item.Tool.id; Reason = 'host installation declined' }) | Out-Null
            Add-GameWipOperationPreserved -Message "Left $($item.Tool.name) uninstalled after the WinGet prompt was declined."
            Write-GameWipSemanticText -Object "Skipped $($item.Tool.name); it remains missing." -Semantic Warning
            continue
        }

        $approved.Add($item) | Out-Null
    }
    $needed = @($approved)

    if ($needed.Count -eq 0)
    {
        if ($skippedTools.Count -ne 0)
        {
            Write-GameWipSemanticText -Object 'No approved tool changes; declined host installs remain missing.' -Semantic Warning
        }
        return
    }

    # Apply approved installations and verify each resulting tool state.
    Invoke-GameWipMutationBody -Body {
        Initialize-GameWipManagedToolRoot

        foreach ($item in $needed)
        {
            Assert-GameWipNotCancelled
            $tool = $item.Tool

            if (-not (Test-GameWipWindowsHost) -and $tool.provider.kind -in @('msys2', 'winget'))
            {
                throw (
                    "Tool '$($tool.id)' uses Windows provider '$($tool.provider.kind)' " +
                    'and must be provisioned by the runner image/package manager.'
                )
            }
            $version = if ($tool.Contains('requiredVersion'))
            {
                [string]$tool.requiredVersion
            }
            else
            {
                $null
            }
            $functionName = Get-GameWipProviderFunction -Tool $tool -Operation Install
            & $functionName -Tool $tool -Version $version
            $verifiedStatus = Assert-GameWipToolInstallVerified -Tool $tool
            $verified = $verifiedStatus.Detected
            $verb = switch ([string]$item.Action)
            {
                'install'
                {
                    'Installed'
                }
                'update'
                {
                    'Updated'
                }
                'repair'
                {
                    'Repaired'
                }
                default
                {
                    'Updated'
                }
            }
            Add-GameWipOperationChange -Message "$verb $($tool.id) at $($verified.Location)"
        }
    }
}
