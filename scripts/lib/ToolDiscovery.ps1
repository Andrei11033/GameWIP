# Deterministic tool discovery and installed-version selection.

Set-StrictMode -Version Latest

# Load Windows-specific candidate paths from their platform module.
if (Test-GameWipWindowsHost)
{
    . (Join-Path (Join-Path $ScriptsRoot 'platform/windows') 'ToolDiscovery.ps1')
}

# Load candidate discovery before command resolution and status reporting.
$toolDiscoveryModules = @(
    'ToolDiscovery/Candidates.ps1',
    'ToolDiscovery/Resolution.ps1',
    'ToolDiscovery/Status.ps1'
)

foreach ($toolDiscoveryModule in $toolDiscoveryModules)
{
    . (Join-Path $PSScriptRoot $toolDiscoveryModule)
}
