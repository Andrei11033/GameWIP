# GameWIP Benchmarks helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Resolve-GameWipBenchmarkOutputPath
{
    param(
        [string]$RequestedPath,
        [Parameter(Mandatory = $true)][ValidateSet('json', 'csv')][string]$Format
    )

    Initialize-GameWipRunLog
    if ([string]::IsNullOrWhiteSpace($RequestedPath))
    {
        return Join-Path $Script:RunContext.Artifacts "benchmark-results.$Format"
    }

    $resolved = Resolve-GameWipRepositoryPath -Path $RequestedPath
    if ([string]::IsNullOrWhiteSpace([IO.Path]::GetExtension($resolved)))
    {
        $resolved = "$resolved.$Format"
    }
    $buildRoot = Join-Path $RepositoryRoot 'build'
    if ((Test-GameWipPathWithinRoot -Path $resolved -Root $RepositoryRoot) -and
        -not (Test-GameWipPathWithinRoot -Path $resolved -Root $buildRoot))
    {
        throw "Benchmark output inside the checkout must be under 'build'. Requested: $resolved"
    }
    $parent = Split-Path -Parent $resolved
    if (-not [string]::IsNullOrWhiteSpace($parent))
    {
        New-Item -ItemType Directory -Force -Path $parent | Out-Null
    }
    return $resolved
}

function Assert-GameWipBenchmarkExtraArgument
{
    param([string[]]$Arguments)

    $managedPrefixes = @(
        '--benchmark_filter',
        '--benchmark_repetitions',
        '--benchmark_min_time',
        '--benchmark_report_aggregates_only',
        '--benchmark_display_aggregates_only',
        '--benchmark_out',
        '--benchmark_out_format',
        '--benchmark_dry_run',
        '--benchmark_list_tests'
    )
    foreach ($argument in $Arguments)
    {
        if (@($managedPrefixes | Where-Object { $argument.StartsWith($_, [StringComparison]::Ordinal) }).Count -ne 0)
        {
            throw "Benchmark argument '$argument' is managed by a dedicated gamewip option."
        }
    }
}

function Invoke-GameWipBenchmark
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('run', 'dry-run', 'list')][string]$Mode,
        [Parameter(Mandatory = $true)][string]$ProfileId,
        [string]$NameFilter,
        [int]$RepeatCount,
        [string]$MinimumTime,
        [string]$RequestedOutput,
        [Parameter(Mandatory = $true)][ValidateSet('json', 'csv')][string]$Format,
        [switch]$OnlyAggregates,
        [string[]]$Arguments = @(),
        [switch]$SkipBuild
    )

    $Script:RunLabel = "benchmark-$Mode"
    Initialize-GameWipRunLog
    Assert-GameWipBenchmarkExtraArgument -Arguments $Arguments
    if (-not [string]::IsNullOrWhiteSpace($MinimumTime) -and $MinimumTime -notmatch '^(?:[0-9]+x|[0-9]+(?:\.[0-9]+)?s)$')
    {
        throw "Invalid benchmark minimum time '$MinimumTime'. Use a value such as '2s', '0.5s', or '100x'."
    }

    $benchmarkProfile = @($CommandConfig.BenchmarkProfiles | Where-Object { $_.Id -eq $ProfileId } | Select-Object -First 1)
    if ($benchmarkProfile.Count -eq 0)
    {
        throw "Unknown benchmark profile '$ProfileId'."
    }
    $command = Get-GameWipProjectCommand -Id 'benchmark-dry-run'
    if ($SkipBuild)
    {
        Initialize-GameWipProjectCommandBuild -Command $command -NoBuild
    }
    else
    {
        Invoke-GameWipConfigurePreset -Name 'benchmark'
        Invoke-GameWipBuildPreset -Name 'benchmark'
    }
    $executable = Resolve-GameWipProjectExecutable -Command $command
    $benchmarkArguments = [System.Collections.Generic.List[string]]::new()

    if (-not [string]::IsNullOrWhiteSpace($NameFilter))
    {
        $benchmarkArguments.Add("--benchmark_filter=$NameFilter") | Out-Null
    }
    $resultPath = $null
    switch ($Mode)
    {
        'dry-run'
        {
            $benchmarkArguments.Add('--benchmark_dry_run=true') | Out-Null
        }
        'list'
        {
            $benchmarkArguments.Add('--benchmark_list_tests=true') | Out-Null
        }
        'run'
        {
            $effectiveRepetitions = if ($RepeatCount -gt 0)
            {
                $RepeatCount
            }
            else
            {
                [int]$benchmarkProfile[0].Repetitions
            }
            $effectiveMinTime = if (-not [string]::IsNullOrWhiteSpace($MinimumTime))
            {
                $MinimumTime
            }
            else
            {
                [string]$benchmarkProfile[0].MinTime
            }
            $effectiveAggregates = $OnlyAggregates -or [bool]$benchmarkProfile[0].AggregatesOnly
            $benchmarkArguments.Add("--benchmark_repetitions=$effectiveRepetitions") | Out-Null
            $benchmarkArguments.Add("--benchmark_min_time=$effectiveMinTime") | Out-Null
            $benchmarkArguments.Add("--benchmark_report_aggregates_only=$($effectiveAggregates.ToString().ToLowerInvariant())") | Out-Null
            $benchmarkArguments.Add("--benchmark_display_aggregates_only=$($effectiveAggregates.ToString().ToLowerInvariant())") | Out-Null
            if ($benchmarkProfile[0].ContainsKey('RandomInterleaving') -and [bool]$benchmarkProfile[0].RandomInterleaving)
            {
                $benchmarkArguments.Add('--benchmark_enable_random_interleaving=true') | Out-Null
            }
            $resultPath = Resolve-GameWipBenchmarkOutputPath -RequestedPath $RequestedOutput -Format $Format
            $benchmarkArguments.Add("--benchmark_out=$resultPath") | Out-Null
            $benchmarkArguments.Add("--benchmark_out_format=$Format") | Out-Null
            $benchmarkArguments.Add("--benchmark_context=gamewip_profile=$ProfileId") | Out-Null
            $Script:RunContext.Details['benchmark'] = [ordered]@{
                mode = $Mode
                profile = $ProfileId
                filter = $NameFilter
                repetitions = $effectiveRepetitions
                minTime = $effectiveMinTime
                aggregatesOnly = $effectiveAggregates
                format = $Format
            }
        }
    }
    foreach ($argument in $Arguments)
    {
        $benchmarkArguments.Add($argument) | Out-Null
    }

    Invoke-GameWipNative -Name "benchmark-$Mode" -FilePath $executable -Arguments $benchmarkArguments.ToArray() -UseWorkspaceTemp
    if ($null -ne $resultPath)
    {
        Add-GameWipToolRunOutput -Run $Script:RunContext -Kind 'benchmark-results' -Path $resultPath
        Write-GameWipSemanticText -Object 'Benchmark results:' -Semantic Success -NoNewline
        Write-Host ' ' -NoNewline
        Write-GameWipSemanticText -Object $resultPath -Semantic Muted
    }
}

function ConvertTo-GameWipBenchmarkNanosecond
{
    param(
        [Parameter(Mandatory = $true)][double]$Value,
        [Parameter(Mandatory = $true)][string]$Unit
    )

    switch ($Unit)
    {
        'ns'
        {
            return $Value
        }
        'us'
        {
            return $Value * 1000.0
        }
        'ms'
        {
            return $Value * 1000000.0
        }
        's'
        {
            return $Value * 1000000000.0
        }
        default
        {
            throw "Unsupported benchmark time unit '$Unit'."
        }
    }
}

function Get-GameWipBenchmarkComparisonRow
{
    param([Parameter(Mandatory = $true)][string]$Path)

    if (-not (Test-Path -LiteralPath $Path))
    {
        throw "Benchmark result does not exist: $Path"
    }
    $document = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    if (-not $document.PSObject.Properties['benchmarks'])
    {
        throw "Benchmark result has no 'benchmarks' array: $Path"
    }
    $groups = @($document.benchmarks | Group-Object { if ($_.PSObject.Properties['run_name'])
            {
                $_.run_name
            }
            else
            {
                $_.name
            } })
    $rows = [ordered]@{}
    foreach ($group in $groups)
    {
        $mean = @($group.Group | Where-Object { $_.run_type -eq 'aggregate' -and $_.aggregate_name -eq 'mean' } | Select-Object -First 1)
        if ($mean.Count -ne 0)
        {
            $samples = @($mean)
        }
        else
        {
            $samples = @($group.Group | Where-Object { -not $_.PSObject.Properties['run_type'] -or $_.run_type -eq 'iteration' })
        }
        if ($samples.Count -eq 0)
        {
            continue
        }
        $cpuValues = @($samples | ForEach-Object { ConvertTo-GameWipBenchmarkNanosecond -Value ([double]$_.cpu_time) -Unit ([string]$_.time_unit) })
        $realValues = @($samples | ForEach-Object { ConvertTo-GameWipBenchmarkNanosecond -Value ([double]$_.real_time) -Unit ([string]$_.time_unit) })
        $rows[$group.Name] = [pscustomobject]@{
            Name = $group.Name
            CpuNanoseconds = ($cpuValues | Measure-Object -Average).Average
            RealNanoseconds = ($realValues | Measure-Object -Average).Average
        }
    }
    return $rows
}

function Invoke-GameWipBenchmarkComparison
{
    param(
        [Parameter(Mandatory = $true)][string]$BaselinePath,
        [Parameter(Mandatory = $true)][string]$CandidatePath,
        [string]$RequestedOutput
    )

    $Script:RunLabel = 'benchmark-compare'
    Initialize-GameWipRunLog
    $baselineResolved = Resolve-GameWipRepositoryPath -Path $BaselinePath
    $candidateResolved = Resolve-GameWipRepositoryPath -Path $CandidatePath
    $commandLine = "compare benchmark results '$baselineResolved' '$candidateResolved'"
    $step = Initialize-GameWipToolRunStep -Run $Script:RunContext -Name 'benchmark-compare' -CommandLine $commandLine
    try
    {
        $baselineRows = Get-GameWipBenchmarkComparisonRow -Path $baselineResolved
        $candidateRows = Get-GameWipBenchmarkComparisonRow -Path $candidateResolved
        $comparisons = [System.Collections.Generic.List[object]]::new()
        foreach ($name in @($baselineRows.Keys | Where-Object { $candidateRows.Contains($_) } | Sort-Object))
        {
            $before = $baselineRows[$name]
            $after = $candidateRows[$name]
            $cpuDelta = if ($before.CpuNanoseconds -eq 0)
            {
                $null
            }
            else
            {
                (($after.CpuNanoseconds / $before.CpuNanoseconds) - 1.0) * 100.0
            }
            $realDelta = if ($before.RealNanoseconds -eq 0)
            {
                $null
            }
            else
            {
                (($after.RealNanoseconds / $before.RealNanoseconds) - 1.0) * 100.0
            }
            $comparisons.Add([pscustomobject]@{
                    name = $name
                    baselineCpuNanoseconds = $before.CpuNanoseconds
                    candidateCpuNanoseconds = $after.CpuNanoseconds
                    cpuChangePercent = $cpuDelta
                    baselineRealNanoseconds = $before.RealNanoseconds
                    candidateRealNanoseconds = $after.RealNanoseconds
                    realChangePercent = $realDelta
                }) | Out-Null
        }
        if ($comparisons.Count -eq 0)
        {
            throw 'The benchmark result files have no matching benchmark names.'
        }

        $outputPath = if ([string]::IsNullOrWhiteSpace($RequestedOutput))
        {
            Join-Path $Script:RunContext.Artifacts 'benchmark-comparison.json'
        }
        else
        {
            Resolve-GameWipBenchmarkOutputPath -RequestedPath $RequestedOutput -Format 'json'
        }
        $comparisonDocument = [ordered]@{
            schemaVersion = 1
            baseline = $baselineResolved
            candidate = $candidateResolved
            comparisons = @($comparisons)
        }
        Write-GameWipJsonAtomic -Path $outputPath -Value $comparisonDocument -Depth 6
        $comparisons | Format-Table name, cpuChangePercent, realChangePercent -AutoSize
        Add-GameWipToolRunOutput -Run $Script:RunContext -Kind 'benchmark-comparison' -Path $outputPath
        Complete-GameWipToolRunStep -Run $Script:RunContext -Step $step -ExitCode 0
        Write-GameWipSemanticText -Object 'Benchmark comparison:' -Semantic Success -NoNewline
        Write-Host ' ' -NoNewline
        Write-GameWipSemanticText -Object $outputPath -Semantic Muted
    }
    catch
    {
        Complete-GameWipToolRunStep -Run $Script:RunContext -Step $step -ExitCode 1
        $Script:RunFailed = $true
        throw
    }
}
