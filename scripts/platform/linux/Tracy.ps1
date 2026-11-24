# Linux Tracy profiler backend selection.

# ------------------------------------------------------------
# Linux desktop session
# ------------------------------------------------------------

function Get-GameWipLinuxTracyExecutableSet
{
    return @(
        'tracy-profiler-wayland'
        'tracy-profiler-x11'
    )
}

function Get-GameWipTracyProfilerBackend
{
    if (-not [string]::IsNullOrWhiteSpace([string]$env:WAYLAND_DISPLAY))
    {
        return 'wayland'
    }

    if (-not [string]::IsNullOrWhiteSpace([string]$env:DISPLAY))
    {
        return 'x11'
    }

    return ''
}
