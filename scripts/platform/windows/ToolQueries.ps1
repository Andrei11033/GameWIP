# Windows package-registry queries.

# ------------------------------------------------------------
# MSYS2 rolling package updates
# ------------------------------------------------------------

function Get-GameWipMsys2SystemUpdatesQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if (-not (Test-GameWipWindowsHost))
    {
        return New-GameWipToolQueryResult `
            -State unsupported `
            -Provider msys2 `
            -Version $null `
            -Reason 'MSYS2 updates are available only on Windows'
    }

    $bash = Join-Path (Get-GameWipMsys2Root) 'usr\bin\bash.exe'

    if (-not (Test-Path -LiteralPath $bash -PathType Leaf))
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider msys2 `
            -Version $null `
            -Reason 'MSYS2 bash is unavailable'
    }

    $result = Invoke-GameWipProcess `
        -FilePath $bash `
        -Arguments @('-lc', 'pacman --color never -Qu') `
        -OutputMode LogOnly `
        -TimeoutSeconds 60

    if ($result.Cancelled)
    {
        throw [System.OperationCanceledException]::new('MSYS2 update query cancelled.')
    }

    if ($result.TimedOut)
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider msys2 `
            -Version $null `
            -Reason 'pacman update query timed out'
    }

    $lines = @($result.Stdout + $result.Stderr | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })

    # pacman returns 1 with no output when its cached databases contain no updates.
    if ($result.ExitCode -notin @(0, 1) -or ($result.ExitCode -eq 1 -and $lines.Count -ne 0))
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider msys2 `
            -Version $null `
            -Reason (($lines -join ' ').Trim())
    }

    $packages = [System.Collections.Generic.List[object]]::new()

    foreach ($line in $lines)
    {
        $match = [regex]::Match($line, '^(\S+)\s+(\S+)\s+->\s+(\S+)$')

        if (-not $match.Success)
        {
            return New-GameWipToolQueryResult `
                -State invalidResponse `
                -Provider msys2 `
                -Version $null `
                -Reason "pacman returned an unrecognized update line: $line"
        }

        $packages.Add([pscustomobject]@{
                Name = $match.Groups[1].Value
                Installed = $match.Groups[2].Value
                Available = $match.Groups[3].Value
            }) | Out-Null
    }

    $metadata = [pscustomobject]@{
        Kind = 'packageUpdates'
        Count = $packages.Count
        Packages = @($packages)
    }
    $reason = 'Based on cached package databases; setup update refreshes them and applies the full MSYS2 upgrade.'

    return New-GameWipToolQueryResult `
        -State resolved `
        -Provider msys2 `
        -Version $null `
        -Reason $reason `
        -Metadata $metadata
}

# ------------------------------------------------------------
# MSYS2 package versions
# ------------------------------------------------------------

function Get-GameWipMsys2ToolLatestQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $bash = Join-Path (Get-GameWipMsys2Root) 'usr\bin\bash.exe'

    if (-not (Test-Path -LiteralPath $bash))
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider msys2 `
            -Version $null `
            -Reason 'MSYS2 bash is unavailable'
    }

    $query = Invoke-GameWipToolQueryProcess `
        -FilePath $bash `
        -Arguments @('-lc', "pacman -Si '$($Tool.provider.package)' 2>/dev/null") `
        -Provider msys2

    if ($query.State -ne 'process')
    {
        return $query
    }

    $match = [regex]::Match($query.Output, '(?m)^Version\s*:\s*([^\s-]+)')

    if (-not $match.Success)
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider msys2 `
            -Version $null `
            -Reason 'pacman output did not contain a version'
    }

    return New-GameWipToolQueryResult -State resolved -Provider msys2 -Version $match.Groups[1].Value
}

# ------------------------------------------------------------
# WinGet package versions
# ------------------------------------------------------------

function Get-GameWipWingetToolLatestQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $winget = Resolve-GameWipToolCommand -Command 'winget'

    if ($null -eq $winget)
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider winget `
            -Version $null `
            -Reason 'winget is unavailable'
    }

    $query = Invoke-GameWipToolQueryProcess `
        -FilePath $winget `
        -Arguments @('show', '--id', [string]$Tool.provider.package, '--exact', '--accept-source-agreements') `
        -Provider winget

    if ($query.State -ne 'process')
    {
        return $query
    }

    $match = [regex]::Match($query.Output, '(?m)^Version:\s*(\S+)')

    if (-not $match.Success)
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider winget `
            -Version $null `
            -Reason 'winget output did not contain Version'
    }

    return New-GameWipToolQueryResult -State resolved -Provider winget -Version $match.Groups[1].Value
}
