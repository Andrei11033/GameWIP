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

$script:WorkflowRunResponses = @(
    [pscustomobject]@{
        databaseId = 9001
        url = 'https://example.invalid/actions/runs/9001'
        status = 'queued'
        createdAt = '2026-07-19T00:00:00Z'
    }
)
$firstRun = Wait-GameWipWorkflowRun -WorkflowFile 'project-automation.yml' -PreviousIds @()
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
        createdAt = '2026-07-19T00:00:00Z'
    }
)
$newRun = Wait-GameWipWorkflowRun -WorkflowFile 'project-automation.yml' -PreviousIds @('9002')
Assert-GameWipRunId -Run $newRun -ExpectedId '9001'

$script:WorkflowRunResponses = @()
$missingRun = Wait-GameWipWorkflowRun -WorkflowFile 'project-automation.yml' -PreviousIds @()
if ($null -ne $missingRun)
{
    throw 'Expected a null result when no workflow run becomes visible.'
}

Write-Host 'Workflow helper regression tests passed.'
