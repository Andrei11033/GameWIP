[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'

. $helperPath -Action help *> $null

Assert-GameWipCommandConfig

$profileIds = @($CommandConfig.BenchmarkProfiles | ForEach-Object { $_.Id })
foreach ($requiredProfile in @('quick', 'standard', 'stable'))
{
    if ($profileIds -notcontains $requiredProfile) { throw "Missing benchmark profile '$requiredProfile'." }
}

$testCommand = Get-ProjectCommand -Id 'test-all'
if (-not [bool]$testCommand.AcceptsExtraArgs) { throw 'The test-all project command must accept focused runner arguments.' }

$argumentRejected = $false
try
{
    Assert-BenchmarkExtraArguments -Arguments @('--benchmark_out=unexpected.json')
}
catch
{
    $argumentRejected = $true
}
if (-not $argumentRejected) { throw 'Expected managed benchmark output arguments to be rejected.' }

$testRoot = Join-Path $repositoryRoot (Join-Path 'build\gamewip-temp\project-helper-tests' ([guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
try
{
    $baselinePath = Join-Path $testRoot 'baseline.json'
    $candidatePath = Join-Path $testRoot 'candidate.json'
    @{
        benchmarks = @(
            @{ name = 'BM_Example'; run_name = 'BM_Example'; run_type = 'iteration'; cpu_time = 100; real_time = 200; time_unit = 'ns' }
        )
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
    @{
        benchmarks = @(
            @{ name = 'BM_Example'; run_name = 'BM_Example'; run_type = 'iteration'; cpu_time = 120; real_time = 180; time_unit = 'ns' }
        )
    } | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $candidatePath -Encoding UTF8

    $baselineRows = Get-BenchmarkComparisonRows -Path $baselinePath
    $candidateRows = Get-BenchmarkComparisonRows -Path $candidatePath
    if ([double]$baselineRows['BM_Example'].CpuNanoseconds -ne 100.0) { throw 'Baseline benchmark parsing failed.' }
    if ([double]$candidateRows['BM_Example'].RealNanoseconds -ne 180.0) { throw 'Candidate benchmark parsing failed.' }

    $run = New-GameWipToolRun -RepositoryRoot $testRoot -RunLogRoot 'runs' -Tool 'test' -Action 'sample action'
    $step = New-GameWipToolRunStep -Run $run -Name 'sample-step' -CommandLine 'sample.exe --flag'
    'sample output' | Set-Content -LiteralPath $step.LogPath -Encoding UTF8
    Complete-GameWipToolRunStep -Run $run -Step $step -ExitCode 0
    $artifact = Join-Path $run.Artifacts 'result.json'
    '{}' | Set-Content -LiteralPath $artifact -Encoding UTF8
    Add-GameWipToolRunOutput -Run $run -Kind 'test-result' -Path $artifact
    $summary = Save-GameWipToolRun -Run $run -Status 'passed'
    foreach ($requiredFile in @($summary, (Join-Path $run.Root 'summary.json'), (Join-Path $run.Root 'manifest.json')))
    {
        if (-not (Test-Path -LiteralPath $requiredFile)) { throw "Tool-run output was not created: $requiredFile" }
    }
    $manifest = Get-Content -Raw -LiteralPath (Join-Path $run.Root 'manifest.json') | ConvertFrom-Json
    if ($manifest.status -ne 'passed' -or @($manifest.steps).Count -ne 1 -or @($manifest.outputs).Count -ne 1)
    {
        throw 'Tool-run manifest does not describe the completed run.'
    }
}
finally
{
    $resolvedTestRoot = [IO.Path]::GetFullPath($testRoot)
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build\gamewip-temp\project-helper-tests')).TrimEnd('\') + '\'
    if ($resolvedTestRoot.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedTestRoot))
    {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}

Write-Host 'Project helper regression tests passed.'
