# Shared setup state and presentation helpers. Native execution stays in scripts/lib/Process.ps1.

# ------------------------------------------------------------
# Setup state and ownership metadata
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipSetupState
{
    $defaultState = [ordered]@{ schemaVersion = 1; wingetPackages = @(); vscodeExtensions = @(); systemPackages = @(); msys2InstalledBySetup = $false }

    if (-not (Test-Path -LiteralPath $script:SetupStatePath -PathType Leaf))
    {
        $script:SetupStateReadStatus = 'missing'
        return $defaultState
    }

    try
    {
        $state = Get-Content -LiteralPath $script:SetupStatePath -Raw | ConvertFrom-Json

        if ($state.schemaVersion -ne 1)
        {
            throw "Unsupported setup state schemaVersion '$($state.schemaVersion)'."
        }

        foreach ($field in @('wingetPackages', 'vscodeExtensions', 'msys2InstalledBySetup'))
        {
            if ($null -eq $state.PSObject.Properties[$field])
            {
                throw "Setup state is missing required ownership field '$field'."
            }
        }
    }

    catch
    {
        $script:SetupStateReadStatus = 'corrupt'
        Add-GameWipOperationWarning -Message "Disposable setup state is corrupt and will be ignored: $($_.Exception.Message)"
        return $defaultState
    }

    $script:SetupStateReadStatus = 'valid'
    [string[]]$wingetPackages = @($state.wingetPackages | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    [string[]]$vscodeExtensions = @($state.vscodeExtensions | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    $systemPackages = [System.Collections.Generic.List[object]]::new()
    $systemPackageKeys = [System.Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $storedSystemPackages = if ($null -ne $state.PSObject.Properties['systemPackages'])
    {
        @($state.systemPackages)
    }
    else
    {
        @()
    }

    foreach ($entry in $storedSystemPackages)
    {
        $manager = [string]$entry.manager
        $package = [string]$entry.package
        $key = "$manager`:$package"

        if ($manager -in @('apt', 'pacman') -and $package -match '^[A-Za-z0-9][A-Za-z0-9+_.-]*$' -and $systemPackageKeys.Add($key))
        {
            $systemPackages.Add([ordered]@{ manager = $manager; package = $package }) | Out-Null
        }
    }

    return [ordered]@{
        schemaVersion = 1
        wingetPackages = @($wingetPackages | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        vscodeExtensions = @($vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        systemPackages = @($systemPackages.ToArray() | Sort-Object manager, package)
        msys2InstalledBySetup = [bool]$state.msys2InstalledBySetup
    }
}

function Save-GameWipSetupState
{
    param([Parameter(Mandatory = $true)]$State)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $script:SetupStatePath) | Out-Null
    Write-GameWipJsonAtomic -Path $script:SetupStatePath -Value $State -Depth 8
    $script:SetupStateReadStatus = 'valid'
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
