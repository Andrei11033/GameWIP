# MSYS2 package derivation, full-system update, retry, and toolchain verification.

# ------------------------------------------------------------
# MSYS2 package discovery and installation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipExistingMsys2Roots
{
    $candidates = [System.Collections.Generic.List[string]]::new()
    $candidatesFound = [System.Collections.Generic.List[string]]::new()
    $seen = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($root in @(
            'C:\msys64',
            'C:\msys32',
            (Join-Path $env:LOCALAPPDATA 'Programs/msys64'),
            (Join-Path $env:LOCALAPPDATA 'Programs/msys32'),
            (Join-Path ([Environment]::GetFolderPath('ProgramFiles')) 'MSYS2'),
            (Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'MSYS2')
        ))
    {
        if (-not [string]::IsNullOrWhiteSpace([string]$root))
        {
            $candidates.Add([string]$root) | Out-Null
        }
    }

    foreach ($application in @('pacman.exe', 'bash.exe'))
    {
        foreach ($command in @(Get-Command $application -CommandType Application -All -ErrorAction SilentlyContinue))
        {
            $root = Split-Path -Parent (Split-Path -Parent (Split-Path -Parent ([string]$command.Source)))
            $candidates.Add($root) | Out-Null
        }
    }

    foreach ($registryPath in @(
            'HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\*',
            'HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\*'
        ))
    {
        # Registry uninstall entries may omit optional fields.
        foreach ($application in @(Get-ItemProperty -Path $registryPath -ErrorAction SilentlyContinue))
        {
            $displayName = $application.PSObject.Properties['DisplayName']

            if ($null -eq $displayName -or [string]$displayName.Value -notmatch '(?i)msys2')
            {
                continue
            }

            $installLocation = $application.PSObject.Properties['InstallLocation']
            $root = if ($null -ne $installLocation)
            {
                [string]$installLocation.Value
            }
            else
            {
                ''
            }

            if ([string]::IsNullOrWhiteSpace($root))
            {
                $appPath = $application.PSObject.Properties['Inno Setup: App Path']
                $root = if ($null -ne $appPath)
                {
                    [string]$appPath.Value
                }
                else
                {
                    ''
                }
            }

            if ([string]::IsNullOrWhiteSpace($root))
            {
                $uninstallValue = $application.PSObject.Properties['UninstallString']
                $uninstallString = if ($null -ne $uninstallValue)
                {
                    [string]$uninstallValue.Value
                }
                else
                {
                    ''
                }
                $match = [regex]::Match($uninstallString, '(?i)(?:"(?<path>[^\"]+\\uninstall\.exe)"|(?<path>[^\s]+\\uninstall\.exe))')

                if ($match.Success)
                {
                    $root = Split-Path -Parent $match.Groups['path'].Value
                }
            }

            if (-not [string]::IsNullOrWhiteSpace($root))
            {
                $candidates.Add($root) | Out-Null
            }
        }
    }

    $defaultRoot = Get-GameWipMsys2Root

    foreach ($candidate in $candidates)
    {
        if (-not (Test-Path -LiteralPath $candidate -PathType Container))
        {
            continue
        }
        $root = [IO.Path]::GetFullPath($candidate)

        if ($root.Equals($defaultRoot, [StringComparison]::OrdinalIgnoreCase) -or
            -not (Test-Path -LiteralPath (Join-Path $root 'usr/bin/bash.exe') -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $root 'usr/bin/pacman.exe') -PathType Leaf) -or
            -not $seen.Add($root))
        {
            continue
        }
        $candidatesFound.Add($root) | Out-Null
    }

    return @($candidatesFound | Sort-Object)
}

function Invoke-GameWipMsys2
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string]$Command)
    $bash = Join-Path $MsysRoot 'usr\bin\bash.exe'

    if (-not (Test-Path -LiteralPath $bash))
    {
        throw "MSYS2 bash was not found at $bash."
    }

    Invoke-GameWipSetupNative -FilePath $bash -ArgumentList @('-lc', $Command) | Out-Null
}

function Invoke-GameWipMsys2PacmanWithRetry
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string]$Command, [ValidateRange(1, 10)][int]$MaxAttempts = 3, [ValidateRange(0, 60)][int]$RetryDelaySeconds = 2)

    for ($attempt = 1; $attempt -le $MaxAttempts; ++$attempt)
    {
        try
        {
            Write-GameWipOperationEvent -Phase execute -Step pacman -Severity info -Message "pacman attempt $attempt of $MaxAttempts"
            Invoke-GameWipMsys2 -MsysRoot $MsysRoot -Command $Command
            return
        }

        catch
        {
            if ($attempt -ge $MaxAttempts)
            {
                throw
            }
            Write-GameWipOperationEvent -Phase execute -Step pacman-retry -Severity warning -Message "Transient pacman failure; retrying in $RetryDelaySeconds second(s)." -Data @{ attempt = $attempt; reason = $_.Exception.Message }

            if ($RetryDelaySeconds -gt 0)
            {
                Start-Sleep -Seconds $RetryDelaySeconds
            }
        }
    }
}

function Get-GameWipMissingMsys2Package
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string[]]$Packages)
    $installed = Get-GameWipMsys2InstalledPackageNames -MsysRoot $MsysRoot

    if ($installed.State -ne 'ready')
    {
        return $Packages
    }

    $installedNames = [System.Collections.Generic.HashSet[string]]::new([string[]]$installed.Packages)
    return @($Packages | Where-Object { -not $installedNames.Contains($_) })
}

function Test-GameWipMsys2PackageSet
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][string[]]$Packages)
    return @(Get-GameWipMissingMsys2Package -MsysRoot $MsysRoot -Packages $Packages).Count -eq 0
}

function Get-GameWipMsys2PackageConfig
{
    param([Parameter(Mandatory = $true)][hashtable]$ProjectTools)
    $result = @{ Common = @(); Ucrt64 = @(); Clang64 = @() }
    $packageEntries = [System.Collections.Generic.List[object]]::new()

    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $provider = $toolInfo.provider

        if ($provider.kind -eq 'msys2')
        {
            $packageEntries.Add($provider) | Out-Null
            $dependencies = if ($provider.Contains('dependencies'))
            {
                @($provider.dependencies)
            }
            else
            {
                @()
            }

            foreach ($dependency in $dependencies)
            {
                $packageEntries.Add($dependency) | Out-Null
            }
        }
        elseif ($provider.kind -eq 'python' -and $provider.Contains('windowsMsys2'))
        {
            $packageEntries.Add($provider.windowsMsys2) | Out-Null
        }
    }

    foreach ($packageInfo in $packageEntries)
    {
        $key = switch ([string]$packageInfo.environment)
        {
            'common'
            {
                'Common'
            }
            'ucrt64'
            {
                'Ucrt64'
            }
            'clang64'
            {
                'Clang64'
            }
            default
            {
                throw "Unknown MSYS2 environment '$($packageInfo.environment)'."
            }
        }
        $result[$key] += [string]$packageInfo.package
    }

    foreach ($key in @('Common', 'Ucrt64', 'Clang64'))
    {
        $result[$key] = @($result[$key] | Sort-Object -Unique)
    }

    return $result
}

function Install-GameWipMsys2PackageSet
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot, [Parameter(Mandatory = $true)][hashtable]$PackageConfig, [switch]$Update)

    if ($Update)
    {
        Invoke-GameWipMsys2PacmanWithRetry -MsysRoot $MsysRoot -Command 'pacman -Syu --noconfirm'
        Invoke-GameWipMsys2PacmanWithRetry -MsysRoot $MsysRoot -Command 'pacman -Syu --noconfirm'
    }
    $packages = @(@($PackageConfig.Common) + @($PackageConfig.Ucrt64) + @($PackageConfig.Clang64) | Sort-Object -Unique)
    Invoke-GameWipMsys2PacmanWithRetry -MsysRoot $MsysRoot -Command "pacman --needed --noconfirm -S $($packages -join ' ')"

    if (-not (Test-GameWipMsys2PackageSet -MsysRoot $MsysRoot -Packages $packages))
    {
        throw 'One or more required MSYS2 packages failed verification.'
    }
}

function Test-GameWipMsys2Toolchain
{
    param([Parameter(Mandatory = $true)][hashtable]$ProjectTools)
    $toolIds = @($ProjectTools.tools | Where-Object { $_.provider.kind -eq 'msys2' -and $_.capabilities.detectInstalled } | ForEach-Object { [string]$_.id })
    Test-GameWipToolchain `
        -ToolIds $toolIds `
        -ThrowOnFailure `
        -FailureCode msys2-toolchain-incomplete `
        -FailureSummary 'The declared MSYS2 toolchain is incomplete.' `
        -SuggestedActions @('.\setup.bat repair', '.\gamewip.bat tool status') | Out-Null
    Write-Host '  Ready: registry-declared UCRT64/CLANG64 requirements.'
}
