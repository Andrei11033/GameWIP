# Continue debugging - add more code from ProjectHelper.Tests.ps1

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'
$powerShellPath = (Get-Process -Id $PID).Path

Write-Host "1. Sourcing GameWIP.ps1..."
. $helperPath -Action help *> $null
Write-Host "1. Done"

Write-Host "2. Calling Assert-GameWipCommandConfig..."
Assert-GameWipCommandConfig
Write-Host "2. Done"

Write-Host "3. Creating test root and files..."
$testRoot = Join-Path $repositoryRoot (Join-Path 'build\gamewip\temp\project-helper-tests' ([guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

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
Write-Host "3. Done"

Write-Host "4. Testing tool run functions..."
$run = Initialize-GameWipToolRun -RepositoryRoot $testRoot -RunLogRoot 'runs' -Tool 'test' -Action 'sample action'
Write-Host "4.1 run=$run"
$step = Initialize-GameWipToolRunStep -Run $run -Name 'sample-step' -CommandLine 'sample.exe --flag'
Write-Host "4.2 step=$step"
Write-Host "4.3 step.LogPath=$($step.LogPath)"
'sample output' | Set-Content -LiteralPath $step.LogPath -Encoding UTF8
Write-Host "4. Done"

Write-Host "5. Calling Assert-GameWipProjectToolConfig..."
Assert-GameWipProjectToolConfig
Write-Host "5. Done"

Write-Host "All tests completed successfully"
