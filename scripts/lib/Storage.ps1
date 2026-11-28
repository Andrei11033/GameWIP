# Repository-owned storage and path helpers.

Set-StrictMode -Version Latest

# Load shared helpers in dependency order.
$storageModules = @(
    'Storage/Files.ps1',
    'Storage/Paths.ps1',
    'Storage/OwnedTrees.ps1'
)

foreach ($storageModule in $storageModules)
{
    . (Join-Path $PSScriptRoot $storageModule)
}
