# Windows package-registry queries.

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
