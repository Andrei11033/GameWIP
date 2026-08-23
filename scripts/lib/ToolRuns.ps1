# GameWIP ToolRuns helper behavior. Dot-sourced by scripts/GameWIP.ps1.

Set-StrictMode -Version Latest

function ConvertTo-GameWipToolSafeName
{
    param([Parameter(Mandatory = $true)][string]$Text)

    $safe = $Text -replace '[^A-Za-z0-9_.-]+', '-'
    $safe = $safe.Trim('-')
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
        Steps = [System.Collections.Generic.List[object]]::new()
        Outputs = [System.Collections.Generic.List[object]]::new()
        Details = [ordered]@{}
    }
}

function Initialize-GameWipToolRunStep
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$CommandLine
    )

    $index = $Run.Steps.Count + 1
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
        [Parameter(Mandatory = $true)][int]$ExitCode
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

    $Run.Outputs.Add([pscustomobject]@{
            Kind = $Kind
            Path = [IO.Path]::GetFullPath($Path)
        }) | Out-Null
}

function Save-GameWipToolRun
{
    param(
        [Parameter(Mandatory = $true)]$Run,
        [ValidateSet('passed', 'failed', 'cancelled', 'running')]
        [string]$Status = 'passed'
    )

    $Run.Status = $Status
    $Run.FinishedAt = (Get-Date).ToUniversalTime().ToString('o')
    $failed = @($Run.Steps | Where-Object { $_.ExitCode -ne 0 })
    $summaryPath = Join-Path $Run.Root 'summary.txt'
    $jsonPath = Join-Path $Run.Root 'summary.json'
    $manifestPath = Join-Path $Run.Root 'manifest.json'

    $lines = [System.Collections.Generic.List[string]]::new()
    $lines.Add("GameWIP $($Run.Tool) summary") | Out-Null
    $lines.Add("Action: $($Run.Action)") | Out-Null
    $lines.Add("Status: $Status") | Out-Null
    $lines.Add("Started: $($Run.StartedAt)") | Out-Null
    $lines.Add("Finished: $($Run.FinishedAt)") | Out-Null
    $lines.Add("Repository: $($Run.Repository)") | Out-Null
    $lines.Add("Failed steps: $($failed.Count)") | Out-Null
    $lines.Add('') | Out-Null
    foreach ($step in $Run.Steps)
    {
        $stepStatus = if ($step.ExitCode -eq 0)
        {
            'PASS'
        }
        else
        {
            'FAIL'
        }
        $lines.Add(('{0} {1} exit={2} duration={3:N3}s log={4}' -f $stepStatus, $step.Name, $step.ExitCode, $step.DurationSeconds, $step.LogPath)) | Out-Null
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

    $lines | Set-Content -LiteralPath $summaryPath -Encoding UTF8
    @($Run.Steps) | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $jsonPath -Encoding UTF8
    [ordered]@{
        schemaVersion = $Run.SchemaVersion
        tool = $Run.Tool
        action = $Run.Action
        status = $Run.Status
        repository = $Run.Repository
        startedAt = $Run.StartedAt
        finishedAt = $Run.FinishedAt
        details = $Run.Details
        steps = @($Run.Steps)
        outputs = @($Run.Outputs)
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $manifestPath -Encoding UTF8

    return $summaryPath
}

function ConvertTo-GameWipSafeName
{
    param([Parameter(Mandatory = $true)][string]$Text)

    $safe = $Text -replace '[^A-Za-z0-9_.-]+', '_'
    if ([string]::IsNullOrWhiteSpace($safe))
    {
        return 'step'
    }
    $safe.Trim('_')
}

function Initialize-GameWipRunLog
{
    if ($null -ne $Script:RunRoot)
    {
        return
    }

    $Script:RunContext = Initialize-GameWipToolRun `
        -RepositoryRoot $RepositoryRoot `
        -RunLogRoot $ProjectConfig.storage.runs `
        -Tool 'project-tool' `
        -Action $Script:RunLabel
    $Script:RunRoot = $Script:RunContext.Root
    Write-Host "Tool run: $Script:RunRoot"
}

function Save-GameWipRunSummary
{
    if ($null -eq $Script:RunRoot)
    {
        return
    }

    $status = if ($Script:RunFailed)
    {
        'failed'
    }
    else
    {
        'passed'
    }
    $summaryPath = Save-GameWipToolRun -Run $Script:RunContext -Status $status
    Write-Host "Summary: $summaryPath"
}
