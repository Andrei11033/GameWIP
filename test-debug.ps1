[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'

Write-Host "Repository root: $repositoryRoot"
Write-Host "Helper path: $helperPath"

Write-Host "Sourcing GameWIP.ps1..."
. $helperPath -Action help *> $null

Write-Host "GameWIP.ps1 sourced successfully"

Write-Host "Running first test..."
$helpOutput = (& powershell -NoProfile -ExecutionPolicy Bypass -File $helperPath -Action help 2>&1 | Out-String)
Write-Host "Help output captured"

Write-Host "Test completed successfully"
