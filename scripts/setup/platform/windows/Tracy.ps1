# Windows Tracy module loader.

# ------------------------------------------------------------
# Load Tracy discovery, runtime, build, and installation functions
# ------------------------------------------------------------

Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Tracy/Discovery.ps1')
. (Join-Path $PSScriptRoot 'Tracy/Runtime.ps1')
. (Join-Path $PSScriptRoot 'Tracy/Projects.ps1')
. (Join-Path $PSScriptRoot 'Tracy/Installation.ps1')
. (Join-Path $PSScriptRoot 'Tracy/Build.ps1')
