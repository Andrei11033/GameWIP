# Linux Tracy profiler backend selection.

# ------------------------------------------------------------
# Linux desktop session
# ------------------------------------------------------------

function Get-GameWipLinuxTracyExecutableSet
{
    return @(
        # Shared command-line tools.
        'tracy-capture'
        'tracy-csvexport'
        'tracy-import-chrome'
        'tracy-import-fuchsia'
        'tracy-update'

        # Native profiler variants.
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
