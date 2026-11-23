# Shared setup state and presentation helpers. Native execution stays in scripts/lib/Process.ps1.

# ------------------------------------------------------------
# Setup state and ownership metadata
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipSetupState
{
    $defaultState = [ordered]@{ schemaVersion = 1; wingetPackages = @(); vscodeExtensions = @(); systemPackages = @(); msys2InstalledBySetup = $false }

    $script:SetupStateReadError = ''

    if (-not (Test-Path -LiteralPath $script:SetupStatePath -PathType Leaf))
    {
        $script:SetupStateReadStatus = 'missing'
        return $defaultState
    }

    try
    {
        $storedState = Get-Content -LiteralPath $script:SetupStatePath -Raw | ConvertFrom-Json -ErrorAction Stop
    }

    catch
    {
        $script:SetupStateReadStatus = 'corrupt'
        $script:SetupStateReadError = $_.Exception.Message
        return $defaultState
    }

    if ($null -eq $storedState)
    {
        $script:SetupStateReadStatus = 'corrupt'
        $script:SetupStateReadError = 'The state document must contain a JSON object.'
        return $defaultState
    }

    if ($storedState -isnot [pscustomobject])
    {
        $script:SetupStateReadStatus = 'corrupt'
        $script:SetupStateReadError = 'The state document must contain a JSON object.'
        return $defaultState
    }

    $repairReasons = [System.Collections.Generic.List[string]]::new()
    $schemaVersionProperty = $storedState.PSObject.Properties['schemaVersion']

    if ($null -eq $schemaVersionProperty -or $schemaVersionProperty.Value -ne 1)
    {
        $repairReasons.Add('schemaVersion is missing or unsupported') | Out-Null
    }

    # Normalize recorded tool and editor ownership.
    $stringLists = @{}

    foreach ($field in @('wingetPackages', 'vscodeExtensions'))
    {
        $property = $storedState.PSObject.Properties[$field]
        $values = [System.Collections.Generic.List[string]]::new()

        if ($null -eq $property -or $null -eq $property.Value)
        {
            $repairReasons.Add("$field is missing") | Out-Null
        }
        else
        {
            if ($property.Value -isnot [array])
            {
                $repairReasons.Add("$field is not an array") | Out-Null
            }

            foreach ($value in @($property.Value))
            {
                if ($value -is [string] -and -not [string]::IsNullOrWhiteSpace($value))
                {
                    $values.Add($value) | Out-Null
                }
                else
                {
                    $repairReasons.Add("$field contains an invalid entry") | Out-Null
                }
            }
        }

        $stringLists[$field] = @($values | Sort-Object -Unique)
    }

    # Normalize package ownership used by Linux uninstall.
    $systemPackages = [System.Collections.Generic.List[object]]::new()
    $systemPackageKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $systemPackagesProperty = $storedState.PSObject.Properties['systemPackages']

    if ($null -eq $systemPackagesProperty -or $null -eq $systemPackagesProperty.Value)
    {
        $repairReasons.Add('systemPackages is missing') | Out-Null
    }
    else
    {
        if ($systemPackagesProperty.Value -isnot [array])
        {
            $repairReasons.Add('systemPackages is not an array') | Out-Null
        }

        foreach ($entry in @($systemPackagesProperty.Value))
        {
            if ($null -eq $entry)
            {
                $repairReasons.Add('systemPackages contains an invalid entry') | Out-Null
                continue
            }

            $managerProperty = $entry.PSObject.Properties['manager']
            $packageProperty = $entry.PSObject.Properties['package']

            if ($null -eq $managerProperty -or $null -eq $packageProperty)
            {
                $repairReasons.Add('systemPackages contains an invalid entry') | Out-Null
                continue
            }

            $manager = ([string]$managerProperty.Value).ToLowerInvariant()
            $package = [string]$packageProperty.Value
            $key = "$manager`:$package"

            if ($manager -in @('apt', 'pacman') -and $package -match '^[A-Za-z0-9][A-Za-z0-9+_.-]*$')
            {
                if ($systemPackageKeys.Add($key))
                {
                    $systemPackages.Add([ordered]@{ manager = $manager; package = $package }) | Out-Null
                }
                else
                {
                    $repairReasons.Add('systemPackages contains duplicate entries') | Out-Null
                }
            }
            else
            {
                $repairReasons.Add('systemPackages contains an invalid entry') | Out-Null
            }
        }
    }

    # Normalize the MSYS2 ownership flag.
    $msys2InstalledBySetup = $false
    $msys2InstalledProperty = $storedState.PSObject.Properties['msys2InstalledBySetup']

    if ($null -eq $msys2InstalledProperty)
    {
        $repairReasons.Add('msys2InstalledBySetup is missing') | Out-Null
    }
    elseif ($msys2InstalledProperty.Value -is [bool])
    {
        $msys2InstalledBySetup = [bool]$msys2InstalledProperty.Value
    }
    elseif ($msys2InstalledProperty.Value -is [string] -and
        [bool]::TryParse([string]$msys2InstalledProperty.Value, [ref]$msys2InstalledBySetup))
    {
        $repairReasons.Add('msys2InstalledBySetup is not a JSON boolean') | Out-Null
    }
    else
    {
        $repairReasons.Add('msys2InstalledBySetup is invalid') | Out-Null
    }

    if ($repairReasons.Count -eq 0)
    {
        $script:SetupStateReadStatus = 'valid'
    }
    else
    {
        $script:SetupStateReadStatus = 'needs-repair'
        $script:SetupStateReadError = (@($repairReasons | Sort-Object -Unique) -join '; ')
    }

    return [ordered]@{
        schemaVersion = 1
        wingetPackages = @($stringLists.wingetPackages)
        vscodeExtensions = @($stringLists.vscodeExtensions)
        systemPackages = @($systemPackages.ToArray() | Sort-Object manager, package)
        msys2InstalledBySetup = $msys2InstalledBySetup
    }
}

function Save-GameWipSetupState
{
    param([Parameter(Mandatory = $true)]$State)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $script:SetupStatePath) | Out-Null
    Write-GameWipJsonAtomic -Path $script:SetupStatePath -Value $State -Depth 8
    $script:SetupStateReadStatus = 'valid'
}

function Repair-GameWipSetupState
{
    $state = Get-GameWipSetupState

    if ($script:SetupStateReadStatus -in @('needs-repair', 'corrupt'))
    {
        Save-GameWipSetupState -State $state
    }

    return $state
}

function Assert-GameWipBuildUninstallLauncher
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$LaunchToken
    )

    if ($LaunchToken -notmatch '^[0-9]+-[0-9]+-[0-9]+$')
    {
        throw 'Uninstall must run through setup.bat or setup.sh so the launcher can remove build after exit.'
    }

    $buildRoot = Join-Path $RepositoryRoot 'build'
    $buildAttributes = Get-GameWipFileSystemAttributes -Path $buildRoot

    if ($null -eq $buildAttributes -or
        ($buildAttributes -band [IO.FileAttributes]::Directory) -eq 0 -or
        ($buildAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing to schedule build cleanup for missing or linked path '$buildRoot'."
    }
}

function Write-GameWipBuildUninstallRequest
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][string]$LaunchToken
    )

    Assert-GameWipBuildUninstallLauncher -RepositoryRoot $RepositoryRoot -LaunchToken $LaunchToken
    $buildRoot = Join-Path $RepositoryRoot 'build'
    $markerPath = Join-Path $buildRoot ".gamewip-uninstall-$LaunchToken.pending"
    $stream = [IO.File]::Open($markerPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)

    try
    {
        $bytes = [Text.Encoding]::ASCII.GetBytes($LaunchToken)
        $stream.Write($bytes, 0, $bytes.Length)
    }

    finally
    {
        $stream.Dispose()
    }
}

function Add-GameWipOwnedVsCodeExtension
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $state = Get-GameWipSetupState
    $state.vscodeExtensions = @(@($state.vscodeExtensions) + @($Id) | Sort-Object -Unique)
    Save-GameWipSetupState -State $state
}

function Add-GameWipOwnedWingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $state = Get-GameWipSetupState
    $state.wingetPackages = @(@($state.wingetPackages) + @($Id) | Sort-Object -Unique)
    Save-GameWipSetupState -State $state
}

function Add-GameWipOwnedSystemPackages
{
    param(
        [Parameter(Mandatory = $true)][ValidateSet('apt', 'pacman')][string]$Manager,
        [Parameter(Mandatory = $true)][string[]]$Packages
    )

    $state = Get-GameWipSetupState
    $entries = [System.Collections.Generic.Dictionary[string, object]]::new([StringComparer]::OrdinalIgnoreCase)

    foreach ($entry in @($state.systemPackages))
    {
        $entries["$($entry.manager):$($entry.package)"] = $entry
    }

    foreach ($package in $Packages)
    {
        if ($package -match '^[A-Za-z0-9][A-Za-z0-9+_.-]*$')
        {
            $key = "$Manager`:$package"
            $entries[$key] = [ordered]@{ manager = $Manager; package = $package }
        }
    }

    $state.systemPackages = @($entries.Values | Sort-Object manager, package)
    Save-GameWipSetupState -State $state
}

function Initialize-GameWipSetupProcessPath
{
    Update-GameWipProcessPath
}

function Resolve-GameWipSetupCommand
{
    param([Parameter(Mandatory = $true)][string]$Name)

    $command = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue |
        Select-Object -First 1

    if ($null -eq $command)
    {
        return $null
    }

    return [string]$command.Source
}

function Test-GameWipSetupCommand
{
    param([Parameter(Mandatory = $true)][string]$Name)
    return $null -ne (Resolve-GameWipSetupCommand -Name $Name)
}

function Invoke-GameWipSetupNative
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [int[]]$AllowedExitCodes = @(0),
        [switch]$InheritConsole,
        [switch]$PreserveConsoleSession
    )
    $result = Invoke-GameWipNative `
        -Name ([IO.Path]::GetFileNameWithoutExtension($FilePath)) `
        -FilePath $FilePath `
        -Arguments $ArgumentList `
        -AllowedExitCodes $AllowedExitCodes `
        -InheritConsole:$InheritConsole `
        -PreserveConsoleSession:$PreserveConsoleSession
    return [int]$result.ExitCode
}

function Assert-GameWipSetupWindows
{
    if ($env:OS -ne 'Windows_NT')
    {
        throw 'The Windows setup entry point can only run on Windows.'
    }

    $build = [Environment]::OSVersion.Version.Build

    if ($build -lt 22000)
    {
        Write-Warning "GameWIP officially supports Windows 11; detected Windows build $build."
    }
}

function Assert-GameWipSetupHost
{
    if (Test-GameWipWindowsHost)
    {
        Assert-GameWipSetupWindows
        return
    }

    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
    {
        if (-not (Get-Command Get-GameWipLinuxDistribution -ErrorAction SilentlyContinue))
        {
            throw 'Linux setup support was not loaded.'
        }

        $null = Get-GameWipLinuxDistribution
        return
    }

    throw 'Setup supports Windows and Linux hosts only.'
}

function Assert-GameWipSetupRepository
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    foreach ($file in @('CMakeLists.txt', 'CMakePresets.json'))
    {
        if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot $file)))
        {
            throw "The setup script is not inside a valid GameWIP checkout; missing $file."
        }
    }
}
