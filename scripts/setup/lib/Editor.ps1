# Editor setup module loader.

# ------------------------------------------------------------
# Load selection and integration functions
# ------------------------------------------------------------

Set-StrictMode -Version Latest

. (Join-Path $PSScriptRoot 'Editor/Selection.ps1')
. (Join-Path $PSScriptRoot 'Editor/VsCode/Keybindings.ps1')
. (Join-Path $PSScriptRoot 'Editor/VsCode/Packaging.ps1')
. (Join-Path $PSScriptRoot 'Editor/VsCode.ps1')
