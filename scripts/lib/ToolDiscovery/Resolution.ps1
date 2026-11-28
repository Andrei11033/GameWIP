# ------------------------------------------------------------
# Command resolution
# ------------------------------------------------------------

function Resolve-GameWipToolCommand
{
    param([hashtable]$Tool, [string]$Command)

    if ($null -ne $Tool)
    {
        $candidates = @(Get-GameWipToolCandidates -Tool $Tool)

        if ($candidates.Count -eq 0)
        {
            return $null
        }

        return $candidates[0].Path
    }

    if ([string]::IsNullOrWhiteSpace($Command))
    {
        throw 'Resolve-GameWipToolCommand requires -Tool or -Command.'
    }

    $names = @(Get-GameWipExecutableNames -Command $Command)

    if (Test-GameWipWindowsHost)
    {
        $windowsCommand = Resolve-GameWipWindowsToolCommand -Names $names

        if ($null -ne $windowsCommand)
        {
            return $windowsCommand
        }
    }
    $resolved = Get-Command $Command -ErrorAction SilentlyContinue | Select-Object -First 1

    if ($null -ne $resolved)
    {
        return $resolved.Source
    }

    return $null
}
