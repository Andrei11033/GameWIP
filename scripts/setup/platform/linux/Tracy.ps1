# Linux Tracy profiler setup module.

# ------------------------------------------------------------
# Load profiler discovery, build, and installation functions
# ------------------------------------------------------------

foreach ($file in @('Tracy/Discovery.ps1', 'Tracy/Build.ps1', 'Tracy/Installation.ps1'))
{
    . (Join-Path $PSScriptRoot $file)
}
