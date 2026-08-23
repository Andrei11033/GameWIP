# GameWIP Workflows helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Get-GameWipWorkflow
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $workflowInfo = @($CommandConfig.ManualWorkflows | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($workflowInfo.Count -eq 0)
    {
        throw "Unknown manual workflow '$Id'. Run '.\gamewip.bat workflow -WorkflowAction list' to see supported workflows."
    }
    $workflowInfo[0]
}

function Show-GameWipWorkflowCatalog
{
    Write-GameWipSection 'Manual GitHub workflows'
    Write-Host "Repository: $($ProjectConfig.repository)"
    Write-Host "Dispatch ref: $($ProjectConfig.defaultBranch) (fixed)"
    Write-Host ''
    foreach ($workflowInfo in $CommandConfig.ManualWorkflows)
    {
        Write-Host ('  {0,-27} [{1,-8}] {2}' -f $workflowInfo.Id, $workflowInfo.Safety, $workflowInfo.Name)
    }
    Write-Host ''
    Write-Host 'check/dry-run operations do not mutate repository state.'
    Write-Host 'write/deploy/finalize operations require typed confirmation and protected-environment approval.'
}

function Invoke-GameWipGhJson
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = 'Continue'
        $output = @(& gh @Arguments 2>&1)
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($exitCode -ne 0)
    {
        throw "GitHub CLI command failed: gh $($Arguments -join ' ')$([Environment]::NewLine)$($output -join [Environment]::NewLine)"
    }
    $json = ($output -join [Environment]::NewLine).Trim()
    if ([string]::IsNullOrWhiteSpace($json))
    {
        return @()
    }
    @($json | ConvertFrom-Json)
}

function Resolve-GameWipWorkflowArgument
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowId,
        [Parameter(Mandatory = $true)][string]$ItemKind,
        [int]$ItemNumber,
        [string]$Commit
    )

    $workflowInfo = Get-GameWipWorkflow -Id $WorkflowId
    $arguments = New-Object System.Collections.Generic.List[string]
    @('workflow', 'run', $workflowInfo.File, '--repo', $ProjectConfig.repository, '--ref', $ProjectConfig.defaultBranch) |
        ForEach-Object { $arguments.Add($_) | Out-Null }

    if ($WorkflowId -like 'project-*')
    {
        if ($ItemKind -ne 'all' -and $ItemNumber -le 0)
        {
            $numberText = Read-GameWipTextValue -Prompt "$ItemKind number"
            if (-not [int]::TryParse($numberText, [ref]$ItemNumber))
            {
                $ItemNumber = 0
            }
        }
        if ($ItemKind -ne 'all' -and $ItemNumber -le 0)
        {
            throw "Project kind '$ItemKind' requires -WorkflowNumber with a positive issue or pull-request number."
        }
        @('-f', "kind=$ItemKind") | ForEach-Object { $arguments.Add($_) | Out-Null }
        if ($ItemKind -ne 'all')
        {
            @('-f', "number=$ItemNumber") | ForEach-Object { $arguments.Add($_) | Out-Null }
        }
        $dryRunValue = if ($WorkflowId -eq 'project-dry-run')
        {
            'true'
        }
        else
        {
            'false'
        }
        @('-f', "dry_run=$dryRunValue") | ForEach-Object { $arguments.Add($_) | Out-Null }
    }

    if ($WorkflowId -like 'release-*')
    {
        $command = switch ($WorkflowId)
        {
            'release-check'
            {
                'check'
            }
            'release-prepare'
            {
                'prepare'
            }
            default
            {
                'finalize'
            }
        }
        $dryRunValue = if ($WorkflowId -eq 'release-prepare' -or $WorkflowId -eq 'release-finalize')
        {
            'false'
        }
        else
        {
            'true'
        }
        @('-f', "command=$command", '-f', "dry_run=$dryRunValue") |
            ForEach-Object { $arguments.Add($_) | Out-Null }

        if ($WorkflowId -like 'release-finalize*')
        {
            if ([string]::IsNullOrWhiteSpace($Commit))
            {
                $Commit = Read-GameWipTextValue -Prompt 'Exact master commit SHA to finalize'
            }
            if ($Commit -notmatch '^[0-9a-fA-F]{40}$')
            {
                throw 'Release finalization requires the complete 40-character master commit SHA.'
            }
            @('-f', "release_commit=$Commit") | ForEach-Object { $arguments.Add($_) | Out-Null }
        }
    }

    [pscustomobject]@{
        Definition = $workflowInfo
        Arguments = $arguments.ToArray()
        ReleaseCommit = $Commit
    }
}

function Confirm-GameWipTypedPhrase
{
    param([Parameter(Mandatory = $true)][string]$Phrase)

    Write-Host ''
    Write-Host 'This operation can change shared GitHub state.' -ForegroundColor Yellow
    Write-Host "Type exactly: $Phrase"
    $answer = Read-Host 'Confirmation'
    if ($answer -cne $Phrase)
    {
        Write-Host 'Confirmation did not match; workflow dispatch cancelled.' -ForegroundColor Yellow
        return $false
    }
    return $true
}

function Get-GameWipWorkflowRunId
{
    param([Parameter(Mandatory = $true)][string]$WorkflowFile)

    $runs = Invoke-GameWipGhJson -Arguments @(
        'run', 'list',
        '--repo', $ProjectConfig.repository,
        '--workflow', $WorkflowFile,
        '--event', 'workflow_dispatch',
        '--branch', $ProjectConfig.defaultBranch,
        '--limit', '10',
        '--json', 'databaseId'
    )
    @($runs | ForEach-Object { [string]$_.databaseId })
}

function Wait-GameWipWorkflowRun
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowFile,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$PreviousIds
    )

    for ($attempt = 0; $attempt -lt 15; ++$attempt)
    {
        Start-Sleep -Seconds 2
        $runs = Invoke-GameWipGhJson -Arguments @(
            'run', 'list',
            '--repo', $ProjectConfig.repository,
            '--workflow', $WorkflowFile,
            '--event', 'workflow_dispatch',
            '--branch', $ProjectConfig.defaultBranch,
            '--limit', '10',
            '--json', 'databaseId,url,status,createdAt'
        )
        $run = @($runs | Where-Object { $PreviousIds -notcontains [string]$_.databaseId } | Select-Object -First 1)
        if ($run.Count -ne 0)
        {
            return $run[0]
        }
    }
    return $null
}

function Show-GameWipWorkflowVerification
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowId,
        [Parameter(Mandatory = $true)][string]$RunId
    )

    Write-GameWipSection 'Verification commands'
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments @('run', 'view', $RunId, '--repo', $ProjectConfig.repository))
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments @('run', 'view', $RunId, '--repo', $ProjectConfig.repository, '--log-failed'))
    if ($WorkflowId -eq 'release-finalize')
    {
        Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments @('release', 'list', '--repo', $ProjectConfig.repository, '--limit', '5'))
        Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'git' -Arguments @('ls-remote', '--tags', 'origin'))
    }
    elseif ($WorkflowId -eq 'release-prepare')
    {
        Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments @('pr', 'list', '--repo', $ProjectConfig.repository, '--state', 'open'))
    }
    elseif ($WorkflowId -eq 'docs-deploy')
    {
        Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments @('api', "repos/$($ProjectConfig.repository)/pages"))
    }
}

function Invoke-GameWipManualWorkflow
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowId,
        [Parameter(Mandatory = $true)][string]$ItemKind,
        [int]$ItemNumber,
        [string]$Commit
    )

    $resolved = Resolve-GameWipWorkflowArgument -WorkflowId $WorkflowId -ItemKind $ItemKind -ItemNumber $ItemNumber -Commit $Commit
    $workflowInfo = $resolved.Definition
    $dispatchCommand = ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments $resolved.Arguments

    Write-GameWipSection 'Workflow dispatch preview'
    Write-Host "Workflow: $($workflowInfo.Name)"
    Write-Host "Safety:   $($workflowInfo.Safety)"
    Write-Host "Ref:      $($ProjectConfig.defaultBranch) (fixed)"
    Write-Host ''
    Write-Host $dispatchCommand
    Write-Host 'gh run watch <run-id> --exit-status'
    Write-Host 'gh run view <run-id> --log-failed'

    if ($Preview)
    {
        Write-Host ''
        Write-Host 'Preview only; nothing was dispatched.' -ForegroundColor Green
        return
    }

    Assert-GameWipGitHubCli -WorkflowId $WorkflowId
    if (@('write', 'deploy', 'finalize') -contains $workflowInfo.Safety)
    {
        $phrase = "$WorkflowId $($ProjectConfig.defaultBranch)"
        if ($WorkflowId -eq 'release-finalize')
        {
            $phrase = "$phrase $($resolved.ReleaseCommit)"
        }
        if (-not (Confirm-GameWipTypedPhrase -Phrase $phrase))
        {
            return
        }
    }
    elseif (-not (Read-GameWipYesNo -Prompt 'Dispatch this non-mutating workflow now?' -Default $true))
    {
        Write-Host 'Workflow dispatch cancelled.'
        return
    }

    $previousIds = @(Get-GameWipWorkflowRunId -WorkflowFile $workflowInfo.File)
    Invoke-GameWipNative -Name "workflow-dispatch-$WorkflowId" -FilePath 'gh' -Arguments $resolved.Arguments
    $run = Wait-GameWipWorkflowRun -WorkflowFile $workflowInfo.File -PreviousIds $previousIds
    if ($null -eq $run)
    {
        Write-Host 'Dispatch succeeded, but the new run was not visible within 30 seconds.' -ForegroundColor Yellow
        Write-Host "Check it with: .\gamewip.bat workflow -WorkflowAction status"
        return
    }

    $runId = [string]$run.databaseId
    Write-Host ''
    Write-Host "Queued run: $($run.url)" -ForegroundColor Green
    $watchArguments = @('run', 'watch', $runId, '--repo', $ProjectConfig.repository, '--exit-status')
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath 'gh' -Arguments $watchArguments)
    if (Read-GameWipYesNo -Prompt 'Watch this run until it finishes?' -Default $true)
    {
        Invoke-GameWipNative -Name "workflow-watch-$runId" -FilePath 'gh' -Arguments $watchArguments
    }
    Show-GameWipWorkflowVerification -WorkflowId $WorkflowId -RunId $runId
}

function Show-GameWipWorkflowStatus
{
    Assert-GameWipGitHubCli -WorkflowId 'validation'
    Invoke-GameWipNative -Name 'workflow-recent-runs' -FilePath 'gh' -Arguments @(
        'run', 'list',
        '--repo', $ProjectConfig.repository,
        '--event', 'workflow_dispatch',
        '--limit', '12'
    )
}

function Show-GameWipWorkflowMenu
{
    while ($true)
    {
        Write-Host ''
        Write-Host 'GitHub Workflows'
        Write-Host '================'
        Write-Host '1. List supported workflows'
        Write-Host '2. Dispatch a supported workflow'
        Write-Host '3. Show recent manual runs'
        Write-Host 'ESC. Back'
        Write-Host 'Choose an action: ' -NoNewline
        $key = [Console]::ReadKey($true)
        if ($key.Key -eq [ConsoleKey]::ESCape)
        {
            Write-Host 'ESC'; return
        }
        Write-Host $key.KeyChar
        switch ($key.KeyChar)
        {
            '1'
            {
                Show-GameWipWorkflowCatalog
            }
            '2'
            {
                Show-GameWipWorkflowCatalog
                $choice = Read-GameWipIndexedChoice -Prompt 'Workflow to dispatch' -Choices @($CommandConfig.ManualWorkflows | ForEach-Object { $_.Id })
                if ($null -ne $choice)
                {
                    Invoke-GameWipManualWorkflow -WorkflowId $choice -ItemKind $WorkflowKind -ItemNumber $WorkflowNumber -Commit $ReleaseCommit
                }
            }
            '3'
            {
                Show-GameWipWorkflowStatus
            }
            default
            {
                Write-Host 'Press 1-3 or ESC.' -ForegroundColor Yellow
            }
        }
    }
}

function Invoke-GameWipWorkflowAction
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$WorkflowId
    )
    switch ($Name)
    {
        'menu'
        {
            Show-GameWipWorkflowMenu
        }
        'list'
        {
            Show-GameWipWorkflowCatalog
        }
        'status'
        {
            Show-GameWipWorkflowStatus
        }
        'run'
        {
            if ([string]::IsNullOrWhiteSpace($WorkflowId))
            {
                throw "WorkflowAction 'run' requires -Workflow <id>. Run '.\gamewip.bat workflow -WorkflowAction list' first."
            }
            Invoke-GameWipManualWorkflow -WorkflowId $WorkflowId -ItemKind $WorkflowKind -ItemNumber $WorkflowNumber -Commit $ReleaseCommit
        }
    }
}
