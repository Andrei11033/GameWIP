# GameWIP Windows setup executable entry point. All reusable behavior lives under setup/lib/.

[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Action = 'menu',
    [string]$Branch,
    [switch]$NonInteractive,
    [switch]$Yes,
    [switch]$SkipDocs,
    [switch]$Preview,
    [switch]$Json,
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

$RepositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
. (Join-Path $PSScriptRoot '..\lib\Bootstrap.ps1') -RepositoryRoot $RepositoryRoot
. (Join-Path $PSScriptRoot 'lib\Orchestration.ps1')

if ($Action -in @('--help', '-h', '-?'))
{
    $Action = 'help'
}
$validActions = @($SetupActionConfig.Actions | ForEach-Object { [string]$_.Id })
if ($Action -notin $validActions)
{
    Write-GameWipHost "Unknown setup action '$Action'." -ForegroundColor Red
    Write-Host 'Run .\setup.bat list to see available actions.'
    exit 2
}
if ($Action -eq 'help')
{
    Show-GameWipSetupHelp
    exit 0
}
if ($Action -eq 'list')
{
    Show-GameWipSetupActionCatalog
    exit 0
}
if ($Action -eq 'menu')
{
    if ($NonInteractive)
    {
        Write-GameWipHost 'The setup menu cannot run with -NonInteractive. Choose an explicit setup action.' -ForegroundColor Red
        exit 2
    }
    Show-GameWipSetupMenu
    exit 0
}

try
{
    $result = Invoke-GameWipSetupOperation -SelectedAction $Action
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
}
catch
{
    Write-Error $_
    exit 1
}
