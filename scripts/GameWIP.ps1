# Project-helper entry point. Shared behavior lives under scripts/lib/.

# ------------------------------------------------------------
# Command-line contract and bootstrap
# ------------------------------------------------------------

[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [string]$Action = 'menu',
    [Parameter(Position = 1)][string]$Command,
    [Parameter(Position = 2)][string]$Target,
    [string]$PythonPath,
    [string]$PythonHostPath,
    [string]$ClangFormatPath,
    [string]$UnicodeDataPath,
    [switch]$RefreshUnicodeData,
    [ValidateSet('all', 'issue', 'pull_request')][string]$WorkflowKind = 'all',
    [int]$ItemNumber = 0,
    [string]$ReleaseCommit,
    [string]$BenchmarkProfile = 'standard',
    [string]$NameFilter,
    [ValidateRange(0, 100000)][int]$Repetitions = 0,
    [string]$MinimumTime,
    [string]$OutputPath,
    [ValidateSet('json', 'csv')][string]$OutputFormat = 'json',
    [switch]$AggregatesOnly,
    [string]$BaselinePath,
    [string]$CandidatePath,
    [Parameter(DontShow = $true)][Alias('Output')][string]$RetiredOutput,
    [Parameter(DontShow = $true)][Alias('Baseline')][string]$RetiredBaseline,
    [Parameter(DontShow = $true)][Alias('Candidate')][string]$RetiredCandidate,
    [ValidateRange(1, 100000)][int]$RunCount = 0,
    [ValidateRange(1, 256)][int]$WorkerCount = 0,
    [string[]]$PassThroughArgs = @(),
    [switch]$SkipBuild,
    [switch]$CleanBuild,
    [switch]$Offline,
    [switch]$FailFast,
    [switch]$ChangedOnly,
    [switch]$FailOnFindings,
    [switch]$Json,
    [switch]$UseCallerTemp,
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

foreach ($retiredOption in @(
        @{ Name = 'Output'; Bound = $PSBoundParameters.ContainsKey('RetiredOutput') -or $PSBoundParameters.ContainsKey('Output') },
        @{ Name = 'Baseline'; Bound = $PSBoundParameters.ContainsKey('RetiredBaseline') -or $PSBoundParameters.ContainsKey('Baseline') },
        @{ Name = 'Candidate'; Bound = $PSBoundParameters.ContainsKey('RetiredCandidate') -or $PSBoundParameters.ContainsKey('Candidate') }
    ))
{
    if ($retiredOption.Bound)
    {
        throw "Option '-$($retiredOption.Name)' was renamed and is no longer accepted; use the canonical option shown by '.\gamewip.bat help'."
    }
}

# Keep the focused libraries' internal names stable while exposing the clearer
# public command-line vocabulary above.
$PythonProviderHostPath = $PythonHostPath
$UnicodeDataRoot = $UnicodeDataPath
$WorkflowNumber = $ItemNumber
$Filter = $NameFilter
$MinTime = $MinimumTime
$Output = $OutputPath
$Baseline = $BaselinePath
$Candidate = $CandidatePath
$Count = $RunCount
$Parallel = $WorkerCount
$ExtraArgs = @($PassThroughArgs)
$NoBuild = $SkipBuild
$Fresh = $CleanBuild
$StopOnFailure = $FailFast
$Changed = $ChangedOnly
$Enforce = $FailOnFindings
$NoWorkspaceTemp = $UseCallerTemp

# ------------------------------------------------------------
# Early command validation and common exits
# ------------------------------------------------------------

if ($Action -in @('--help', '-h', '-?'))
{
    $Action = 'help'
}
$resolvedAction = Resolve-GameWipActionName -Name $Action
if ($null -eq $resolvedAction)
{
    Write-GameWipHost "Unknown project action '$Action'." -ForegroundColor Red
    Write-Host 'Run .\gamewip.bat list to see available actions.'
    exit 2
}
$Action = $resolvedAction

if ($Action -notin @('help', 'list', 'menu'))
{
    Assert-GameWipActionOptions -Action $Action -BoundParameters $PSBoundParameters
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
    -SuppressOutput:$Quiet `
    -ScriptBlock {
    # Dispatch remains in the entry point so the command-line contract is visible
    # in one place; feature behavior belongs to the focused library functions.
    switch ($Action)
    {
        # ------------------------------------------------------------
        # Navigation and repository operations
        # ------------------------------------------------------------

        'ready'
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
            elseif ($verb -eq 'verify')
            {
                Invoke-GameWipMutation -Summary 'Verify reproducible Unicode generated data.' -Risk local -Plan @('Verify/download pinned Unicode input in the owned cache.', 'Generate and format an operation-owned candidate.', 'Compare the candidate with the checked-in table.') -Body { Invoke-GameWipUnicodeAction -Name verify } | Out-Null
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
            if ($verb -notin @('check', 'fix', 'status', 'hygiene'))
            {
                throw "Unknown quality command '$verb'."
            }
            if ($verb -eq 'fix')
            {
                Invoke-GameWipMutation -Summary 'Validate quality tools, apply deterministic formatters, and normalize LF line endings.' -Risk tracked -Plan @('Validate every tool required by the quality workflow before mutation.', 'Apply deterministic formatter changes and normalize maintained text to LF.', 'Run all independent quality checks and aggregate failures.') -Body { Invoke-GameWipQuality -Mode fix -FailFast:$FailFast -Changed:$Changed } | Out-Null
            }
            elseif ($verb -eq 'status')
            {
                Show-GameWipQualityCoverageStatus
            }
            elseif ($verb -eq 'hygiene')
            {
                $selector = if ([string]::IsNullOrWhiteSpace($Target))
                {
                    [string]$HygieneConfig.DefaultProfile
                }
                else
                {
                    $Target
                }
                if ($selector -eq 'list')
                {
                    Show-GameWipHygieneList
                }
                elseif ($selector -eq 'status')
                {
                    Show-GameWipHygieneStatus
                }
                else
                {
                    $hygieneSelection = Get-GameWipHygieneSelection -Selector $selector
                    if (@($hygieneSelection.Checks | Where-Object Availability -eq available).Count -eq 0)
                    {
                        Invoke-GameWipHygieneAudit -Selector $selector -Enforce:$Enforce
                    }
                    else
                    {
                        Invoke-GameWipMutation -Summary "Run optional '$selector' repository-hygiene audit." -Risk local -Plan @('Resolve the selected providers and ensure their local analysis state.', 'Run available advisory checks and disclose planned checks.', 'Retain normalized evidence without editing tracked files.') -Body { Invoke-GameWipHygieneAudit -Selector $selector -Enforce:$Enforce } | Out-Null
                    }
                }
            }
            else
            {
                Invoke-GameWipQuality -Mode check -FailFast:$FailFast -Changed:$Changed
            }
        }
        'tool'
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
                throw "Unknown tool command '$verb'."
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

        # ------------------------------------------------------------
        # Configure, build, and validation operations
        # ------------------------------------------------------------

        'deps'
        {
            $verb = if ([string]::IsNullOrWhiteSpace($Command))
            {
                'check'
            }
            else
            {
                $Command
            }

            if ($verb -notin @('check', 'prepare'))
            {
                throw "Unknown deps command '$verb'. Use 'check' or 'prepare'."
            }

            if ($verb -eq 'prepare')
            {
                Invoke-GameWipDependencyPreparationOperation
            }
            else
            {
                Test-GameWipDependencyCache -ThrowOnFailure | Out-Null
            }
        }
        'config'
        {
            $preset = if ([string]::IsNullOrWhiteSpace($Command))
            {
                [string]$CommandConfig.DefaultConfigurePreset
            }
            else
            {
                $Command
            }

            $configurePlan = if ($Fresh)
            {
                @("Remove build/$preset completely.", "cmake --preset $preset")
            }
            else
            {
                @("cmake --preset $preset")
            }

            if ($Offline)
            {
                $configurePlan += 'Require the prepared dependency cache and disallow downloads.'
            }

            Invoke-GameWipMutation `
                -Summary "Configure preset '$preset'." `
                -Risk local `
                -Plan $configurePlan `
                -Body {
                Invoke-GameWipConfigurePreset `
                    -Name $preset `
                    -Fresh:$Fresh `
                    -Offline:$Offline
            } |
                Out-Null
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

            $buildPlan = if ($Fresh)
            {
                @(
                    "Remove build/$preset completely.",
                    'Configure the recreated preset.',
                    "cmake --build --preset $preset"
                )
            }
            else
            {
                @(
                    'Ensure configure prerequisite if absent.',
                    "cmake --build --preset $preset"
                )
            }

            if ($Offline)
            {
                $buildPlan += 'Require the prepared dependency cache and disallow downloads.'
            }

            Invoke-GameWipMutation `
                -Summary "Build preset '$preset'." `
                -Risk local `
                -Plan $buildPlan `
                -Body {
                Invoke-GameWipBuildPreset `
                    -Name $preset `
                    -Fresh:$Fresh `
                    -Offline:$Offline
            } |
                Out-Null
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
            $testPlan = if ($Fresh)
            {
                @("Remove build/$preset completely.", 'Configure and build the recreated preset.', "ctest --preset $preset --output-on-failure")
            }
            else
            {
                @('Ensure the preset build is current unless -SkipBuild is used.', "ctest --preset $preset --output-on-failure")
            }
            Invoke-GameWipMutation -Summary "Run CTest preset '$preset'." -Risk local -Plan $testPlan -Body { Invoke-GameWipTestPreset -Name $preset -UseWorkspaceTemp -NoBuild:$NoBuild -Fresh:$Fresh } | Out-Null
        }
        'wizard'
        {
            Invoke-GameWipValidationCommandWizard -NoBuild:$NoBuild
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
            Invoke-GameWipMutation -Summary "Run validation module '$module'." -Risk local -Plan @('Ensure the validation executable unless -SkipBuild is used.', 'Execute the selected correctness module.') -Body { Invoke-GameWipValidationModule -Name $module -Arguments $ExtraArgs -NoBuild:$NoBuild } | Out-Null
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
            Invoke-GameWipMutation -Summary "Stress validation module '$module'." -Risk local -Plan @('Ensure the validation executable unless -SkipBuild is used.', "Run up to $runs validation processes with at most $workers workers.") -Body { Invoke-GameWipStressModule -Name $module -RunCount $runs -MaxParallel $workers -Arguments $ExtraArgs -NoBuild:$NoBuild -StopOnFailure:$StopOnFailure } | Out-Null
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
            Invoke-GameWipMutation -Summary "Run project command '$id'." -Risk local -Plan @('Ensure its executable unless -SkipBuild is used.', 'Execute the cataloged project command.') -Body { Invoke-GameWipProjectCommand -Id $id -Arguments $ExtraArgs -NoBuild:$NoBuild } | Out-Null
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
            Invoke-GameWipMutation -Summary "Run bundle '$id'." -Risk local -Plan @('Recreate declared preset trees when required by the bundle or -CleanBuild.', 'Execute its declarative steps in order.') -Body { Invoke-GameWipBundle -Id $id -NoBuild:$NoBuild -Fresh:$Fresh } | Out-Null
        }

        # ------------------------------------------------------------
        # Documentation, analysis, and retained-run operations
        # ------------------------------------------------------------

        'doc'
        {
            Invoke-GameWipMutation -Summary 'Build generated documentation.' -Risk local -Plan @('Configure docs preset.', 'Build docs preset.') -Body { Invoke-GameWipConfigurePreset -Name docs; Invoke-GameWipBuildPreset -Name docs } | Out-Null
        }
        'analyze'
        {
            Invoke-GameWipMutation -Summary 'Run C++ static analysis.' -Risk local -Plan @('Configure analyze preset.', 'Build analyze preset.') -Body { Invoke-GameWipConfigurePreset -Name analyze; Invoke-GameWipBuildPreset -Name analyze } | Out-Null
        }
        'cov'
        {
            Invoke-GameWipMutation -Summary 'Run coverage validation from a clean build tree.' -Risk local -Plan @('Remove build/coverage completely.', 'Configure/build coverage.', 'Run CTest with new profile data.', 'Generate coverage target.') -Body { Invoke-GameWipConfigurePreset -Name coverage -Fresh; Invoke-GameWipBuildPreset -Name coverage; Invoke-GameWipTestPreset -Name coverage -UseWorkspaceTemp -NoBuild; Invoke-GameWipBuildTarget -Name coverage -Target coverage } | Out-Null
        }
        'asan'
        {
            Invoke-GameWipMutation -Summary 'Run AddressSanitizer validation from a clean build tree.' -Risk local -Plan @('Remove build/asan completely.', 'Configure/build asan.', 'Run CTest.') -Body { Invoke-GameWipConfigurePreset -Name asan -Fresh; Invoke-GameWipBuildPreset -Name asan; Invoke-GameWipTestPreset -Name asan -UseWorkspaceTemp -NoBuild } | Out-Null
        }
        'ubsan'
        {
            Invoke-GameWipMutation -Summary 'Run UndefinedBehaviorSanitizer validation from a clean build tree.' -Risk local -Plan @('Remove build/ubsan completely.', 'Configure/build ubsan.', 'Run CTest.') -Body { Invoke-GameWipConfigurePreset -Name ubsan -Fresh; Invoke-GameWipBuildPreset -Name ubsan; Invoke-GameWipTestPreset -Name ubsan -UseWorkspaceTemp -NoBuild } | Out-Null
        }
        'bench'
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
                throw "Unknown bench command '$verb'."
            }
            if ($verb -eq 'compare' -and ([string]::IsNullOrWhiteSpace($Baseline) -or [string]::IsNullOrWhiteSpace($Candidate)))
            {
                throw 'bench compare requires -BaselinePath and -CandidatePath.'
            }
            $benchmarkPlan = if ($verb -eq 'compare')
            {
                @('Read the baseline and candidate JSON results.', 'Write the retained or explicitly requested comparison result.')
            }
            else
            {
                @('Ensure the benchmark executable unless -SkipBuild is used.', "Execute benchmark action '$verb'.", 'Retain measurement output when the action produces it.')
            }
            Invoke-GameWipMutation -Summary "Run benchmark action '$verb'." -Risk local -Plan $benchmarkPlan -Body {
                if ($verb -eq 'compare')
                {
                    Invoke-GameWipBenchmarkComparison -BaselinePath $Baseline -CandidatePath $Candidate -RequestedOutput $Output
                }
                else
                {
                    Invoke-GameWipBenchmark -Mode $verb -ProfileId $BenchmarkProfile -NameFilter $Filter -RepeatCount $Repetitions -MinimumTime $MinTime -RequestedOutput $Output -Format $OutputFormat -OnlyAggregates:$AggregatesOnly -Arguments $ExtraArgs -SkipBuild:$NoBuild
                }
            } | Out-Null
        }
        'history'
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
                throw "Unknown history command '$verb'."
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
