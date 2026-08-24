# Copy from ProjectHelper.Tests.ps1 to find the error

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'
$powerShellPath = (Get-Process -Id $PID).Path

Write-Host "1. Sourcing GameWIP.ps1 with help action..."
. $helperPath -Action help *> $null
Write-Host "1. Done"

Write-Host "2. Calling Assert-GameWipCommandConfig..."
Assert-GameWipCommandConfig
Write-Host "2. Done"

Write-Host "3. Getting benchmark profiles..."
$profileIds = @($CommandConfig.BenchmarkProfiles | ForEach-Object { $_.Id })
foreach ($requiredProfile in @('quick', 'standard', 'stable'))
{
    if ($profileIds -notcontains $requiredProfile)
    {
        throw "Missing benchmark profile '$requiredProfile'."
    }
}
Write-Host "3. Done"

Write-Host "4. Getting test command..."
$testCommand = Get-GameWipProjectCommand -Id 'test-all'
if (-not [bool]$testCommand.AcceptsExtraArgs)
{
    throw 'The test-all project command must accept focused runner arguments.'
}
Write-Host "4. Done"

Write-Host "5. Testing benchmark arguments..."
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
Write-Host "5. Done"

Write-Host "6. Creating test root..."
$testRoot = Join-Path $repositoryRoot (Join-Path 'build\gamewip\temp\project-helper-tests' ([guid]::NewGuid().ToString('N')))
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null
Write-Host "6. Done. testRoot=$testRoot"

Write-Host "7. Creating baseline path..."
$baselinePath = Join-Path $testRoot 'baseline.json'
$candidatePath = Join-Path $testRoot 'candidate.json'
Write-Host "7. Done. baselinePath=$baselinePath"

Write-Host "8. Writing baseline JSON..."
@{
    benchmarks = @(
        @{ name = 'BM_Example'; run_name = 'BM_Example'; run_type = 'iteration'; cpu_time = 100; real_time = 200; time_unit = 'ns' }
    )
} | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $baselinePath -Encoding UTF8
Write-Host "8. Done"

Write-Host "Test completed successfully"
