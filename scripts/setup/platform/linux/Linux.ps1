# Linux setup support for Ubuntu, Debian, and CachyOS.

Set-StrictMode -Version Latest

$LinuxSetupConfig = Read-GameWipJsonConfig `
    -Path (Join-Path $PSScriptRoot 'host.json') `
    -Name 'Linux setup' `
    -SchemaPath (Join-Path $ScriptsRoot 'schemas/linux-host.schema.json')

# ------------------------------------------------------------
# Managed CMake support
# ------------------------------------------------------------

. (Join-Path $PSScriptRoot 'CMake.ps1')

# ------------------------------------------------------------
# Distribution detection and setup plan
# ------------------------------------------------------------

function Get-GameWipLinuxDistribution
{
    $releasePath = '/etc/os-release'

    if (-not (Test-Path -LiteralPath $releasePath -PathType Leaf))
    {
        throw 'Linux setup requires /etc/os-release to identify the distribution.'
    }

    $release = @{}

    foreach ($line in Get-Content -LiteralPath $releasePath)
    {
        if ($line -match '^([A-Z0-9_]+)=(.*)$')
        {
            $release[$Matches[1]] = $Matches[2].Trim().Trim('"').Trim("'")
        }
    }

    $id = [string]$release['ID']

    if (-not $LinuxSetupConfig.distributions.ContainsKey($id))
    {
        throw "Linux setup does not support distribution '$id'. Supported distributions: Ubuntu, Debian, and CachyOS."
    }

    return [pscustomobject]@{
        Id = $id
        Name = [string]$release['NAME']
        PackageManager = [string]$LinuxSetupConfig.distributions[$id]
    }
}

function Get-GameWipSetupLinuxPlan
{
    $distribution = Get-GameWipLinuxDistribution
    $manager = $LinuxSetupConfig.packageManagers[$distribution.PackageManager]
    $packages = @($manager.packages) -join ', '
    $cmakeVersion = (Get-GameWipProjectTool -Id cmake).requiredVersion

    $packageStep = if ($distribution.PackageManager -eq 'pacman')
    {
        "Run pacman -Syu --needed for $($distribution.Name): $packages. This includes a full system upgrade."
    }
    else
    {
        "Run apt-get update, then install missing packages without upgrades for $($distribution.Name): $packages."
    }

    return @(
        $packageStep,
        "Install verified CMake $cmakeVersion under the managed-tools root if the system version is too old."
    )
}

# ------------------------------------------------------------
# Privileged package commands
# ------------------------------------------------------------

function Get-GameWipLinuxInstalledPackages
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('apt', 'pacman')][string]$Manager,
        [Parameter(Mandatory = $true)][string[]]$Packages
    )

    $requested = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($package in $Packages)
    {
        $requested.Add($package) | Out-Null
    }

    if ($Manager -eq 'apt')
    {
        $result = Invoke-GameWipProcess `
            -FilePath dpkg-query `
            -Arguments @('-W', '-f=${binary:Package}\t${db:Status-Status}\n') `
            -OutputMode LogOnly `
            -TimeoutSeconds 60
        $installed = foreach ($line in $result.Stdout)
        {
            $columns = ([string]$line).Split("`t")

            if ($columns.Count -ge 2 -and $columns[1] -eq 'installed' -and $requested.Contains($columns[0]))
            {
                $columns[0]
            }
        }
    }
    else
    {
        $result = Invoke-GameWipProcess `
            -FilePath pacman `
            -Arguments @('-Qq') `
            -OutputMode LogOnly `
            -TimeoutSeconds 60
        $installed = foreach ($line in $result.Stdout)
        {
            $package = ([string]$line).Trim()

            if ($requested.Contains($package))
            {
                $package
            }
        }
    }

    return @($installed | Sort-Object -Unique)
}

function Invoke-GameWipLinuxPrivilegedCommand
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )

    $isRoot = [Environment]::UserName -eq 'root'

    if ($isRoot)
    {
        Invoke-GameWipSetupNative -FilePath $FilePath -ArgumentList $Arguments | Out-Null
        return
    }

    $sudo = Resolve-GameWipSetupCommand -Name 'sudo'

    if ($null -eq $sudo)
    {
        throw "'$FilePath' requires administrator privileges. Run setup with sudo or install sudo first."
    }

    if ($NonInteractive)
    {
        $check = Invoke-GameWipProcess `
            -FilePath $sudo `
            -Arguments @('-n', 'true') `
            -OutputMode LogOnly `
            -TimeoutSeconds 10 `
            -PreserveConsoleSession

        if ($check.ExitCode -ne 0)
        {
            throw 'Noninteractive setup needs root or cached sudo credentials. Run sudo -v first, then retry.'
        }
        $arguments = @('-n', $FilePath) + $Arguments
    }
    else
    {
        Invoke-GameWipSetupNative -FilePath $sudo -ArgumentList @('-v') -InheritConsole | Out-Null
        $arguments = @('-n', $FilePath) + $Arguments
    }

    # Keep sudo in the session that accepted credentials while retaining its output.
    Invoke-GameWipSetupNative `
        -FilePath $sudo `
        -ArgumentList $arguments `
        -PreserveConsoleSession | Out-Null
}

# ------------------------------------------------------------
# Linux prerequisite installation
# ------------------------------------------------------------

function Invoke-GameWipSetupLinuxPrerequisites
{
    # Identify the package manager and snapshot its requested packages.
    $distribution = Get-GameWipLinuxDistribution
    $manager = $LinuxSetupConfig.packageManagers[$distribution.PackageManager]
    $command = if ($distribution.PackageManager -eq 'apt')
    {
        'apt-get'
    }
    else
    {
        'pacman'
    }

    Write-GameWipSection "Linux prerequisites: $($distribution.Name)"
    Write-Host "  Package manager: $command"
    Write-Host "  Packages: $(@($manager.packages) -join ', ')"
    $packagesBefore = [System.Collections.Generic.HashSet[string]]::new(
        [string[]]@(Get-GameWipLinuxInstalledPackages -Manager $distribution.PackageManager -Packages @($manager.packages)),
        [StringComparer]::OrdinalIgnoreCase
    )

    # Apply package updates and install the required host tools.
    try
    {
        if ($manager.updateArguments.Count -ne 0)
        {
            Invoke-GameWipLinuxPrivilegedCommand -FilePath $command -Arguments @($manager.updateArguments)
        }

        Invoke-GameWipLinuxPrivilegedCommand `
            -FilePath $command `
            -Arguments (@($manager.installArguments) + @($manager.packages))
    }

    finally
    {
        # Record only packages that were absent before this operation.
        $packagesAfter = @(Get-GameWipLinuxInstalledPackages -Manager $distribution.PackageManager -Packages @($manager.packages))
        $newPackages = @($packagesAfter | Where-Object { -not $packagesBefore.Contains([string]$_) })

        if ($newPackages.Count -ne 0)
        {
            Add-GameWipOwnedSystemPackages -Manager $distribution.PackageManager -Packages $newPackages
            Write-Host "  Recorded newly installed packages for optional uninstall: $($newPackages -join ', ')"
        }
    }

    # Add a verified CMake build when the host package is too old.
    Install-GameWipLinuxCMake
}

function Remove-GameWipLinuxSystemPackages
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('apt', 'pacman')][string]$Manager,
        [Parameter(Mandatory = $true)][string[]]$Packages
    )

    if ($Packages.Count -eq 0)
    {
        return
    }

    $packageManager = $LinuxSetupConfig.packageManagers[$Manager]
    $command = if ($Manager -eq 'apt')
    {
        'apt-get'
    }
    else
    {
        'pacman'
    }

    Invoke-GameWipLinuxPrivilegedCommand `
        -FilePath $command `
        -Arguments (@($packageManager.removeArguments) + @($Packages))
}
