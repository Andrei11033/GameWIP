# MSYS2 package queries and installation.

# ------------------------------------------------------------
# Query package versions
# ------------------------------------------------------------

function Get-GameWipMsys2ToolLatestVersion
{
    param([hashtable]$Tool)

    $query = Get-GameWipToolLatestQuery -Tool $Tool

    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }

    return $null
}

# ------------------------------------------------------------
# Inspect installed packages and dependencies
# ------------------------------------------------------------

function Get-GameWipMsys2InstalledPackageNames
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot)

    $pacman = Join-Path $MsysRoot 'usr\bin\pacman.exe'

    if (-not (Test-Path -LiteralPath $pacman -PathType Leaf))
    {
        return [pscustomobject]@{ State = 'missing'; Packages = @(); Reason = "MSYS2 pacman was not found at '$pacman'." }
    }

    $result = Invoke-GameWipProcess -FilePath $pacman -Arguments @('-Qq') -OutputMode LogOnly -TimeoutSeconds 60

    if ($result.ExitCode -ne 0)
    {
        return [pscustomobject]@{ State = 'unknown'; Packages = @(); Reason = 'MSYS2 pacman could not enumerate installed packages.' }
    }

    return [pscustomobject]@{
        State = 'ready'
        Packages = @($result.Stdout | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })
        Reason = $null
    }
}

function Get-GameWipMsys2ToolDependencyStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $installed = Get-GameWipMsys2InstalledPackageNames -MsysRoot (Get-GameWipMsys2Root)

    foreach ($dependency in @($Tool.provider.dependencies))
    {
        $package = [string]$dependency.package
        $state = if ($installed.State -ne 'ready')
        {
            $installed.State
        }
        elseif ($installed.Packages -contains $package)
        {
            'compatible'
        }
        else
        {
            'missing'
        }

        $reason = if ($state -eq 'missing')
        {
            "MSYS2 package '$package' is not installed."
        }
        elseif ($installed.Reason)
        {
            [string]$installed.Reason
        }
        else
        {
            $null
        }

        [pscustomobject]@{
            Package = $package
            RequiredVersion = $null
            InstalledVersion = $null
            State = $state
            Location = Get-GameWipMsys2Root
            Reason = $reason
        }
    }
}

# ------------------------------------------------------------
# Install declared packages
# ------------------------------------------------------------

function Install-GameWipMsys2Tool
{
    param(
        [hashtable]$Tool,
        [AllowNull()][string]$Version
    )

    $null = $Version
    $bash = Join-Path (Get-GameWipMsys2Root) 'usr\bin\bash.exe'

    if (-not (Test-Path -LiteralPath $bash))
    {
        throw 'MSYS2 is not installed; run setup.bat repair.'
    }

    # Combine the tool package and its declared dependencies.
    $dependencies = if ($Tool.provider.Contains('dependencies'))
    {
        @($Tool.provider.dependencies)
    }
    else
    {
        @()
    }
    $packages = @([string]$Tool.provider.package) + @($dependencies | ForEach-Object { [string]$_.package })
    $packageText = @($packages | Sort-Object -Unique | ForEach-Object { "'$_'" }) -join ' '

    Invoke-GameWipProviderNative `
        -Name "pacman-install-$($Tool.id)" `
        -FilePath $bash `
        -Arguments @('-lc', "pacman --noconfirm --needed -S $packageText") | Out-Null
}
