# GameWIP project-helper executable entry point. Library/bootstrap code lives under scripts/lib/.

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Action = 'menu',
    [Parameter(Position = 1)][string]$Command,
    [Parameter(Position = 2)][string]$Target,
    [string]$PythonPath,
    [string]$PythonProviderHostPath,
    [string]$ClangFormatPath,
    [string]$UnicodeDataRoot,
    [switch]$RefreshUnicodeData,
    [ValidateSet('all', 'issue', 'pull_request')][string]$WorkflowKind = 'all',
    [int]$WorkflowNumber = 0,
    [string]$ReleaseCommit,
    [string]$BenchmarkProfile = 'standard',
    [string]$Filter,
    [ValidateRange(0, 100000)][int]$Repetitions = 0,
    [string]$MinTime,
    [string]$Output,
    [ValidateSet('json', 'csv')][string]$OutputFormat = 'json',
    [switch]$AggregatesOnly,
    [string]$Baseline,
    [string]$Candidate,
    [ValidateRange(1, 100000)][int]$Count = 0,
    [ValidateRange(1, 256)][int]$Parallel = 0,
    [string[]]$ExtraArgs = @(),
    [switch]$NoBuild,
    [switch]$StopOnFailure,
    [switch]$FailFast,
    [switch]$Changed,
    [switch]$Json,
    [switch]$NoWorkspaceTemp,
    [switch]$Preview,
    [switch]$NonInteractive,
    [switch]$Yes,
    [switch]$Quiet,
    [switch]$NoColor,
    [ValidateSet('Summary', 'Stream', 'LogOnly')][string]$OutputMode = 'Stream'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Quiet)
{
    $OutputMode = 'LogOnly'
}
$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
. (Join-Path $PSScriptRoot 'lib\Bootstrap.ps1') -RepositoryRoot $RepositoryRoot

if ($Action -in @('--help', '-h', '-?'))
{
    $Action = 'help'
}
$validActions = @('menu', 'doctor', 'git', 'workflow', 'unicode', 'format', 'quality', 'tools', 'links', 'configure', 'build', 'test', 'module', 'wizard', 'stress', 'run', 'bundle', 'docs', 'analyze', 'coverage', 'asan', 'benchmark', 'runs', 'list', 'help')
if ($Action -notin $validActions)
{
    Write-GameWipHost "Unknown project action '$Action'." -ForegroundColor Red
    Write-Host 'Run .\gamewip.bat list to see available actions.'
    exit 2
}

# Keep common PowerShell -Verbose semantics without inventing a parallel flag.
if ($PSBoundParameters.ContainsKey('Verbose') -and [bool]$PSBoundParameters.Verbose)
{
    $VerbosePreference = 'Continue'
}

if ($Action -eq 'help')
{
    Show-GameWipHelp; exit 0
}
if ($Action -eq 'list')
{
    Show-GameWipProjectCatalog; exit 0
}
if ($Action -eq 'menu')
{
    if ($NonInteractive)
    {
        Write-GameWipHost 'The interactive menu cannot run with -NonInteractive. Choose an explicit action.' -ForegroundColor Red
        exit 2
    }
    Show-GameWipMenu
    exit 0
}

$label = @(@($Action, $Command, $Target) | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }) -join '-'
$result = Invoke-GameWipOperation `
    -Label $label `
    -NonInteractive:$NonInteractive `
    -Yes:$Yes `
    -Preview:$Preview `
    -OutputMode $OutputMode `
    -NoColor:$NoColor `
    -SuppressReceipt:$Quiet `
    -ScriptBlock {
    switch ($Action)
    {
        'doctor'
        {
            Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
        }
        'git'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'status'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('status', 'fetch', 'switch', 'update', 'cleanup', 'create', 'push', 'log'))
            {
                throw "Unknown git command '$verb'."
            }
            Invoke-GameWipGitAction -Name $verb -BranchName $Target
        }
        'workflow'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'list'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('list', 'status', 'run'))
            {
                throw "Unknown workflow command '$verb'."
            }
            Invoke-GameWipWorkflowAction -Name $verb -WorkflowId $Target
        }
        'unicode'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'status'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('status', 'verify', 'regenerate'))
            {
                throw "Unknown unicode command '$verb'."
            }
            if ($verb -eq 'regenerate')
            {
                Invoke-GameWipMutation -Summary 'Regenerate the tracked Unicode property table.' -Risk tracked -Plan @('Verify/download pinned Unicode input.', 'Generate and format a candidate.', 'Replace the tracked table only if content changes.') -Body { Invoke-GameWipUnicodeAction -Name regenerate } | Out-Null
            }
            else
            {
                Invoke-GameWipUnicodeAction -Name $verb
            }
        }
        'format'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'check'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('check', 'apply'))
            {
                throw "Unknown format command '$verb'."
            }
            if ($verb -eq 'apply')
            {
                Invoke-GameWipMutation -Summary 'Apply repository C/C++ formatting.' -Risk tracked -Plan @('Rewrite maintained C/C++ files with the repository clang-format policy.') -Body { Invoke-GameWipFormat -Mode apply } | Out-Null
            }
            else
            {
                Invoke-GameWipFormat -Mode check
            }
        }
        'quality'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'check'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('check', 'fix', 'status'))
            {
                throw "Unknown quality command '$verb'."
            }
            if ($verb -eq 'fix')
            {
                Invoke-GameWipMutation -Summary 'Apply deterministic formatters, then run the quality gate.' -Risk tracked -Plan @('Apply deterministic formatter changes.', 'Run all independent quality checks and aggregate failures.') -Body { Invoke-GameWipQuality -Mode fix -FailFast:$FailFast -Changed:$Changed } | Out-Null
            }
            elseif ($verb -eq 'status')
            {
                Show-GameWipQualityCoverageStatus
            }
            else
            {
                Invoke-GameWipQuality -Mode check -FailFast:$FailFast -Changed:$Changed
            }
        }
        'tools'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'list'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('list', 'status', 'check-updates', 'ensure', 'update'))
            {
                throw "Unknown tools command '$verb'."
            }
            $toolId = if ([string]::IsNullOrWhiteSpace($Target))
            {
                'all'
            }
            else
            {
                $Target
            }
            if ($verb -eq 'list')
            {
                Show-GameWipToolList
            }
            elseif ($verb -eq 'status')
            {
                Show-GameWipToolStatus
            }
            elseif ($verb -eq 'check-updates')
            {
                Write-GameWipSection 'Upstream tool versions'
                Show-GameWipToolUpdatePlan -Plan @(Get-GameWipToolUpdatePlan -ToolId $toolId)
            }
            elseif ($verb -eq 'ensure')
            {
                Invoke-GameWipToolEnsure -Selector $toolId
            }
            else
            {
                Invoke-GameWipToolUpdate -ToolId $toolId -PreviewOnly:$Preview
            }
        }
        'links'
        {
            Invoke-GameWipMarkdownLink
        }
        'configure'
        {
            $preset = if ([string]::IsNullOrWhiteSpace($Command))
            {
                [string]$CommandConfig.DefaultConfigurePreset
            }
            else
            {
                $Command
            }
            Invoke-GameWipMutation -Summary "Configure preset '$preset'." -Risk local -Plan @("cmake --preset $preset") -Body { Invoke-GameWipConfigurePreset -Name $preset } | Out-Null
        }
        'build'
        {
            $preset = if ([string]::IsNullOrWhiteSpace($Command))
            {
                [string]$CommandConfig.DefaultBuildPreset
            }
            else
            {
                $Command
            }
            Invoke-GameWipMutation -Summary "Build preset '$preset'." -Risk local -Plan @('Ensure configure prerequisite if absent.', "cmake --build --preset $preset") -Body { Invoke-GameWipBuildPreset -Name $preset } | Out-Null
        }
        'test'
        {
            $preset = if ([string]::IsNullOrWhiteSpace($Command))
            {
                [string]$CommandConfig.DefaultTestPreset
            }
            else
            {
                $Command
            }
            Invoke-GameWipTestPreset -Name $preset -UseWorkspaceTemp -NoBuild:$NoBuild
        }
        'wizard'
        {
            Invoke-GameWipValidationCommandWizard
        }
        'module'
        {
            $module = if ([string]::IsNullOrWhiteSpace($Command))
            {
                [string]$CommandConfig.DefaultModule
            }
            else
            {
                $Command
            }
            Invoke-GameWipValidationModule -Name $module -Arguments $ExtraArgs -NoBuild:$NoBuild
        }
        'stress'
        {
            $module = if ([string]::IsNullOrWhiteSpace($Command))
            {
                [string]$CommandConfig.DefaultModule
            }
            else
            {
                $Command
            }
            $runs = if ($Count -gt 0)
            {
                $Count
            }
            else
            {
                [int]$CommandConfig.DefaultStressCount
            }
            $workers = if ($Parallel -gt 0)
            {
                $Parallel
            }
            else
            {
                [int]$CommandConfig.DefaultStressParallel
            }
            Invoke-GameWipStressModule -Name $module -RunCount $runs -MaxParallel $workers -Arguments $ExtraArgs -NoBuild:$NoBuild -StopOnFailure:$StopOnFailure
        }
        'run'
        {
            $id = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'benchmark-dry-run'
            }
            else
            {
                $Command
            }
            Invoke-GameWipProjectCommand -Id $id -Arguments $ExtraArgs -NoBuild:$NoBuild
        }
        'bundle'
        {
            $id = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'quick'
            }
            else
            {
                $Command
            }
            Invoke-GameWipMutation -Summary "Run bundle '$id'." -Risk local -Plan @('Execute its declarative steps in order.') -Body { Invoke-GameWipBundle -Id $id } | Out-Null
        }
        'docs'
        {
            Invoke-GameWipMutation -Summary 'Build generated documentation.' -Risk local -Plan @('Configure docs preset.', 'Build docs preset.') -Body { Invoke-GameWipConfigurePreset -Name docs; Invoke-GameWipBuildPreset -Name docs } | Out-Null
        }
        'analyze'
        {
            Invoke-GameWipMutation -Summary 'Run C++ static analysis.' -Risk local -Plan @('Configure analyze preset.', 'Build analyze preset.') -Body { Invoke-GameWipConfigurePreset -Name analyze; Invoke-GameWipBuildPreset -Name analyze } | Out-Null
        }
        'coverage'
        {
            Invoke-GameWipMutation -Summary 'Run coverage validation.' -Risk local -Plan @('Configure/build coverage.', 'Run CTest.', 'Generate coverage target.') -Body { Invoke-GameWipConfigurePreset -Name coverage; Invoke-GameWipBuildPreset -Name coverage; Invoke-GameWipTestPreset -Name coverage -UseWorkspaceTemp -NoBuild; Invoke-GameWipBuildTarget -Name coverage -Target coverage } | Out-Null
        }
        'asan'
        {
            Invoke-GameWipMutation -Summary 'Run AddressSanitizer validation.' -Risk local -Plan @('Configure/build asan.', 'Run CTest.') -Body { Invoke-GameWipConfigurePreset -Name asan; Invoke-GameWipBuildPreset -Name asan; Invoke-GameWipTestPreset -Name asan -UseWorkspaceTemp -NoBuild } | Out-Null
        }
        'benchmark'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'run'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('run', 'dry-run', 'list', 'compare'))
            {
                throw "Unknown benchmark command '$verb'."
            }
            if ($verb -eq 'compare')
            {
                if ([string]::IsNullOrWhiteSpace($Baseline) -or [string]::IsNullOrWhiteSpace($Candidate))
                {
                    throw 'benchmark compare requires -Baseline and -Candidate.'
                }
                Invoke-GameWipBenchmarkComparison -BaselinePath $Baseline -CandidatePath $Candidate -RequestedOutput $Output
            }
            else
            {
                Invoke-GameWipBenchmark -Mode $verb -ProfileId $BenchmarkProfile -NameFilter $Filter -RepeatCount $Repetitions -MinimumTime $MinTime -RequestedOutput $Output -Format $OutputFormat -OnlyAggregates:$AggregatesOnly -Arguments $ExtraArgs -SkipBuild:$NoBuild
            }
        }
        'runs'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'list'
            }
            else
            {
                $Command
            }
            if ($verb -notin @('list', 'show', 'clean'))
            {
                throw "Unknown runs command '$verb'."
            }
            if ($verb -eq 'list')
            {
                Show-GameWipRunList -All:($Target -eq 'all')
            }
            elseif ($verb -eq 'show')
            {
                Show-GameWipRun -Selector $(if ([string]::IsNullOrWhiteSpace($Target))
                    {
                        'latest'
                    }
                    else
                    {
                        $Target
                    })
            }
            else
            {
                Invoke-GameWipRunCleanup -Selector $(if ([string]::IsNullOrWhiteSpace($Target))
                    {
                        'all'
                    }
                    else
                    {
                        $Target
                    })
            }
        }
    }
}

if ($Json)
{
    $result | ConvertTo-Json -Depth 12 | Write-Output
}
if ($result.Status -eq 'cancelled')
{
    exit 130
}
if ($result.Status -ne 'passed')
{
    exit 1
}
exit 0
