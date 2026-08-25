# GameWIP retained run logs, step records, manifests, and receipts.

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
    $root = Join-Path $RepositoryRoot (Join-Path $RunLogRoot "${timestamp}_${safeAction}")
    $logs = Join-Path $root 'logs'
    $artifacts = Join-Path $root 'artifacts'
    New-Item -ItemType Directory -Force -Path $logs, $artifacts | Out-Null

    [pscustomobject]@{
        SchemaVersion = 1
        Tool = $Tool
        Action = $Action
        Repository = $RepositoryRoot
        Root = $root
        Logs = $logs
        Artifacts = $artifacts
        StartedAt = (Get-Date).ToUniversalTime().ToString('o')
        FinishedAt = $null
        Status = 'running'
        MutationState = 'none'
        NextStepIndex = 1
        Steps = [System.Collections.Generic.List[object]]::new()
        Outputs = [System.Collections.Generic.List[object]]::new()
        Details = [ordered]@{}
        Changes = @()
        Preserved = @()
        Warnings = @()
        NextActions = @()
        Events = @()
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
        Index = $index
        Name = $Name
        CommandLine = $CommandLine
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
        schemaVersion = $Run.SchemaVersion
        tool = $Run.Tool
        action = $Run.Action
        status = $Run.Status
        mutationState = $Run.MutationState
        repository = $Run.Repository
        startedAt = $Run.StartedAt
        finishedAt = $Run.FinishedAt
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

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("GameWIP $($Run.Tool) summary") | Out-Null
    $lines.Add("Action: $($Run.Action)") | Out-Null
    $lines.Add("Status: $Status") | Out-Null
    $lines.Add("Mutation state: $($Run.MutationState)") | Out-Null
    $lines.Add("Started: $($Run.StartedAt)") | Out-Null
    $lines.Add("Finished: $($Run.FinishedAt)") | Out-Null
    $lines.Add("Repository: $($Run.Repository)") | Out-Null
    $lines.Add("Failed steps: $($failed.Count)") | Out-Null
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
        $lines.Add(('{0} {1} exit={2} duration={3:N3}s log={4}' -f $marker, $step.Name, $step.ExitCode, $step.DurationSeconds, $step.LogPath)) | Out-Null
    }
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
    $runAction = if ($null -ne (Get-Variable -Name RunLabel -Scope Script -ErrorAction SilentlyContinue) -and -not [string]::IsNullOrWhiteSpace([string]$Script:RunLabel))
    {
        [string]$Script:RunLabel
    }
    else
    {
        [string]$Script:OperationContext.Label
    }
    $Script:OperationContext.Run = Initialize-GameWipToolRun `
        -RepositoryRoot $RepositoryRoot `
        -RunLogRoot $ProjectConfig.storage.runs `
        -Tool 'project-tool' `
        -Action $runAction
    $Script:RunContext = $Script:OperationContext.Run
}


function Get-GameWipRunRoot
{
    return Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.runs
}

function Get-GameWipRetainedRun
{
    $root = Get-GameWipRunRoot
    if (-not (Test-Path -LiteralPath $root))
    {
        return @()
    }
    $activeRunRoot = if ($null -ne $Script:OperationContext -and $null -ne $Script:OperationContext.Run)
    {
        [IO.Path]::GetFullPath([string]$Script:OperationContext.Run.Root)
    }
    else
    {
        ''
    }
    return @(Get-ChildItem -LiteralPath $root -Directory -ErrorAction SilentlyContinue |
            Where-Object { [IO.Path]::GetFullPath($_.FullName) -ne $activeRunRoot } |
            Sort-Object Name -Descending)
}

function Show-GameWipRunList
{
    param([switch]$All)
    Write-GameWipSection 'Retained runs'
    $items = @(Get-GameWipRetainedRun)
    if ($items.Count -eq 0)
    {
        Write-Host '  (none)'; return
    }
    $displayItems = if ($All)
    {
        $items
    }
    else
    {
        @($items | Select-Object -First 25)
    }
    if (-not $All -and $displayItems.Count -lt $items.Count)
    {
        Write-Host "  Showing the newest $($displayItems.Count) of $($items.Count) runs. Use 'gamewip runs list all' for complete history."
        Write-Host ''
    }
    $runWidth = [Math]::Min(60, [Math]::Max(38, [int](@($displayItems | ForEach-Object { $_.Name.Length }) | Measure-Object -Maximum).Maximum))
    $rowFormat = "  {0,-$runWidth} {1,-10} {2}"
    Write-Host ($rowFormat -f 'Run', 'Status', 'Action')
    Write-Host ($rowFormat -f ('-' * $runWidth), ('-' * 10), ('-' * 24))
    foreach ($item in $displayItems)
    {
        $manifest = Join-Path $item.FullName 'manifest.json'
        $status = 'incomplete'; $action = $item.Name
        if (Test-Path -LiteralPath $manifest -PathType Leaf)
        {
            try
            {
                $doc = Get-Content -Raw -LiteralPath $manifest | ConvertFrom-Json; $status = [string]$doc.status; $action = [string]$doc.action
            }
            catch
            {
                $null = $_.Exception
            }
        }
        $displayName = if ($item.Name.Length -le $runWidth)
        {
            $item.Name
        }
        else
        {
            $item.Name.Substring(0, $runWidth - 3) + '...'
        }
        Write-Host ($rowFormat -f $displayName, $status, $action)
    }
}

function Resolve-GameWipRunSelection
{
    param([Parameter(Mandatory = $true)][string]$Selector)
    $items = @(Get-GameWipRetainedRun)
    if ($items.Count -eq 0)
    {
        throw 'No retained GameWIP runs exist.'
    }
    if ($Selector -eq 'latest')
    {
        return $items[0]
    }
    $match = @($items | Where-Object { $_.Name -eq $Selector })
    if ($match.Count -ne 1)
    {
        throw "Unknown retained run '$Selector'."
    }
    return $match[0]
}

function Show-GameWipRun
{
    param([string]$Selector = 'latest')
    $run = Resolve-GameWipRunSelection -Selector $Selector
    $summary = Join-Path $run.FullName 'summary.txt'
    if (Test-Path -LiteralPath $summary)
    {
        Get-Content -LiteralPath $summary | ForEach-Object { Write-Host $_ }
    }
    else
    {
        Write-GameWipHost 'Retained run has no finalized receipt.' -ForegroundColor Yellow
        Write-Host "  Run:  $($run.FullName)"
        Write-Host "  Logs: $(Join-Path $run.FullName 'logs')"
    }
}

function Invoke-GameWipRunCleanup
{
    param([string]$Selector = 'all')
    $root = Get-GameWipRunRoot
    $targets = @(if ($Selector -eq 'all')
        {
            Get-GameWipRetainedRun
        }
        else
        {
            Resolve-GameWipRunSelection -Selector $Selector
        })
    if ($targets.Count -eq 0)
    {
        Write-Host 'No retained runs to clean.'; return
    }
    $plan = @($targets | ForEach-Object { "Remove retained run $($_.Name)" })
    if (-not (Confirm-GameWipMutation -Summary "Remove $($targets.Count) retained run(s)?" -Risk destructive -Plan $plan))
    {
        return
    }
    Set-GameWipMutationState partial
    foreach ($target in $targets)
    {
        Assert-GameWipSafeChildPath -Path $target.FullName -OwnedRoot $root | Out-Null
        Remove-Item -LiteralPath $target.FullName -Recurse -Force
        Add-GameWipOperationChange -Message "Removed retained run $($target.Name)"
    }
    Set-GameWipMutationState complete
}
