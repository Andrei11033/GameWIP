[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'
$powerShellPath = (Get-Process -Id $PID).Path

. $helperPath -Action help *> $null

$helpOutput = (& $powerShellPath -NoProfile -ExecutionPolicy Bypass -File $helperPath -Action help 2>&1 | Out-String)
foreach ($requiredHelpText in @('.\gamewip.bat [action] [options]', 'links', 'coverage', 'quality', 'tools', '-NoWorkspaceTemp', '-WorkflowKind'))
{
    if ($helpOutput -notmatch [regex]::Escape($requiredHelpText))
    {
        throw "Project helper help omits '$requiredHelpText'."
    }
}

$actionValidateSet = @(
    (Get-Command -Name $helperPath).Parameters['Action'].Attributes |
        Where-Object { $_ -is [System.Management.Automation.ValidateSetAttribute] } |
        ForEach-Object { $_.ValidValues }
)
foreach ($action in $actionValidateSet)
{
    if ($helpOutput -notmatch "(?m)(^|[^A-Za-z0-9_-])$([regex]::Escape($action))([^A-Za-z0-9_-]|$)")
    {
        throw "Project helper help omits action '$action'."
    }
}
foreach ($parameter in (Get-Command -Name $helperPath).Parameters.Keys)
{
    if ($parameter -eq 'Action' -or $parameter -in [System.Management.Automation.PSCmdlet]::CommonParameters)
    {
        continue
    }
    if ($helpOutput -notmatch [regex]::Escape("-$parameter"))
    {
        throw "Project helper help omits option '-$parameter'."
    }
}

Assert-GameWipCommandConfig

$profileIds = @($CommandConfig.BenchmarkProfiles | ForEach-Object { $_.Id })
foreach ($requiredProfile in @('quick', 'standard', 'stable'))
{
    if ($profileIds -notcontains $requiredProfile)
    {
        throw "Missing benchmark profile '$requiredProfile'."
    }
}

$testCommand = Get-GameWipProjectCommand -Id 'test-all'
if (-not [bool]$testCommand.AcceptsExtraArgs)
{
    throw 'The test-all project command must accept focused runner arguments.'
}

$argumentRejected = $false
try
{
    Assert-GameWipBenchmarkExtraArgument -Arguments @('--benchmark_out=unexpected.json')
}
catch
{
    $argumentRejected = $true
}
if (-not $argumentRejected)
{
    throw 'Expected managed benchmark output arguments to be rejected.'
}

$testRoot = Join-Path $repositoryRoot (Join-Path 'build\gamewip\temp\project-helper-tests' ([guid]::NewGuid().ToString('N')))
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

    $baselineRows = Get-GameWipBenchmarkComparisonRow -Path $baselinePath
    $candidateRows = Get-GameWipBenchmarkComparisonRow -Path $candidatePath
    if ([double]$baselineRows['BM_Example'].CpuNanoseconds -ne 100.0)
    {
        throw 'Baseline benchmark parsing failed.'
    }
    if ([double]$candidateRows['BM_Example'].RealNanoseconds -ne 180.0)
    {
        throw 'Candidate benchmark parsing failed.'
    }

    $run = Initialize-GameWipToolRun -RepositoryRoot $testRoot -RunLogRoot 'runs' -Tool 'test' -Action 'sample action'
    $step = Initialize-GameWipToolRunStep -Run $run -Name 'sample-step' -CommandLine 'sample.exe --flag'
    'sample output' | Set-Content -LiteralPath $step.LogPath -Encoding UTF8
    Complete-GameWipToolRunStep -Run $run -Step $step -ExitCode 0
    $artifact = Join-Path $run.Artifacts 'result.json'
    '{}' | Set-Content -LiteralPath $artifact -Encoding UTF8
    Add-GameWipToolRunOutput -Run $run -Kind 'test-result' -Path $artifact
    $summary = Save-GameWipToolRun -Run $run -Status 'passed'
    foreach ($requiredFile in @($summary, (Join-Path $run.Root 'summary.json'), (Join-Path $run.Root 'manifest.json')))
    {
        if (-not (Test-Path -LiteralPath $requiredFile))
        {
            throw "Tool-run output was not created: $requiredFile"
        }
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
    $expectedParent = [IO.Path]::GetFullPath((Join-Path $repositoryRoot 'build\gamewip\temp\project-helper-tests')).TrimEnd('\') + '\'
    if ($resolvedTestRoot.StartsWith($expectedParent, [StringComparison]::OrdinalIgnoreCase) -and (Test-Path -LiteralPath $resolvedTestRoot))
    {
        Remove-Item -LiteralPath $resolvedTestRoot -Recurse -Force
    }
}

Assert-GameWipProjectToolConfig
$firstRegisteredTool = $ProjectTools['tools'][0]
if ($firstRegisteredTool -isnot [hashtable])
{
    throw "Project tool registry changed type before provider tests: '$($firstRegisteredTool.GetType().FullName)' '$firstRegisteredTool'."
}
$testedProviders = @{}
for ($toolIndex = 0; $toolIndex -lt $ProjectTools['tools'].Count; ++$toolIndex)
{
    $registeredToolInfo = $ProjectTools['tools'][$toolIndex]
    if ($registeredToolInfo -isnot [hashtable])
    {
        throw "Tool registry yielded unexpected type '$($registeredToolInfo.GetType().FullName)'."
    }
    $providerKind = [string]$registeredToolInfo['provider']['kind']
    if ($testedProviders.ContainsKey($providerKind))
    {
        continue
    }
    $testedProviders[$providerKind] = $true
    foreach ($operation in @('Latest', 'Install'))
    {
        $providerFunction = Get-GameWipProviderFunction -Tool $registeredToolInfo -Operation $operation
        if ($null -eq (Get-Command $providerFunction -ErrorAction SilentlyContinue))
        {
            throw "Provider '$providerKind' does not implement '$operation'."
        }
    }
}

$exactTool = @{ versionPolicy = 'exact'; requiredVersion = '2.0.0' }
if ((Get-GameWipToolCompatibility -Tool $exactTool -Detected @{ Installed = $true; Version = '2.0.0' }) -ne 'compatible' -or
    (Get-GameWipToolCompatibility -Tool $exactTool -Detected @{ Installed = $true; Version = '2.0.1' }) -ne 'mismatch')
{
    throw 'Exact tool compatibility policy is incorrect.'
}
$minimumTool = @{ versionPolicy = 'minimum'; requiredVersion = '2.0.0' }
if ((Get-GameWipToolCompatibility -Tool $minimumTool -Detected @{ Installed = $true; Version = '2.1.0' }) -ne 'compatible' -or
    (Get-GameWipToolCompatibility -Tool $minimumTool -Detected @{ Installed = $true; Version = '1.9.0' }) -ne 'outdated')
{
    throw 'Minimum tool compatibility policy is incorrect.'
}

Initialize-GameWipStorage
foreach ($storagePath in @($ProjectConfig.storage.cache, $ProjectConfig.storage.state, $ProjectConfig.storage.temp, $ProjectConfig.storage.runs))
{
    if (-not (Test-Path -LiteralPath (Resolve-GameWipStoragePath -RelativePath $storagePath) -PathType Container))
    {
        throw "Storage initialization omitted '$storagePath'."
    }
}

$tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
$staleMarked = Join-Path $tempRoot ('stale-marked-' + [guid]::NewGuid().ToString('N'))
$staleUnmarked = Join-Path $tempRoot ('stale-unmarked-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $staleMarked, $staleUnmarked | Out-Null
'{"schemaVersion":1,"owner":"GameWIP","resource":"operation-temp"}' | Set-Content -LiteralPath (Join-Path $staleMarked '.gamewip-owned.json') -Encoding UTF8
(Get-Item -LiteralPath $staleMarked).LastWriteTimeUtc = (Get-Date).ToUniversalTime().AddDays(-2)
(Get-Item -LiteralPath $staleUnmarked).LastWriteTimeUtc = (Get-Date).ToUniversalTime().AddDays(-2)
try
{
    Invoke-GameWipStaleOperationTempCleanup
    if (Test-Path -LiteralPath $staleMarked)
    {
        throw 'Stale marked operation temp was not removed.'
    }
    if (-not (Test-Path -LiteralPath $staleUnmarked))
    {
        throw 'Stale unmarked operation temp was removed.'
    }
}
finally
{
    if (Test-Path -LiteralPath $staleUnmarked)
    {
        Remove-Item -LiteralPath $staleUnmarked -Recurse -Force
    }
}

$informationalPlan = @(Get-GameWipToolUpdatePlan -ToolId 'unicode')
if ($informationalPlan.Count -ne 1 -or $informationalPlan[0].Tool.id -ne 'unicode' -or $null -ne $informationalPlan[0].Latest)
{
    throw 'Informational tool update planning must remain read-only and report no latest version.'
}

function Assert-GameWipCleanTrackedTree
{
    throw 'simulated dirty tracked tree'
}
$cleanTreeProtected = $false
try
{
    Invoke-GameWipToolUpdate -ToolId 'unicode'
}
catch
{
    $cleanTreeProtected = $_.Exception.Message -eq 'simulated dirty tracked tree'
}
if (-not $cleanTreeProtected)
{
    throw 'A real tool update did not enforce clean tracked-tree protection before planning.'
}

Write-Host 'Project helper regression tests passed.'
