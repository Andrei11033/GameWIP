[CmdletBinding()]
param(
    [ValidateSet('menu', 'doctor', 'git', 'workflow', 'unicode', 'format', 'quality', 'tools', 'links', 'configure', 'build', 'test', 'module', 'wizard', 'stress', 'run', 'bundle', 'docs', 'analysis', 'analyze', 'coverage', 'asan', 'benchmark', 'list', 'help')]
    [string]$Action = 'menu',
    [string]$Preset,
    [string]$Module,
    [string]$ProjectCommand,
    [string]$Bundle,
    [ValidateSet('menu', 'status', 'fetch', 'switch', 'update', 'cleanup', 'create', 'push', 'log')]
    [string]$GitAction = 'menu',
    [string]$GitBranch,
    [ValidateSet('menu', 'list', 'status', 'run')]
    [string]$WorkflowAction = 'menu',
    [ValidateSet('menu', 'status', 'verify', 'regenerate')]
    [string]$UnicodeAction = 'menu',
    [string]$PythonPath,
    [string]$ClangFormatPath,
    [ValidateSet('check', 'apply')]
    [string]$FormatAction = 'check',
    [ValidateSet('check', 'fix')]
    [string]$QualityAction = 'check',
    [ValidateSet('list', 'status', 'check-updates', 'update')]
    [string]$ToolsAction = 'list',
    [string]$Tool,
    [ValidateSet('run', 'dry-run', 'list', 'compare')]
    [string]$BenchmarkAction = 'run',
    [string]$BenchmarkProfile = 'standard',
    [string]$Filter,
    [ValidateRange(0, 100000)]
    [int]$Repetitions = 0,
    [string]$MinTime,
    [string]$Output,
    [ValidateSet('json', 'csv')]
    [string]$OutputFormat = 'json',
    [switch]$AggregatesOnly,
    [switch]$NoBuild,
    [string]$Baseline,
    [string]$Candidate,
    [string]$UnicodeDataRoot,
    [switch]$RefreshUnicodeData,
    [string]$Workflow,
    [ValidateSet('all', 'issue', 'pull_request')]
    [string]$WorkflowKind = 'all',
    [int]$WorkflowNumber = 0,
    [string]$ReleaseCommit,
    [ValidateRange(1, 100000)]
    [int]$Count = 0,
    [ValidateRange(1, 256)]
    [int]$Parallel = 0,
    [string[]]$ExtraArgs = @(),
    [switch]$BuildIfMissing,
    [switch]$NoWorkspaceTemp,
    [switch]$Preview
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ProjectConfigPath = Join-Path $PSScriptRoot 'config\project.json'
$CommandConfigPath = Join-Path $PSScriptRoot 'config\commands.json'
$ProjectToolsPath = Join-Path $PSScriptRoot 'config\project-tools.json'
$PresetsPath = Join-Path $RepositoryRoot 'CMakePresets.json'

$libraryFiles = @(
    'Config.ps1', 'Storage.ps1', 'Native.ps1', 'ToolRuns.ps1', 'Git.ps1', 'Workflows.ps1', 'Unicode.ps1', 'Formatting.ps1',
    'Quality.ps1', 'Tools.ps1', 'Build.ps1', 'Testing.ps1', 'Benchmarks.ps1', 'Documentation.ps1', 'Analysis.ps1', 'Bundles.ps1', 'Help.ps1'
)
foreach ($libraryFile in $libraryFiles)
{
    . (Join-Path $PSScriptRoot (Join-Path 'lib' $libraryFile))
}

$ProjectConfig = Read-GameWipJsonConfig -Path $ProjectConfigPath -Name 'project' -SchemaPath (Join-Path $PSScriptRoot 'schemas\project.schema.json')
$CommandConfig = Read-GameWipJsonConfig -Path $CommandConfigPath -Name 'commands' -SchemaPath (Join-Path $PSScriptRoot 'schemas\commands.schema.json')
$ProjectTools = Read-GameWipJsonConfig -Path $ProjectToolsPath -Name 'project tools' -SchemaPath (Join-Path $PSScriptRoot 'schemas\project-tools.schema.json')
$PresetData = Get-Content -Raw -LiteralPath $PresetsPath | ConvertFrom-Json

# These public parameters and shared values are consumed by dot-sourced action
# implementations; explicit references keep per-file static analysis accurate.
$null = $PythonPath, $ClangFormatPath, $UnicodeDataRoot, $RefreshUnicodeData
$null = $WorkflowKind, $WorkflowNumber, $ReleaseCommit, $NoWorkspaceTemp
$null = $ProjectConfig, $ProjectTools, $PresetData

$Script:RunRoot = $null
$Script:RunContext = $null
$Script:RunLabel = $Action
$Script:RunFailed = $false
$Script:OperationTemp = $null
try
{
    Assert-GameWipProjectConfig
    Assert-GameWipCommandConfig
    Assert-GameWipProjectToolConfig
    $Script:OperationTemp = Initialize-GameWipOperationTemp
    switch ($Action)
    {
        'menu'
        {
            Show-GameWipMenu
        }
        'doctor'
        {
            Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
        }
        'git'
        {
            Invoke-GameWipGitAction -Name $GitAction -BranchName $GitBranch
        }
        'workflow'
        {
            Invoke-GameWipWorkflowAction -Name $WorkflowAction -WorkflowId $Workflow
        }
        'unicode'
        {
            Invoke-GameWipUnicodeAction -Name $UnicodeAction
        }
        'format'
        {
            Invoke-GameWipFormat -Mode $FormatAction
        }
        'quality'
        {
            Invoke-GameWipQuality -Mode $QualityAction
        }
        'tools'
        {
            Invoke-GameWipToolAction -Name $ToolsAction -ToolId $Tool -PreviewOnly:$Preview
        }
        'links'
        {
            Invoke-GameWipMarkdownLink
        }
        'configure'
        {
            if ([string]::IsNullOrWhiteSpace($Preset))
            {
                $Preset = $CommandConfig.DefaultConfigurePreset
            }
            Invoke-GameWipConfigurePreset -Name $Preset
            Write-GameWipNextStepHint "build it with: .\gamewip.bat build -Preset $Preset"
        }
        'build'
        {
            if ([string]::IsNullOrWhiteSpace($Preset))
            {
                $Preset = $CommandConfig.DefaultBuildPreset
            }
            Invoke-GameWipBuildPreset -Name $Preset
            if ((Get-GameWipVisiblePresetName -Kind 'test') -contains $Preset)
            {
                Write-GameWipNextStepHint "run tests with: .\gamewip.bat test -Preset $Preset"
            }
            elseif ($Preset -eq 'benchmark')
            {
                Write-GameWipNextStepHint 'run performance measurements with: .\gamewip.bat benchmark; use -BenchmarkAction dry-run for registration only.'
            }
        }
        'test'
        {
            if ([string]::IsNullOrWhiteSpace($Preset))
            {
                $Preset = $CommandConfig.DefaultTestPreset
            }
            Invoke-GameWipTestPreset -Name $Preset -UseWorkspaceTemp
            Write-GameWipNextStepHint 'run a focused command with: .\gamewip.bat wizard'
        }
        'wizard'
        {
            Invoke-GameWipValidationCommandWizard
        }
        'module'
        {
            if ([string]::IsNullOrWhiteSpace($Module))
            {
                $Module = $CommandConfig.DefaultModule
            }
            Invoke-GameWipValidationModule -Name $Module -Arguments $ExtraArgs -ForceBuild:$BuildIfMissing
            Write-GameWipNextStepHint "stress this module with: .\gamewip.bat stress -Module $Module -Count 100 -Parallel 16"
        }
        'stress'
        {
            if ([string]::IsNullOrWhiteSpace($Module))
            {
                $Module = $CommandConfig.DefaultModule
            }
            if ($Count -le 0)
            {
                $Count = [int]$CommandConfig.DefaultStressCount
            }
            if ($Parallel -le 0)
            {
                $Parallel = [int]$CommandConfig.DefaultStressParallel
            }
            Invoke-GameWipStressModule -Name $Module -RunCount $Count -MaxParallel $Parallel -Arguments $ExtraArgs -ForceBuild:$BuildIfMissing
        }
        'run'
        {
            if ([string]::IsNullOrWhiteSpace($ProjectCommand))
            {
                $ProjectCommand = 'benchmark-dry-run'
            }
            $Script:RunLabel = "command-$ProjectCommand"
            Invoke-GameWipProjectCommand -Id $ProjectCommand -Arguments $ExtraArgs -ForceBuild:$BuildIfMissing
        }
        'bundle'
        {
            if ([string]::IsNullOrWhiteSpace($Bundle))
            {
                $Bundle = 'quick'
            }
            $Script:RunLabel = "bundle-$Bundle"
            Invoke-GameWipBundle -Id $Bundle
        }
        'docs'
        {
            Invoke-GameWipConfigurePreset -Name 'docs'
            Invoke-GameWipBuildPreset -Name 'docs'
        }
        'analysis'
        {
            Invoke-GameWipConfigurePreset -Name 'analyze'
            Invoke-GameWipBuildPreset -Name 'analyze'
        }
        'analyze'
        {
            Invoke-GameWipConfigurePreset -Name 'analyze'
            Invoke-GameWipBuildPreset -Name 'analyze'
        }
        'coverage'
        {
            Invoke-GameWipConfigurePreset -Name 'coverage'
            Invoke-GameWipBuildPreset -Name 'coverage'
            Invoke-GameWipTestPreset -Name 'coverage' -UseWorkspaceTemp
            Invoke-GameWipBuildTarget -Name 'coverage' -Target 'coverage'
        }
        'asan'
        {
            Invoke-GameWipConfigurePreset -Name 'asan'
            Invoke-GameWipBuildPreset -Name 'asan'
            Invoke-GameWipTestPreset -Name 'asan' -UseWorkspaceTemp
        }
        'benchmark'
        {
            if ($BenchmarkAction -eq 'compare')
            {
                if ([string]::IsNullOrWhiteSpace($Baseline) -or [string]::IsNullOrWhiteSpace($Candidate))
                {
                    throw 'Benchmark comparison requires both -Baseline and -Candidate result paths.'
                }
                Invoke-GameWipBenchmarkComparison -BaselinePath $Baseline -CandidatePath $Candidate -RequestedOutput $Output
            }
            else
            {
                Invoke-GameWipBenchmark `
                    -Mode $BenchmarkAction `
                    -ProfileId $BenchmarkProfile `
                    -NameFilter $Filter `
                    -RepeatCount $Repetitions `
                    -MinimumTime $MinTime `
                    -RequestedOutput $Output `
                    -Format $OutputFormat `
                    -OnlyAggregates:$AggregatesOnly `
                    -Arguments $ExtraArgs `
                    -SkipBuild:$NoBuild
            }
        }
        'list'
        {
            Show-GameWipProjectCatalog
        }
        'help'
        {
            Show-GameWipHelp
        }
    }
}
catch
{
    $Script:RunFailed = $true
    Show-GameWipActionFailure -ErrorRecord $_
    exit 1
}
finally
{
    Save-GameWipRunSummary
    Complete-GameWipOperationTemp
}
