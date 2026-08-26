# Regression tests for guarded GameWIP GitHub workflow discovery and dispatch contracts.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'

. $helperPath -Action help *> $null

$script:WorkflowRunResponses = @()

function Start-Sleep
{
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSAvoidOverwritingBuiltInCmdlets', '', Justification = 'The test replaces sleeping with a deterministic no-op.')]
    [Diagnostics.CodeAnalysis.SuppressMessageAttribute('PSUseShouldProcessForStateChangingFunctions', '', Justification = 'The no-op test double never changes state.')]
    param([int]$Seconds)

    $null = $Seconds
}

function Invoke-GameWipGhJson
{
    param([Parameter(Mandatory = $true)][string[]]$Arguments)

    $null = $Arguments
    @($script:WorkflowRunResponses)
}

function Assert-GameWipRunId
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [Parameter(Mandatory = $true)][string]$ExpectedId
    )

    $actualId = [string]$Run.databaseId
    if ($actualId -ne $ExpectedId)
    {
        throw "Expected workflow run '$ExpectedId', received '$actualId'."
    }
}

$dispatchTime = [datetime]'2026-07-19T00:00:30Z'

$script:WorkflowRunResponses = @(
    [pscustomobject]@{
        databaseId = 9001
        url = 'https://example.invalid/actions/runs/9001'
        status = 'queued'
        createdAt = '2026-07-19T00:00:31Z'
    }
)
$firstRun = Wait-GameWipWorkflowRun `
    -WorkflowFile 'project-automation.yml' `
    -PreviousIds @() `
    -DispatchedAfter $dispatchTime
Assert-GameWipRunId -Run $firstRun -ExpectedId '9001'

$script:WorkflowRunResponses = @(
    [pscustomobject]@{
        databaseId = 9002
        url = 'https://example.invalid/actions/runs/9002'
        status = 'queued'
        createdAt = '2026-07-19T00:01:00Z'
    },
    [pscustomobject]@{
        databaseId = 9001
        url = 'https://example.invalid/actions/runs/9001'
        status = 'completed'
        createdAt = '2026-07-19T00:00:31Z'
    }
)
$newRun = Wait-GameWipWorkflowRun `
    -WorkflowFile 'project-automation.yml' `
    -PreviousIds @('9001') `
    -DispatchedAfter $dispatchTime
Assert-GameWipRunId -Run $newRun -ExpectedId '9002'

$script:WorkflowRunResponses = @(
    [pscustomobject]@{
        databaseId = 9003
        url = 'https://example.invalid/actions/runs/9003'
        status = 'queued'
        createdAt = '2026-07-19T00:00:20Z'
    }
)
$staleRun = Wait-GameWipWorkflowRun `
    -WorkflowFile 'project-automation.yml' `
    -PreviousIds @() `
    -DispatchedAfter $dispatchTime
if ($null -ne $staleRun)
{
    throw 'A workflow run older than the dispatch window was incorrectly correlated.'
}

$script:WorkflowRunResponses = @(
    [pscustomobject]@{
        databaseId = 9004
        url = 'https://example.invalid/actions/runs/9004'
        status = 'queued'
        createdAt = '2026-07-19T00:01:00Z'
    },
    [pscustomobject]@{
        databaseId = 9005
        url = 'https://example.invalid/actions/runs/9005'
        status = 'queued'
        createdAt = '2026-07-19T00:01:01Z'
    }
)

$ambiguityRejected = $false
try
{
    $null = Wait-GameWipWorkflowRun `
        -WorkflowFile 'project-automation.yml' `
        -PreviousIds @() `
        -DispatchedAfter $dispatchTime
}
catch
{
    if ($_.Exception.Message -notmatch 'correlation is ambiguous')
    {
        throw
    }
    $ambiguityRejected = $true
}

if (-not $ambiguityRejected)
{
    throw 'Ambiguous workflow-run correlation did not fail closed.'
}

$script:WorkflowRunResponses = @()
$missingRun = Wait-GameWipWorkflowRun `
    -WorkflowFile 'project-automation.yml' `
    -PreviousIds @() `
    -DispatchedAfter $dispatchTime
if ($null -ne $missingRun)
{
    throw 'Expected a null result when no workflow run becomes visible.'
}

Write-Host 'Workflow helper regression tests passed.'
