# Regression tests for setup metadata, consent, ownership, and orchestration boundaries.
[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repositoryRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$setupScript = Join-Path $repositoryRoot 'scripts\setup\Windows.ps1'
$actionConfig = Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot 'scripts\setup\config\setup.json') | ConvertFrom-Json
$actions = @($actionConfig.Actions)

$duplicates = @($actions.Id | Group-Object | Where-Object Count -gt 1)
if ($duplicates.Count -ne 0)
{
    throw "Duplicate setup action IDs: $($duplicates.Name -join ', ')."
}
$menuActions = @($actions | Where-Object { $null -ne $_.PSObject.Properties['key'] })
$duplicateKeys = @($menuActions.Key | Group-Object | Where-Object Count -gt 1)
if ($duplicateKeys.Count -ne 0)
{
    throw "Duplicate setup menu keys: $($duplicateKeys.Name -join ', ')."
}
foreach ($requiredAction in @('menu', 'full', 'check', 'update', 'repair', 'uninstall', 'tools', 'visual-studio', 'msys2', 'repository', 'profiler', 'editor', 'docs', 'list', 'help'))
{
    if (@($actions.Id) -notcontains $requiredAction)
    {
        throw "Missing setup action '$requiredAction'."
    }
}
foreach ($action in $actions)
{
    if ($action.risk -notin @('read-only', 'local', 'tracked', 'machine', 'destructive', 'remote'))
    {
        throw "Setup action '$($action.id)' has invalid risk metadata."
    }
    if ($action.mutation -notin @('none', 'local', 'tracked', 'machine', 'destructive', 'remote', 'conditional'))
    {
        throw "Setup action '$($action.id)' has invalid mutation metadata."
    }
}

# Bootstrap project internals without invoking the setup executable entrypoint.
. (Join-Path $repositoryRoot 'scripts\lib\Bootstrap.ps1') -RepositoryRoot $repositoryRoot
$SetupRoot = Join-Path $repositoryRoot 'scripts\setup'
$SetupActionConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config\setup.json') -Name setup -SchemaPath (Join-Path $repositoryRoot 'scripts\schemas\setup.schema.json')
$EditorConfig = Read-GameWipJsonConfig -Path (Join-Path $SetupRoot 'config\editors.json') -Name editors -SchemaPath (Join-Path $repositoryRoot 'scripts\schemas\editors.schema.json')
$script:SetupStatePath = Join-Path $repositoryRoot (Join-Path $ProjectConfig.storage.state 'setup.json')
$ToolConfig = @{ MsysRoot = [string]$ProjectConfig.managedEnvironment.msys2Root }
foreach ($file in @('Common.ps1', 'Msys2.ps1', 'Uninstall.ps1'))
{
    . (Join-Path $SetupRoot (Join-Path 'lib' $file))
}

# Setup is required to keep prompting policy separate from execution mode.
$windowsSource = Get-Content -Raw -LiteralPath $setupScript
if ($windowsSource -notmatch '\[switch\]\$Yes')
{
    throw 'Setup entrypoint does not expose explicit -Yes consent.'
}
if ($windowsSource -match '(?ms)if\s*\(\$NonInteractive\)\s*\{\s*return\s+\$true')
{
    throw 'Setup still treats -NonInteractive as mutation approval.'
}
if ($windowsSource -notmatch '\$OutputMode\s*=\s*''Stream''')
{
    throw 'Setup does not stream native output by default.'
}

$orchestrationSource = Get-Content -Raw -LiteralPath (Join-Path $SetupRoot 'lib\Orchestration.ps1')
if ($orchestrationSource -notmatch 'Invoke-GameWipOperation')
{
    throw 'Setup does not use the shared operation lifecycle.'
}
if ($orchestrationSource -notmatch 'Invoke-GameWipToolEnsure')
{
    throw 'Setup does not use the shared exact tool ensure path.'
}
if ($orchestrationSource -notmatch 'Uninstall owns its confirmation so inventory is always printed first')
{
    throw 'Uninstall inventory/consent ordering is not explicit.'
}

# Common ownership markers use the shared envelope and fail conservatively.
$marker = New-GameWipOwnershipMarker -Resource 'test-resource' -Origin created -Payload ([ordered]@{ sample = $true })
if ($marker.schemaVersion -ne 1 -or $marker.owner -ne 'GameWIP' -or $marker.resource -ne 'test-resource' -or $marker.origin -ne 'created')
{
    throw 'Ownership-marker envelope is incomplete.'
}

# Pacman retry remains bounded and reports each attempt.
$script:simulatedAttempts = 0
$script:simulatedFailures = 1
function Invoke-GameWipMsys2
{
    param([string]$MsysRoot, [string]$Command)
    $null = $MsysRoot, $Command
    ++$script:simulatedAttempts
    if ($script:simulatedFailures -gt 0)
    {
        --$script:simulatedFailures; throw 'simulated transient failure'
    }
}
$output = @(& { Invoke-GameWipMsys2PacmanWithRetry -MsysRoot 'C:\TestMsys2' -Command 'pacman -Syu --noconfirm' -MaxAttempts 3 -RetryDelaySeconds 0 } *>&1)
if ($script:simulatedAttempts -ne 2)
{
    throw "Pacman retry made $script:simulatedAttempts attempts instead of 2."
}
if (($output -join "`n") -notmatch 'attempt 2 of 3')
{
    throw 'Pacman retry feedback omitted the successful retry.'
}

# Destructive setup actions must never be consented by non-interactive mode alone.
$Script:OperationContext = New-GameWipOperationContext -Label 'setup-consent' -NonInteractive
$rejected = $false
try
{
    Confirm-GameWipMutation -Summary 'setup mutation' -Risk destructive -Plan @('remove test resource') | Out-Null
}
catch
{
    $rejected = $_.Exception.Data['GameWipCode'] -eq 'consent-required'
}
if (-not $rejected)
{
    throw 'Non-interactive setup implicitly authorized a destructive action.'
}
$Script:OperationContext = $null

Write-Host 'Setup helper regression tests passed.'
