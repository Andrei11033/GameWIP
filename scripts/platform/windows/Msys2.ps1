# Windows MSYS2 module loader.

# ------------------------------------------------------------
# Load archive and package operations
# ------------------------------------------------------------

Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Msys2/Bootstrap.ps1')
. (Join-Path $PSScriptRoot 'Msys2/Packages.ps1')
