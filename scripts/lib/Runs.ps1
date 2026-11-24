# Retained run logs, step records, manifests, and receipts.

# ------------------------------------------------------------
# Run identity and receipt persistence
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function ConvertTo-GameWipToolSafeName
{
    param([Parameter(Mandatory = $true)][string]$Text)
    $safe = ($Text -replace '[^A-Za-z0-9_.-]+', '-').Trim('-')

    if ([string]::IsNullOrWhiteSpace($safe))
    {
        return 'run'
    }

    return $safe.ToLowerInvariant()
}

function Initialize-GameWipToolRun
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$RunLogRoot,
        [Parameter(Mandatory = $true)][string]$Tool,
        [Parameter(Mandatory = $true)][string]$Action
    )

    $timestamp = Get-Date -Format 'yyyy-MM-dd_HHmmss_fff'
    $safeAction = ConvertTo-GameWipToolSafeName -Text $Action
    $root = Join-Path $RunLogRoot "${timestamp}_${safeAction}"
    $logs = Join-Path $root 'logs'
    $artifacts = Join-Path $root 'artifacts'
    New-Item -ItemType Directory -Force -Path $logs, $artifacts | Out-Null

    [pscustomobject]@{
        # Run identity
        SchemaVersion = 1
        Tool = $Tool
        Action = $Action
        Repository = $RepositoryRoot

        # Storage and timing
        Root = $root
        Logs = $logs
        Artifacts = $artifacts
        StartedAt = (Get-Date).ToUniversalTime().ToString('o')
        FinishedAt = $null

        # Status and recorded work
        Status = 'running'
        MutationState = 'none'
        NextStepIndex = 1
        Steps = [System.Collections.Generic.List[object]]::new()
        Outputs = [System.Collections.Generic.List[object]]::new()

        # Operation results
        Failure = $null
        FinalizationFailure = $null
        Details = [ordered]@{}
        Changes = @()
        Preserved = @()
        Warnings = @()
        NextActions = @()
        Events = @()
    }
}

function ConvertTo-GameWipRunFailure
{
    param([Parameter(Mandatory = $true)][System.Management.Automation.ErrorRecord]$ErrorRecord)

    $exception = $ErrorRecord.Exception
    $code = if ($exception.Data.Contains('GameWipCode'))
    {
        [string]$exception.Data['GameWipCode']
    }
    else
    {
        'operation-failed'
    }
    $details = if ($exception.Data.Contains('GameWipDetails'))
    {
        [string]$exception.Data['GameWipDetails']
    }
    else
    {
        ''
    }
    $suggestedActions = if ($exception.Data.Contains('GameWipSuggestedActions'))
    {
        @($exception.Data['GameWipSuggestedActions'])
    }
    else
    {
        @()
    }
    $logPath = if ($exception.Data.Contains('GameWipLogPath'))
    {
        [string]$exception.Data['GameWipLogPath']
    }
    else
    {
        ''
    }

    return [pscustomobject]@{
        Code = $code
        Summary = $exception.Message
        Details = $details
        SuggestedActions = $suggestedActions
        LogPath = $logPath
    }
}

function Initialize-GameWipToolRunStep
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$CommandLine
    )

    $index = [int]$Run.NextStepIndex
    $Run.NextStepIndex = $index + 1
    $safeName = ConvertTo-GameWipToolSafeName -Text $Name
    [pscustomobject]@{
        # Step identity
        Index = $index
        Name = $Name
        CommandLine = $CommandLine

        # Timing and retained output
        StartedAt = (Get-Date).ToUniversalTime().ToString('o')
        Stopwatch = [System.Diagnostics.Stopwatch]::StartNew()
        LogPath = Join-Path $Run.Logs ('{0:D3}_{1}.log' -f $index, $safeName)
    }
}

function Complete-GameWipToolRunStep
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [Parameter(Mandatory = $true)]$Step,
        [Parameter(Mandatory = $true)][int]$ExitCode,
        [ValidateSet('passed', 'failed', 'cancelled', 'timed-out')][string]$Status = $(if ($ExitCode -eq 0)
            {
                'passed'
            }
            else
            {
                'failed'
            })
    )

    $Step.Stopwatch.Stop()
    $Run.Steps.Add([pscustomobject]@{
            Index = $Step.Index
            Name = $Step.Name
            CommandLine = $Step.CommandLine

            StartedAt = $Step.StartedAt
            FinishedAt = (Get-Date).ToUniversalTime().ToString('o')
            DurationSeconds = [Math]::Round($Step.Stopwatch.Elapsed.TotalSeconds, 3)

            ExitCode = $ExitCode
            Status = $Status
            LogPath = $Step.LogPath
        }) | Out-Null
}

function Add-GameWipToolRunOutput
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [Parameter(Mandatory = $true)][string]$Kind,
        [Parameter(Mandatory = $true)][string]$Path
    )
    $Run.Outputs.Add([pscustomobject]@{ Kind = $Kind; Path = [IO.Path]::GetFullPath($Path) }) | Out-Null
}

function ConvertTo-GameWipRunDocument
{
    param([Parameter(Mandatory = $true)]$Run)
    [ordered]@{
        # Run identity and status
        schemaVersion = $Run.SchemaVersion
        tool = $Run.Tool
        action = $Run.Action
        status = $Run.Status
        mutationState = $Run.MutationState
        repository = $Run.Repository

        # Timing and failures
        startedAt = $Run.StartedAt
        finishedAt = $Run.FinishedAt
        failure = $Run.Failure
        finalizationFailure = $Run.FinalizationFailure

        # Recorded work and operation results
        details = $Run.Details
        steps = @($Run.Steps)
        outputs = @($Run.Outputs)
        changes = @($Run.Changes)
        preserved = @($Run.Preserved)
        warnings = @($Run.Warnings)
        nextActions = @($Run.NextActions)
        events = @($Run.Events)
    }
}

function Save-GameWipToolRun
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [ValidateSet('passed', 'failed', 'cancelled', 'running')][string]$Status = 'passed'
    )

    $Run.Status = $Status
    $Run.FinishedAt = (Get-Date).ToUniversalTime().ToString('o')
    $failed = @($Run.Steps | Where-Object { $_.Status -in @('failed', 'timed-out') })
    $summaryPath = Join-Path $Run.Root 'summary.txt'
    $jsonPath = Join-Path $Run.Root 'summary.json'
    $manifestPath = Join-Path $Run.Root 'manifest.json'

    # Write the human-readable operation summary.
    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("GameWIP $($Run.Tool) summary") | Out-Null
    $lines.Add("Action: $($Run.Action)") | Out-Null
    $lines.Add("Status: $Status") | Out-Null
    $lines.Add("Mutation state: $($Run.MutationState)") | Out-Null
    $lines.Add("Started: $($Run.StartedAt)") | Out-Null
    $lines.Add("Finished: $($Run.FinishedAt)") | Out-Null
    $lines.Add("Repository: $($Run.Repository)") | Out-Null

    if ($null -ne $Run.Failure)
    {
        $lines.Add("Failure: [$($Run.Failure.Code)] $($Run.Failure.Summary)") | Out-Null

        if (-not [string]::IsNullOrWhiteSpace([string]$Run.Failure.Details))
        {
            $lines.Add("Failure details: $($Run.Failure.Details)") | Out-Null
        }
    }

    if ($null -ne $Run.FinalizationFailure)
    {
        $lines.Add("Finalization failure: [$($Run.FinalizationFailure.Code)] $($Run.FinalizationFailure.Summary)") | Out-Null

        if (-not [string]::IsNullOrWhiteSpace([string]$Run.FinalizationFailure.Details))
        {
            $lines.Add("Finalization details: $($Run.FinalizationFailure.Details)") | Out-Null
        }
    }

    $lines.Add("Failed process steps: $($failed.Count)") | Out-Null
    $lines.Add('') | Out-Null

    foreach ($step in $Run.Steps)
    {
        $marker = if ($step.Status -eq 'passed')
        {
            'PASS'
        }
        elseif ($step.Status -eq 'cancelled')
        {
            'CANCEL'
        }
        elseif ($step.Status -eq 'timed-out')
        {
            'TIMEOUT'
        }
        else
        {
            'FAIL'
        }
        $lines.Add((
                '{0} {1} exit={2} duration={3:N3}s log={4}' -f
                $marker,
                $step.Name,
                $step.ExitCode,
                $step.DurationSeconds,
                $step.LogPath
            )) | Out-Null
    }

    # Append operation changes, warnings, next steps, and retained outputs.
    foreach ($section in @(
            @{ Name = 'Changes'; Values = @($Run.Changes) },
            @{ Name = 'Preserved'; Values = @($Run.Preserved) },
            @{ Name = 'Warnings'; Values = @($Run.Warnings) },
            @{ Name = 'Next'; Values = @($Run.NextActions) }
        ))
    {
        if ($section.Values.Count -eq 0)
        {
            continue
        }
        $lines.Add('') | Out-Null
        $lines.Add("$($section.Name):") | Out-Null

        foreach ($value in $section.Values)
        {
            $lines.Add("  $value") | Out-Null
        }
    }

    if ($Run.Outputs.Count -ne 0)
    {
        $lines.Add('') | Out-Null
        $lines.Add('Outputs:') | Out-Null

        foreach ($output in $Run.Outputs)
        {
            $lines.Add("  $($output.Kind): $($output.Path)") | Out-Null
        }
    }

    Write-GameWipTextAtomic -Path $summaryPath -Content (($lines -join "`n") + "`n")

    # Persist structured summaries for tools and history views.
    $document = ConvertTo-GameWipRunDocument -Run $Run
    Write-GameWipJsonAtomic -Path $jsonPath -Value $document -Depth 12
    Write-GameWipJsonAtomic -Path $manifestPath -Value $document -Depth 12
    return $summaryPath
}

function Initialize-GameWipRunLog
{
    if ($null -eq $Script:OperationContext)
    {
        throw 'A run log can only be initialized inside a GameWIP operation.'
    }

    if ($null -ne $Script:OperationContext.Run)
    {
        $Script:RunContext = $Script:OperationContext.Run
        return
    }

    $runAction = if (
        $null -ne (Get-Variable -Name RunLabel -Scope Script -ErrorAction SilentlyContinue) -and
        -not [string]::IsNullOrWhiteSpace([string]$Script:RunLabel)
    )
    {
        [string]$Script:RunLabel
    }
    else
    {
        [string]$Script:OperationContext.Label
    }
    $Script:OperationContext.Run = Initialize-GameWipToolRun `
        -RepositoryRoot $RepositoryRoot `
        -RunLogRoot (Get-GameWipStorageAreaPath -Area runs) `
        -Tool 'project-tool' `
        -Action $runAction
    $Script:RunContext = $Script:OperationContext.Run
}
