# Project-tool commands and provider loading.

Set-StrictMode -Version Latest

# Load providers before resolving project tool metadata.
$portableProviderFiles = @(
    'Npm.ps1',
    'Python.ps1',
    'PowerShellGallery.ps1',
    'Archive.ps1',
    'GitHubRelease.ps1',
    'External.ps1'
)

foreach ($providerFile in $portableProviderFiles)
{
    . (Join-Path $PSScriptRoot (Join-Path 'Providers' $providerFile))
}

# Load shared tool inventory before selecting host implementations.
. (Join-Path $PSScriptRoot 'Tools/Discovery.ps1')

$platformRoot = if (Test-GameWipWindowsHost)
{
    Join-Path $ScriptsRoot 'platform/windows'
}
else
{
    Join-Path $ScriptsRoot 'platform/linux'
}

. (Join-Path $platformRoot 'Tools.ps1')

if (Test-GameWipWindowsHost)
{
    . (Join-Path $platformRoot 'ToolQueries.ps1')
    . (Join-Path $platformRoot 'Msys2.ps1')
    . (Join-Path $platformRoot 'Winget.ps1')
}

# Load the remaining tool functions by responsibility.
$toolModules = @(
    'Tools/Paths.ps1',
    'Tools/Status.ps1',
    'Tools/Providers.ps1',
    'Tools/Readiness.ps1'
)

foreach ($toolModule in $toolModules)
{
    . (Join-Path $PSScriptRoot $toolModule)
}
