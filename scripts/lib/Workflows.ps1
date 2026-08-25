# GameWIP guarded GitHub workflow discovery, dispatch, and verification.

Set-StrictMode -Version Latest

function Get-GameWipWorkflow
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $workflowInfo = @($CommandConfig.ManualWorkflows | Where-Object { $_.Id -eq $Id } | Select-Object -First 1)
    if ($workflowInfo.Count -eq 0)
    {
        throw "Unknown manual workflow '$Id'. Run '.\gamewip.bat workflow list'."
    }
    return $workflowInfo[0]
}

function Show-GameWipWorkflowCatalog
{
    Write-GameWipSection 'Manual GitHub workflows'
    Write-Host "Repository: $($ProjectConfig.repository)"
    Write-Host "Dispatch ref: $($ProjectConfig.defaultBranch) (fixed)"
    foreach ($workflowInfo in $CommandConfig.ManualWorkflows)
    {
        Write-Host ('  {0,-27} [{1,-8}] {2}' -f $workflowInfo.Id, $workflowInfo.Safety, $workflowInfo.Name)
    }
}

function Assert-GameWipGitHubCli
{
    if ($null -eq (Get-Command gh -ErrorAction SilentlyContinue))
    {
        throw "GitHub CLI is unavailable. Run '.\setup.bat repair', then 'gh auth login'."
    }
    $auth = Invoke-GameWipProcess -FilePath gh -Arguments @('auth', 'status', '--hostname', 'github.com') -OutputMode LogOnly -TimeoutSeconds 20
    if ($auth.ExitCode -ne 0)
    {
        throw (New-GameWipDiagnosticException -Code 'github-auth' -Summary 'GitHub CLI authentication is unavailable.' -Details (($auth.Stderr + $auth.Stdout) -join "`n") -SuggestedActions @('Run gh auth login.', 'Run gh auth status --hostname github.com.'))
    }
}

function Invoke-GameWipGhJson
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments)
    $result = Invoke-GameWipProcess -FilePath gh -Arguments $Arguments -OutputMode LogOnly -TimeoutSeconds 30
    if ($result.ExitCode -ne 0)
    {
        throw "GitHub CLI query failed: gh $($Arguments -join ' ')"
    }
    $json = ($result.Stdout -join "`n").Trim()
    if ([string]::IsNullOrWhiteSpace($json))
    {
        return @()
    }
    return @($json | ConvertFrom-Json)
}

function Resolve-GameWipWorkflowArgument
{
    param([Parameter(Mandatory = $true)][string]$WorkflowId, [Parameter(Mandatory = $true)][string]$ItemKind, [int]$ItemNumber, [string]$Commit)
    $workflowInfo = Get-GameWipWorkflow -Id $WorkflowId
    $arguments = [Collections.Generic.List[string]]::new()
    @('workflow', 'run', $workflowInfo.File, '--repo', $ProjectConfig.repository, '--ref', $ProjectConfig.defaultBranch) | ForEach-Object { $arguments.Add($_) | Out-Null }
    if ($WorkflowId -like 'project-*')
    {
        if ($ItemKind -ne 'all' -and $ItemNumber -le 0)
        {
            throw "Project kind '$ItemKind' requires -WorkflowNumber with a positive issue or pull-request number."
        }
        @('-f', "kind=$ItemKind") | ForEach-Object { $arguments.Add($_) | Out-Null }
        if ($ItemKind -ne 'all')
        {
            @('-f', "number=$ItemNumber") | ForEach-Object { $arguments.Add($_) | Out-Null }
        }
        @('-f', "dry_run=$(if ($WorkflowId -eq 'project-dry-run') { 'true' } else { 'false' })") | ForEach-Object { $arguments.Add($_) | Out-Null }
    }
    if ($WorkflowId -like 'release-*')
    {
        $command = if ($WorkflowId -eq 'release-check')
        {
            'check'
        }
        elseif ($WorkflowId -eq 'release-prepare')
        {
            'prepare'
        }
        else
        {
            'finalize'
        }
        $dryRun = if ($WorkflowId -in @('release-prepare', 'release-finalize'))
        {
            'false'
        }
        else
        {
            'true'
        }
        @('-f', "command=$command", '-f', "dry_run=$dryRun") | ForEach-Object { $arguments.Add($_) | Out-Null }
        if ($WorkflowId -like 'release-finalize*')
        {
            if ($Commit -notmatch '^[0-9a-fA-F]{40}$')
            {
                throw 'Release finalization requires the complete 40-character master commit SHA.'
            }
            @('-f', "release_commit=$Commit") | ForEach-Object { $arguments.Add($_) | Out-Null }
        }
    }
    return [pscustomobject]@{ Definition = $workflowInfo; Arguments = $arguments.ToArray(); ReleaseCommit = $Commit }
}

function Get-GameWipWorkflowRunId
{
    param([Parameter(Mandatory = $true)][string]$WorkflowFile)
    $runs = Invoke-GameWipGhJson -Arguments @('run', 'list', '--repo', $ProjectConfig.repository, '--workflow', $WorkflowFile, '--event', 'workflow_dispatch', '--branch', $ProjectConfig.defaultBranch, '--user', '@me', '--limit', '10', '--json', 'databaseId')
    return @($runs | ForEach-Object { [string]$_.databaseId })
}

function Wait-GameWipWorkflowRun
{
    param(
        [Parameter(Mandatory = $true)][string]$WorkflowFile,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][string[]]$PreviousIds,
        [Parameter(Mandatory = $true)][datetime]$DispatchedAfter
    )
    $threshold = $DispatchedAfter.ToUniversalTime().AddSeconds(-2)
    for ($attempt = 1; $attempt -le 15; ++$attempt)
    {
        Assert-GameWipNotCancelled
        Start-Sleep -Seconds 2
        $runs = Invoke-GameWipGhJson -Arguments @('run', 'list', '--repo', $ProjectConfig.repository, '--workflow', $WorkflowFile, '--event', 'workflow_dispatch', '--branch', $ProjectConfig.defaultBranch, '--user', '@me', '--limit', '10', '--json', 'databaseId,url,status,createdAt')
        $candidates = @(
            $runs |
                Where-Object {
                    $PreviousIds -notcontains [string]$_.databaseId -and
                    [DateTimeOffset]::Parse([string]$_.createdAt).UtcDateTime -ge $threshold
                } |
                Sort-Object {
                    [DateTimeOffset]::Parse([string]$_.createdAt).UtcDateTime
                }
        )

        if ($candidates.Count -eq 1)
        {
            return $candidates[0]
        }

        if ($candidates.Count -gt 1)
        {
            throw (
                "Workflow dispatch correlation is ambiguous: {0} matching runs appeared " +
                "for '{1}' after dispatch. Refusing to attach to an arbitrary run."
            ) -f $candidates.Count, $WorkflowFile
        }
        Write-Verbose "Waiting for dispatched workflow to appear ($attempt/15)."
    }
    return $null
}

function Show-GameWipWorkflowVerification
{
    param([Parameter(Mandatory = $true)][string]$WorkflowId, [Parameter(Mandatory = $true)][string]$RunId)
    Write-GameWipSection 'Verification commands'
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath gh -Arguments @('run', 'view', $RunId, '--repo', $ProjectConfig.repository))
    Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath gh -Arguments @('run', 'view', $RunId, '--repo', $ProjectConfig.repository, '--log-failed'))
    if ($WorkflowId -eq 'release-finalize')
    {
        Write-Host (ConvertTo-GameWipNativeCommandLine -FilePath gh -Arguments @('release', 'list', '--repo', $ProjectConfig.repository, '--limit', '5'))
    }
}

function Invoke-GameWipManualWorkflow
{
    param([Parameter(Mandatory = $true)][string]$WorkflowId, [Parameter(Mandatory = $true)][string]$ItemKind, [int]$ItemNumber, [string]$Commit)
    $resolved = Resolve-GameWipWorkflowArgument -WorkflowId $WorkflowId -ItemKind $ItemKind -ItemNumber $ItemNumber -Commit $Commit
    $workflowInfo = $resolved.Definition
    $dispatchCommand = ConvertTo-GameWipNativeCommandLine -FilePath gh -Arguments $resolved.Arguments
    Write-GameWipSection 'Workflow dispatch plan'
    Write-Host "Workflow: $($workflowInfo.Name)"
    Write-Host "Safety:   $($workflowInfo.Safety)"
    Write-Host "Ref:      $($ProjectConfig.defaultBranch) (fixed)"
    Write-Host $dispatchCommand

    $phrase = ''
    if (@('write', 'deploy', 'finalize') -contains $workflowInfo.Safety)
    {
        $phrase = "$WorkflowId $($ProjectConfig.defaultBranch)"
        if ($WorkflowId -eq 'release-finalize')
        {
            $phrase = "$phrase $($resolved.ReleaseCommit)"
        }
    }
    $dispatchState = @{ PreviousIds = @(); DispatchedAfter = $null }
    $applied = Invoke-GameWipMutation -Summary "Dispatch '$WorkflowId' on $($ProjectConfig.defaultBranch)." -Risk remote -Plan @($dispatchCommand, 'The dispatched run is a shared GitHub resource.') -TypedPhrase $phrase -Body {
        Assert-GameWipGitHubCli
        $dispatchState.PreviousIds = @(Get-GameWipWorkflowRunId -WorkflowFile $workflowInfo.File)
        $dispatchState.DispatchedAfter = (Get-Date).ToUniversalTime()
        Invoke-GameWipNative -Name "workflow-dispatch-$WorkflowId" -FilePath gh -Arguments $resolved.Arguments
    }
    if (-not $applied)
    {
        return
    }

    $run = Wait-GameWipWorkflowRun -WorkflowFile $workflowInfo.File -PreviousIds $dispatchState.PreviousIds -DispatchedAfter $dispatchState.DispatchedAfter
    if ($null -eq $run)
    {
        Add-GameWipOperationWarning -Message 'Dispatch succeeded, but the new run was not visible within 30 seconds.'; return
    }
    $runId = [string]$run.databaseId
    Write-GameWipHost "Queued run: $($run.url)" -ForegroundColor Green
    $watch = $true
    if ($null -eq $Script:OperationContext -or -not $Script:OperationContext.NonInteractive)
    {
        $watch = Read-GameWipYesNo -Prompt 'Watch this run until it finishes?' -Default $true
    }
    if ($watch)
    {
        Invoke-GameWipNative -Name "workflow-watch-$runId" -FilePath gh -Arguments @('run', 'watch', $runId, '--repo', $ProjectConfig.repository, '--exit-status')
    }
    Show-GameWipWorkflowVerification -WorkflowId $WorkflowId -RunId $runId
}

function Show-GameWipWorkflowStatus
{
    Assert-GameWipGitHubCli
    Invoke-GameWipNative -Name 'workflow-recent-runs' -FilePath gh -Arguments @('run', 'list', '--repo', $ProjectConfig.repository, '--event', 'workflow_dispatch', '--limit', '12')
}

function Invoke-GameWipWorkflowAction
{
    param([Parameter(Mandatory = $true)][ValidateSet('list', 'status', 'run')][string]$Name, [string]$WorkflowId)
    switch ($Name)
    {
        list
        {
            Show-GameWipWorkflowCatalog
        }
        status
        {
            Show-GameWipWorkflowStatus
        }
        run
        {
            if ([string]::IsNullOrWhiteSpace($WorkflowId))
            {
                throw "workflow run requires a workflow ID. Run '.\gamewip.bat workflow list'."
            }
            Invoke-GameWipManualWorkflow -WorkflowId $WorkflowId -ItemKind $WorkflowKind -ItemNumber $WorkflowNumber -Commit $ReleaseCommit
        }
    }
}
