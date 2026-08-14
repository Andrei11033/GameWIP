[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$setupScript = Join-Path $repositoryRoot 'scripts\setup\windows.ps1'
$actionConfig = Import-PowerShellDataFile (Join-Path $repositoryRoot 'scripts\setup\config\actions.psd1')
$actions = @($actionConfig.Actions)

$duplicates = @($actions | ForEach-Object { [string]$_.Id } | Group-Object | Where-Object { $_.Count -gt 1 })
if ($duplicates.Count -ne 0) { throw "Duplicate setup action IDs: $($duplicates.Name -join ', ')." }

$menuActions = @($actions | Where-Object { $_.ContainsKey('Key') })
$duplicateKeys = @($menuActions | ForEach-Object { [string]$_.Key } | Group-Object | Where-Object { $_.Count -gt 1 })
if ($duplicateKeys.Count -ne 0) { throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')." }

foreach ($requiredAction in @('menu', 'full', 'check', 'update', 'repair', 'uninstall', 'tools', 'visual-studio', 'msys2', 'repository', 'profiler', 'editor', 'docs', 'list', 'help'))
{
    if (@($actions | ForEach-Object { $_.Id }) -notcontains $requiredAction) { throw "Missing setup action '$requiredAction'." }
}

$output = @(& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $setupScript list 2>&1)
if ($LASTEXITCODE -ne 0) { throw "setup list failed: $($output -join [Environment]::NewLine)" }
foreach ($action in @($actions | Where-Object { $_.Id -notin @('menu') }))
{
    if (($output -join "`n") -notmatch [regex]::Escape([string]$action.Id)) { throw "setup list omitted '$($action.Id)'." }
}

Write-Host 'Setup helper regression tests passed.'
