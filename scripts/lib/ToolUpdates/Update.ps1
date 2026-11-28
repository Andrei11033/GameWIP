# ------------------------------------------------------------
# Tool update workflow
# ------------------------------------------------------------

function Invoke-GameWipToolUpdate
{
    param([string[]]$ToolId, [switch]$PreviewOnly)

    if ($null -eq $ToolId -or $ToolId.Count -eq 0 -or ($ToolId.Count -eq 1 -and [string]::IsNullOrWhiteSpace($ToolId[0])))
    {
        $ToolId = @('all')
    }

    # Resolve current versions and the tracked-file changes they imply.
    Write-GameWipOperationEvent `
        -Phase discover `
        -Severity info `
        -Message "Resolving project-tool state for '$($ToolId -join ', ')'..."
    $plan = @(Get-GameWipToolUpdatePlan -ToolId $ToolId)
    $trackedPlan = Get-GameWipTrackedToolMutationPlan -Plan $plan

    # Show the complete update plan before any installation or file write.
    $previewLabel = if ($PreviewOnly -or ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview))
    {
        ' (preview)'
    }
    else
    {
        ''
    }
    Write-GameWipSection "Tool update plan$previewLabel"
    Show-GameWipToolUpdatePlan -Plan $plan

    if ($trackedPlan.Descriptions.Count -ne 0)
    {
        Write-Host '  Tracked mutations:'

        foreach ($description in $trackedPlan.Descriptions)
        {
            Write-Host "    - $description"
        }
    }
    else
    {
        Write-Host '  Tracked mutations: none'
    }

    if ($PreviewOnly -or ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview))
    {
        return
    }

    # Keep only tools that need installation, update, or repair.
    $installPlan = @($plan | Where-Object {
            $_.Tool.capabilities.update -and (Test-GameWipToolPlanRequiresInstall -PlanItem $_ -TrackedPlan $trackedPlan)
        })

    if ($installPlan.Count -eq 0 -and $trackedPlan.Files.Count -eq 0)
    {
        Write-Host 'All selected tools and tracked declarations are already current.'
        return
    }

    # Confirm the repository is clean and list every planned mutation.
    Assert-GameWipCleanTrackedTree
    $installPlanLines = @($installPlan | ForEach-Object {
            $version = if ($_.Latest)
            {
                $_.Latest
            }
            elseif ($_.Tool.Contains('requiredVersion'))
            {
                $_.Tool.requiredVersion
            }
            else
            {
                'provider-managed version'
            }

            "$($_.Tool.id): install/verify $version"
        })
    $planLines = @($installPlanLines) +
    @($trackedPlan.Descriptions) +
    @('Format maintained files with the updated tools, then run the quality gate.')

    if (-not (Confirm-GameWipMutation `
                -Summary 'Apply the complete project-tool update plan?' `
                -Risk machine `
                -Plan $planLines))
    {
        Write-Host 'Project-tool update cancelled; no changes were made.'
        return
    }

    Invoke-GameWipMutationBody -Body {
        # Apply installs and tracked pins as one recoverable operation.
        Invoke-GameWipToolInstallPlan -InstallPlan $installPlan -TrackedPlan $trackedPlan
        Invoke-GameWipTrackedToolMutation -TrackedPlan $trackedPlan

        if ($trackedPlan.RegistryChanged)
        {
            $script:ProjectTools = Read-GameWipJsonConfig `
                -Path $ProjectToolsPath `
                -Name 'project tools' `
                -SchemaPath (Join-Path $ScriptsRoot 'schemas\project-tools.schema.json')
            Assert-GameWipProjectToolConfig
        }

        Write-GameWipOperationEvent `
            -Phase verify `
            -Severity info `
            -Message 'Reformatting with the updated tools, then running the complete quality gate...'
        Invoke-GameWipQuality -Mode fix
        Invoke-GameWipNative -Name 'git-status-after-tool-update' -FilePath 'git' -Arguments @('-C', $RepositoryRoot, 'status', '--short')
    }
}
