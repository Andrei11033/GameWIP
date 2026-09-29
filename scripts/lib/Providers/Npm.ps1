# npm tool provider support.

# ------------------------------------------------------------
# npm discovery and installation
# ------------------------------------------------------------

function Get-GameWipNpmPackageLatestVersion
{
    param([Parameter(Mandatory = $true)][string]$Package)
    $query = Get-GameWipNpmPackageLatestQuery -Package $Package
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Get-GameWipNpmToolLatestVersion
{
    param([hashtable]$Tool)
    return Get-GameWipNpmPackageLatestVersion -Package ([string]$Tool.provider.package)
}

function Get-GameWipNpmGlobalModuleRoot
{
    if (Test-GameWipWindowsHost)
    {
        return Join-Path ((Get-GameWipManagedToolRoot)) 'npm\lib\node_modules'
    }
    $npm = Resolve-GameWipToolCommand -Command npm
    if ($null -eq $npm)
    {
        throw 'npm is unavailable; cannot resolve the global module root.'
    }
    $result = Invoke-GameWipProcess -FilePath $npm -Arguments @('root', '--global') -OutputMode LogOnly -TimeoutSeconds 20
    $output = ($result.Stdout -join "`n").Trim()
    if ($result.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($output))
    {
        throw 'Unable to resolve the global npm module root.'
    }
    return $output
}

function Get-GameWipNpmPackageInstallPath
{
    param(
        [Parameter(Mandatory = $true)][string]$ModuleRoot,
        [Parameter(Mandatory = $true)][string]$Package
    )

    $installPath = $ModuleRoot
    foreach ($segment in ($Package -split '/'))
    {
        $installPath = Join-Path $installPath $segment
    }
    return $installPath
}

function Get-GameWipNpmInstalledPackageEntryPath
{
    param(
        [Parameter(Mandatory = $true)][string]$ModuleRoot,
        [Parameter(Mandatory = $true)][string]$Package
    )

    $packageRoot = Get-GameWipNpmPackageInstallPath -ModuleRoot $ModuleRoot -Package $Package
    $manifestPath = Join-Path $packageRoot 'package.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf))
    {
        throw "Installed npm package '$Package' was not found at '$packageRoot'."
    }

    try
    {
        $manifest = Read-GameWipUtf8Text -Path $manifestPath | ConvertFrom-Json
    }
    catch
    {
        throw "Installed npm package '$Package' has unreadable package metadata: $($_.Exception.Message)"
    }

    $main = [string]$manifest.main
    if ([string]::IsNullOrWhiteSpace($main))
    {
        throw "Installed npm package '$Package' does not declare a main entry in '$manifestPath'."
    }

    $entryPath = $packageRoot
    foreach ($segment in ($main -split '[/\\]'))
    {
        if ($segment -and $segment -ne '.')
        {
            $entryPath = Join-Path $entryPath $segment
        }
    }
    if (-not (Test-Path -LiteralPath $entryPath -PathType Leaf))
    {
        throw "Installed npm package '$Package' declares missing entry '$main' in '$manifestPath'."
    }
    return [IO.Path]::GetFullPath($entryPath)
}

function Get-GameWipNpmInstalledPackageVersion
{
    param(
        [Parameter(Mandatory = $true)][string]$ModuleRoot,
        [Parameter(Mandatory = $true)][string]$Package
    )

    $manifestPath = Join-Path (Get-GameWipNpmPackageInstallPath -ModuleRoot $ModuleRoot -Package $Package) 'package.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf))
    {
        return [pscustomobject]@{ State = 'missing'; Version = $null; Location = $null; Reason = "Installed npm package '$Package' was not found." }
    }

    try
    {
        $manifest = Read-GameWipUtf8Text -Path $manifestPath | ConvertFrom-Json
        $version = [string]$manifest.version
        if ([string]::IsNullOrWhiteSpace($version))
        {
            return [pscustomobject]@{ State = 'unknown'; Version = $null; Location = $manifestPath; Reason = "Installed npm package '$Package' has no version in its package metadata." }
        }
        return [pscustomobject]@{ State = 'installed'; Version = $version; Location = $manifestPath; Reason = $null }
    }
    catch
    {
        return [pscustomobject]@{ State = 'unknown'; Version = $null; Location = $manifestPath; Reason = "Installed npm package '$Package' has unreadable package metadata: $($_.Exception.Message)" }
    }
}

function Get-GameWipNpmToolDependencyStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $moduleRoot = Get-GameWipNpmGlobalModuleRoot
    foreach ($dependency in @($Tool.provider.dependencies))
    {
        $package = [string]$dependency.package
        $requiredVersion = [string]$dependency.version
        $installed = Get-GameWipNpmInstalledPackageVersion -ModuleRoot $moduleRoot -Package $package
        $state = if ($installed.State -eq 'missing')
        {
            'missing'
        }
        elseif ($installed.State -ne 'installed')
        {
            'unknown'
        }
        elseif ($installed.Version -cne $requiredVersion)
        {
            'mismatch'
        }
        else
        {
            'compatible'
        }

        $reason = if ($state -eq 'mismatch')
        {
            "Required $requiredVersion, found $($installed.Version)."
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
            RequiredVersion = $requiredVersion
            InstalledVersion = $installed.Version
            State = $state
            Location = $installed.Location
            Reason = $reason
        }
    }
}

function Install-GameWipNpmTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    Initialize-GameWipManagedToolRoot
    $root = Join-Path ((Get-GameWipManagedToolRoot)) 'npm'
    $specification = if ($Version)
    {
        "$($Tool.provider.package)@$Version"
    }
    else
    {
        [string]$Tool.provider.package
    }
    $specifications = @($specification)
    foreach ($dependency in $(if ($Tool.provider.Contains('dependencies'))
            {
                @($Tool.provider.dependencies)
            }
            else
            {
                @()
            }))
    {
        $specifications += "$($dependency.package)@$($dependency.version)"
    }
    $npm = Resolve-GameWipToolCommand -Command npm
    if ($null -eq $npm)
    {
        throw "npm is unavailable for '$($Tool.id)'. Run setup.bat repair."
    }
    Invoke-GameWipProviderNative -Name "npm-install-$($Tool.id)" -FilePath $npm -Arguments (@('install', '--global', '--prefix', $root, '--no-audit', '--no-fund') + $specifications) | Out-Null
}
