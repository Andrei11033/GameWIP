[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '.')).Path
$helperPath = Join-Path $repositoryRoot 'scripts\GameWIP.ps1'

Write-Host "Sourcing GameWIP.ps1..."
. $helperPath -Action help *> $null

Write-Host "Calling Assert-GameWipCommandConfig..."
Assert-GameWipCommandConfig

Write-Host "Assert-GameWipCommandConfig completed successfully"
