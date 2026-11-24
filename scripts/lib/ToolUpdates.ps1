# Tool update and ensure workflows.

Set-StrictMode -Version Latest

# Load planning before tracked-file and installation workflows.
$toolUpdateModules = @(
    'ToolUpdates/Planning.ps1',
    'ToolUpdates/TrackedPlan.ps1',
    'ToolUpdates/Installation.ps1',
    'ToolUpdates/Update.ps1',
    'ToolUpdates/Preflight.ps1',
    'ToolUpdates/Ensure.ps1'
)

foreach ($toolUpdateModule in $toolUpdateModules)
{
    . (Join-Path $PSScriptRoot $toolUpdateModule)
}
