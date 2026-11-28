# Setup orchestration. The entry point loads this library and dispatches actions.

Set-StrictMode -Version Latest

$SetupRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))

# Read setup catalogs and resolve managed state.
$SetupActionConfig = Read-GameWipJsonConfig `
    -Path (Join-Path $SetupRoot 'config/setup.json') `
    -Name 'setup' `
    -SchemaPath (Join-Path $ScriptsRoot 'schemas/setup.schema.json')
$EditorConfig = Read-GameWipJsonConfig `
    -Path (Join-Path $SetupRoot 'config/editors.json') `
    -Name 'editors' `
    -SchemaPath (Join-Path $ScriptsRoot 'schemas/editors.schema.json')
$script:SetupStatePath = Join-Path (Get-GameWipStorageAreaPath -Area state) 'setup.json'
$ToolConfig = @{ MsysRoot = Get-GameWipMsys2Root }

# Load shared setup and editor functions.
foreach ($file in @('Common.ps1', 'State.ps1', 'Repository.ps1', 'Editor.ps1', 'Documentation.ps1', 'Tracy.ps1', 'Uninstall.ps1'))
{
    . (Join-Path $PSScriptRoot $file)
}

if (Test-GameWipWindowsHost)
{
    $windowsPlatformRoot = Join-Path $SetupRoot 'platform/windows'

    foreach ($file in @('Editor.ps1', 'Winget.ps1', 'Msys2.ps1', 'VisualStudio.ps1', 'Tracy.ps1', 'Uninstall.ps1'))
    {
        . (Join-Path $windowsPlatformRoot $file)
    }
}
elseif ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
{
    . (Join-Path $SetupRoot 'platform/linux/Editor.ps1')
    . (Join-Path $SetupRoot 'platform/linux/Linux.ps1')
    . (Join-Path $SetupRoot 'platform/linux/Tracy.ps1')
    . (Join-Path $SetupRoot 'platform/linux/Uninstall.ps1')
}

# ------------------------------------------------------------
# Load setup workflow modules
# ------------------------------------------------------------

$orchestrationModules = @(
    'Orchestration/Actions.ps1',
    'Orchestration/Steps.ps1',
    'Orchestration/Environment.ps1',
    'Orchestration/Plan.ps1',
    'Orchestration/Execution.ps1',
    'Orchestration/Menu.ps1'
)

foreach ($orchestrationModule in $orchestrationModules)
{
    . (Join-Path $PSScriptRoot $orchestrationModule)
}
