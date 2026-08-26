# GameWIP interactive UI. Navigation owns no operation state; every selected action gets a fresh operation.

Set-StrictMode -Version Latest

function Invoke-GameWipInteractiveOperation
{
    param([Parameter(Mandatory = $true)][string]$Label, [Parameter(Mandatory = $true)][scriptblock]$Body)
    $result = Invoke-GameWipOperation `
        -Label $Label `
        -ScriptBlock $Body `
        -NonInteractive:$NonInteractive `
        -Yes:$Yes `
        -Preview:$Preview `
        -OutputMode $OutputMode `
        -NoColor:$NoColor
    return $result
}

function Read-GameWipNamedChoice
{
    param([string]$Prompt, [string[]]$Choices, [string]$Default)
    $result = Read-GameWipMenuChoiceResult -Prompt $Prompt -Choices $Choices -Default $Default
    if ($result.Status -eq 'Cancelled')
    {
        return $null
    }
    return [string]$result.Value
}


function Get-GameWipInteractiveContext
{
    $branch = '<none>'
    $workspace = 'unknown'
    try
    {
        if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git'))
        {
            $branch = Get-GameWipCurrentBranch
            $changes = @(Invoke-GameWipGitQuery -Arguments @('status', '--porcelain', '--untracked-files=no'))
            $workspace = if ($changes.Count -eq 0)
            {
                'clean'
            }
            else
            {
                'modified'
            }
        }
    }
    catch
    {
        $workspace = 'degraded'
    }

    $environment = 'degraded'
    try
    {
        $cmake = Get-GameWipProjectTool -Id 'cmake'
        if ($null -ne (Resolve-GameWipToolCommand -Tool $cmake))
        {
            $environment = 'ready'
        }
    }
    catch
    {
        $null = $_.Exception
    }

    return [pscustomobject]@{ Branch = $branch; Workspace = $workspace; Environment = $environment }
}

function Write-GameWipInteractiveHeader
{
    $context = Get-GameWipInteractiveContext
    $workspaceSemantic = switch ([string]$context.Workspace)
    {
        'clean'
        {
            'Success'
        }
        'modified'
        {
            'Warning'
        }
        'degraded'
        {
            'Failure'
        }
        default
        {
            'Muted'
        }
    }
    $environmentSemantic = switch ([string]$context.Environment)
    {
        'ready'
        {
            'Success'
        }
        'degraded'
        {
            'Failure'
        }
        default
        {
            'Muted'
        }
    }

    Write-Host 'Branch: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$context.Branch) -Semantic Accent -NoNewline
    Write-Host '  Workspace: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$context.Workspace) -Semantic $workspaceSemantic -NoNewline
    Write-Host '  Environment: ' -NoNewline
    Write-GameWipSemanticText -Object ([string]$context.Environment) -Semantic $environmentSemantic
}

function Show-GameWipDevelopmentMenu
{
    while ($true)
    {
        Write-GameWipSection 'Development'
        Write-Host '1. Configure preset'
        Write-Host '2. Build preset'
        Write-Host '3. Run project command'
        Write-Host '4. Build documentation'
        Write-Host 'ESC. Back'
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys @('1', '2', '3', '4') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                $preset = Read-GameWipNamedChoice -Prompt 'Configure preset' -Choices (Get-GameWipVisiblePresetName -Kind configure) -Default $CommandConfig.DefaultConfigurePreset
                if ($null -ne $preset)
                {
                    Invoke-GameWipInteractiveOperation -Label "configure-$preset" -Body { Invoke-GameWipMutation -Summary "Configure preset '$preset'." -Risk local -Plan @("cmake --preset $preset") -Body { Invoke-GameWipConfigurePreset -Name $preset } | Out-Null } | Out-Null
                }
            }
            '2'
            {
                $preset = Read-GameWipNamedChoice -Prompt 'Build preset' -Choices (Get-GameWipVisiblePresetName -Kind build) -Default $CommandConfig.DefaultBuildPreset
                if ($null -ne $preset)
                {
                    Invoke-GameWipInteractiveOperation -Label "build-$preset" -Body { Invoke-GameWipMutation -Summary "Build preset '$preset'." -Risk local -Plan @('Ensure configuration if missing.', "cmake --build --preset $preset") -Body { Invoke-GameWipBuildPreset -Name $preset } | Out-Null } | Out-Null
                }
            }
            '3'
            {
                $id = Read-GameWipNamedChoice -Prompt 'Project command' -Choices @($CommandConfig.ProjectCommands | ForEach-Object { $_.Id }) -Default 'dev-version'
                if ($null -ne $id)
                {
                    Invoke-GameWipInteractiveOperation -Label "run-$id" -Body { Invoke-GameWipMutation -Summary "Run project command '$id'." -Risk local -Plan @('Ensure its executable unless -NoBuild is used.', 'Execute the cataloged project command.') -Body { Invoke-GameWipProjectCommand -Id $id -NoBuild:$NoBuild } | Out-Null } | Out-Null
                }
            }
            '4'
            {
                Invoke-GameWipInteractiveOperation -Label 'docs' -Body { Invoke-GameWipMutation -Summary 'Build generated documentation.' -Risk local -Plan @('Configure docs preset.', 'Build docs preset.') -Body { Invoke-GameWipConfigurePreset -Name docs; Invoke-GameWipBuildPreset -Name docs } | Out-Null } | Out-Null
            }
        }
    }
}

function Show-GameWipValidationMenu
{
    while ($true)
    {
        Write-GameWipSection 'Validation'
        Write-Host '1. Run CTest preset'
        Write-Host '2. Run validation module'
        Write-Host '3. Stress validation module'
        Write-Host '4. Validation command builder'
        Write-Host '5. Benchmarks'
        Write-Host '6. Coverage'
        Write-Host '7. AddressSanitizer'
        Write-Host 'ESC. Back'
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys @('1', '2', '3', '4', '5', '6', '7') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                $preset = Read-GameWipNamedChoice -Prompt 'CTest preset' -Choices (Get-GameWipVisiblePresetName -Kind test) -Default $CommandConfig.DefaultTestPreset
                if ($null -ne $preset)
                {
                    Invoke-GameWipInteractiveOperation -Label "test-$preset" -Body { Invoke-GameWipMutation -Summary "Ensure and run CTest preset '$preset'." -Risk local -Plan @('Build missing prerequisites unless -NoBuild is used.', "ctest --preset $preset") -Body { Invoke-GameWipTestPreset -Name $preset -UseWorkspaceTemp -NoBuild:$NoBuild } | Out-Null } | Out-Null
                }
            }
            '2'
            {
                $module = Read-GameWipNamedChoice -Prompt 'Validation module' -Choices (@('all') + @($CommandConfig.Modules)) -Default $CommandConfig.DefaultModule
                if ($null -ne $module)
                {
                    Invoke-GameWipInteractiveOperation -Label "module-$module" -Body { Invoke-GameWipMutation -Summary "Run validation module '$module'." -Risk local -Plan @('Ensure the validation executable unless -NoBuild is used.', 'Execute the selected correctness module.') -Body { Invoke-GameWipValidationModule -Name $module -NoBuild:$NoBuild } | Out-Null } | Out-Null
                }
            }
            '3'
            {
                $module = Read-GameWipNamedChoice -Prompt 'Stress module' -Choices (@('all') + @($CommandConfig.Modules)) -Default $CommandConfig.DefaultModule
                if ($null -ne $module)
                {
                    $runs = Read-GameWipIntegerValue -Prompt 'Run count' -Default ([int]$CommandConfig.DefaultStressCount)
                    $workers = Read-GameWipIntegerValue -Prompt 'Parallel workers' -Default ([int]$CommandConfig.DefaultStressParallel)
                    Invoke-GameWipInteractiveOperation -Label "stress-$module" -Body { Invoke-GameWipMutation -Summary "Stress validation module '$module'." -Risk local -Plan @('Ensure the validation executable unless -NoBuild is used.', "Run up to $runs validation processes with at most $workers workers.") -Body { Invoke-GameWipStressModule -Name $module -RunCount $runs -MaxParallel $workers -NoBuild:$NoBuild } | Out-Null } | Out-Null
                }
            }
            '4'
            {
                Invoke-GameWipInteractiveOperation -Label 'validation-wizard' -Body { Invoke-GameWipValidationCommandWizard -NoBuild:$NoBuild } | Out-Null
            }
            '5'
            {
                Invoke-GameWipInteractiveOperation -Label 'benchmark-standard' -Body { Invoke-GameWipMutation -Summary 'Build/run standard benchmark profile.' -Risk local -Plan @('Ensure benchmark executable unless -NoBuild is used.', 'Collect retained benchmark result.') -Body { Invoke-GameWipBenchmark -Mode run -ProfileId standard -RepeatCount 0 -MinimumTime '' -RequestedOutput '' -Format json -SkipBuild:$NoBuild } | Out-Null } | Out-Null
            }
            '6'
            {
                Invoke-GameWipInteractiveOperation -Label 'coverage' -Body { Invoke-GameWipMutation -Summary 'Run coverage validation.' -Risk local -Plan @('Configure/build coverage.', 'Run CTest.', 'Generate coverage target.') -Body { Invoke-GameWipConfigurePreset -Name coverage; Invoke-GameWipBuildPreset -Name coverage; Invoke-GameWipTestPreset -Name coverage -UseWorkspaceTemp -NoBuild; Invoke-GameWipBuildTarget -Name coverage -Target coverage } | Out-Null } | Out-Null
            }
            '7'
            {
                Invoke-GameWipInteractiveOperation -Label 'asan' -Body { Invoke-GameWipMutation -Summary 'Run AddressSanitizer validation.' -Risk local -Plan @('Configure/build asan.', 'Run CTest.') -Body { Invoke-GameWipConfigurePreset -Name asan; Invoke-GameWipBuildPreset -Name asan; Invoke-GameWipTestPreset -Name asan -UseWorkspaceTemp -NoBuild } | Out-Null } | Out-Null
            }
        }
    }
}

function Show-GameWipQualityMenu
{
    while ($true)
    {
        Write-GameWipSection 'Quality'
        Write-Host '1. Full quality check'
        Write-Host '2. Apply deterministic formatters, then check'
        Write-Host '3. C/C++ formatting check'
        Write-Host '4. Apply C/C++ formatting'
        Write-Host '5. C++ static analysis'
        Write-Host 'ESC. Back'
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys @('1', '2', '3', '4', '5') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                Invoke-GameWipInteractiveOperation -Label 'quality-check' -Body { Invoke-GameWipQuality -Mode check } | Out-Null
            }
            '2'
            {
                Invoke-GameWipInteractiveOperation -Label 'quality-fix' -Body { Invoke-GameWipMutation -Summary 'Apply deterministic repository formatters.' -Risk tracked -Plan @('Format maintained sources/configuration.', 'Run the complete quality gate.') -Body { Invoke-GameWipQuality -Mode fix } | Out-Null } | Out-Null
            }
            '3'
            {
                Invoke-GameWipInteractiveOperation -Label 'format-check' -Body { Invoke-GameWipFormat -Mode check } | Out-Null
            }
            '4'
            {
                Invoke-GameWipInteractiveOperation -Label 'format-apply' -Body { Invoke-GameWipMutation -Summary 'Apply C/C++ formatting.' -Risk tracked -Plan @('Rewrite maintained C/C++ files with repository clang-format.') -Body { Invoke-GameWipFormat -Mode apply } | Out-Null } | Out-Null
            }
            '5'
            {
                Invoke-GameWipInteractiveOperation -Label 'analyze' -Body { Invoke-GameWipMutation -Summary 'Run C++ static analysis.' -Risk local -Plan @('Configure analyze preset.', 'Build analyze preset.') -Body { Invoke-GameWipConfigurePreset -Name analyze; Invoke-GameWipBuildPreset -Name analyze } | Out-Null } | Out-Null
            }
        }
    }
}

function Show-GameWipToolsMenu
{
    while ($true)
    {
        Write-GameWipSection 'Tools and environment'
        Write-Host '1. Project readiness'
        Write-Host '2. Tool status (offline)'
        Write-Host '3. Check upstream versions'
        Write-Host '4. Preview tool update'
        Write-Host '5. Apply tool update'
        Write-Host '6. Setup guidance'
        Write-Host 'ESC. Back'
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys @('1', '2', '3', '4', '5', '6') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                Invoke-GameWipInteractiveOperation -Label 'doctor' -Body { Test-GameWipProjectReadiness | Out-Null } | Out-Null
            }
            '2'
            {
                Invoke-GameWipInteractiveOperation -Label 'tools-status' -Body { Show-GameWipToolStatus } | Out-Null
            }
            '3'
            {
                Invoke-GameWipInteractiveOperation -Label 'tools-check-updates' -Body { Show-GameWipToolUpdatePlan -Plan @(Get-GameWipToolUpdatePlan -ToolId all) } | Out-Null
            }
            '4'
            {
                $id = Read-GameWipNamedChoice -Prompt 'Tool to preview' -Choices (@('all') + @($ProjectTools.tools | Where-Object { $_.capabilities.update } | ForEach-Object { $_.id })) -Default all
                if ($null -ne $id)
                {
                    Invoke-GameWipInteractiveOperation -Label "tools-preview-$id" -Body { Invoke-GameWipToolUpdate -ToolId $id -PreviewOnly } | Out-Null
                }
            }
            '5'
            {
                $id = Read-GameWipNamedChoice -Prompt 'Tool to update' -Choices (@('all') + @($ProjectTools.tools | Where-Object { $_.capabilities.update } | ForEach-Object { $_.id })) -Default all
                if ($null -ne $id)
                {
                    Invoke-GameWipInteractiveOperation -Label "tools-update-$id" -Body { Invoke-GameWipToolUpdate -ToolId $id } | Out-Null
                }
            }
            '6'
            {
                Write-Host 'Use .\setup.bat check for environment status or .\setup.bat repair for an idempotent repair.'
            }
        }
    }
}

function Show-GameWipRepositoryMenu
{
    while ($true)
    {
        Write-GameWipSection 'Repository and GitHub'
        Write-Host '1. Git status'
        Write-Host '2. Fetch remotes'
        Write-Host '3. Switch branch'
        Write-Host '4. Update current branch'
        Write-Host '5. Workspace cleanup'
        Write-Host '6. Git log'
        Write-Host '7. List guarded workflows'
        Write-Host '8. Run guarded workflow'
        Write-Host 'ESC. Back'
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys @('1', '2', '3', '4', '5', '6', '7', '8') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                Invoke-GameWipInteractiveOperation -Label 'git-status' -Body { Invoke-GameWipGitAction -Name status } | Out-Null
            }
            '2'
            {
                Invoke-GameWipInteractiveOperation -Label 'git-fetch' -Body { Invoke-GameWipGitAction -Name fetch } | Out-Null
            }
            '3'
            {
                Invoke-GameWipInteractiveOperation -Label 'git-switch' -Body { Invoke-GameWipGitAction -Name switch } | Out-Null
            }
            '4'
            {
                Invoke-GameWipInteractiveOperation -Label 'git-update' -Body { Invoke-GameWipGitAction -Name update } | Out-Null
            }
            '5'
            {
                Invoke-GameWipInteractiveOperation -Label 'git-cleanup' -Body { Invoke-GameWipGitAction -Name cleanup } | Out-Null
            }
            '6'
            {
                Invoke-GameWipInteractiveOperation -Label 'git-log' -Body { Invoke-GameWipGitAction -Name log } | Out-Null
            }
            '7'
            {
                Invoke-GameWipInteractiveOperation -Label 'workflow-list' -Body { Invoke-GameWipWorkflowAction -Name list } | Out-Null
            }
            '8'
            {
                $workflow = Read-GameWipNamedChoice -Prompt 'Workflow' -Choices @($CommandConfig.ManualWorkflows | ForEach-Object { $_.Id }) -Default validation
                if ($null -ne $workflow)
                {
                    Invoke-GameWipInteractiveOperation -Label "workflow-$workflow" -Body { Invoke-GameWipWorkflowAction -Name run -WorkflowId $workflow } | Out-Null
                }
            }
        }
    }
}

function Show-GameWipMaintenanceMenu
{
    while ($true)
    {
        Write-GameWipSection 'Maintenance'
        Write-Host '1. Unicode status'
        Write-Host '2. Verify Unicode generated data'
        Write-Host '3. Regenerate Unicode tracked table'
        Write-Host '4. Run command bundle'
        Write-Host '5. Validate Markdown links'
        Write-Host 'ESC. Back'
        $choice = Read-GameWipActionKey -Prompt 'Choose an action:' -AllowedKeys @('1', '2', '3', '4', '5') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                Invoke-GameWipInteractiveOperation -Label 'unicode-status' -Body { Show-GameWipUnicodeStatus } | Out-Null
            }
            '2'
            {
                Invoke-GameWipInteractiveOperation -Label 'unicode-verify' -Body { Invoke-GameWipMutation -Summary 'Verify reproducible Unicode generated data.' -Risk local -Plan @('Verify/download pinned Unicode input in the owned cache.', 'Generate and format an operation-owned candidate.', 'Compare the candidate with the checked-in table.') -Body { Invoke-GameWipUnicodeVerify } | Out-Null } | Out-Null
            }
            '3'
            {
                Invoke-GameWipInteractiveOperation -Label 'unicode-regenerate' -Body { Invoke-GameWipMutation -Summary 'Regenerate the tracked Unicode property table.' -Risk tracked -Plan @('Download/verify pinned UCD inputs.', 'Generate and format candidate.', 'Replace tracked table only when content differs.') -Body { Invoke-GameWipUnicodeRegenerate } | Out-Null } | Out-Null
            }
            '4'
            {
                $bundle = Read-GameWipNamedChoice -Prompt 'Bundle' -Choices @($CommandConfig.Bundles | ForEach-Object { $_.Id }) -Default quick
                if ($null -ne $bundle)
                {
                    Invoke-GameWipInteractiveOperation -Label "bundle-$bundle" -Body { Invoke-GameWipMutation -Summary "Run bundle '$bundle'." -Risk local -Plan @('Execute the declared bundle steps in order.') -Body { Invoke-GameWipBundle -Id $bundle -NoBuild:$NoBuild } | Out-Null } | Out-Null
                }
            }
            '5'
            {
                Invoke-GameWipInteractiveOperation -Label 'links' -Body { Invoke-GameWipMarkdownLink } | Out-Null
            }
        }
    }
}

function Show-GameWipMenu
{
    Assert-GameWipInteractiveConsole -Purpose 'The GameWIP menu'
    while ($true)
    {
        Write-GameWipSection 'GameWIP'
        Write-GameWipInteractiveHeader
        Write-Host '1. Development'
        Write-Host '2. Validation'
        Write-Host '3. Quality'
        Write-Host '4. Tools and environment'
        Write-Host '5. Repository and GitHub'
        Write-Host '6. Maintenance'
        Write-Host 'D. Project readiness'
        Write-Host 'H. Help/catalog'
        Write-Host 'ESC. Exit'
        $choice = Read-GameWipActionKey -Prompt 'Choose an area:' -AllowedKeys @('1', '2', '3', '4', '5', '6', 'd', 'D', 'h', 'H') -AllowEscape
        if ($choice.Status -eq 'Cancelled')
        {
            return
        }
        switch ([string]$choice.Value)
        {
            '1'
            {
                Show-GameWipDevelopmentMenu
            }
            '2'
            {
                Show-GameWipValidationMenu
            }
            '3'
            {
                Show-GameWipQualityMenu
            }
            '4'
            {
                Show-GameWipToolsMenu
            }
            '5'
            {
                Show-GameWipRepositoryMenu
            }
            '6'
            {
                Show-GameWipMaintenanceMenu
            }
            { $_ -eq 'd' -or $_ -eq 'D' }
            {
                Invoke-GameWipInteractiveOperation -Label doctor -Body { Test-GameWipProjectReadiness | Out-Null } | Out-Null
            }
            { $_ -eq 'h' -or $_ -eq 'H' }
            {
                Show-GameWipHelp; Show-GameWipProjectCatalog
            }
        }
    }
}
