# Shared setup state and presentation helpers. Native execution is owned by scripts/lib/Process.ps1.

Set-StrictMode -Version Latest

function Get-GameWipSetupState
{
    $defaultState = [ordered]@{ schemaVersion = 1; wingetPackages = @(); vscodeExtensions = @(); msys2InstalledBySetup = $false }
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
    }
    catch
    {
        $script:SetupStateReadStatus = 'corrupt'
        Add-GameWipOperationWarning -Message "Disposable setup state is corrupt and will be ignored: $($_.Exception.Message)"
        return $defaultState
    }
    $script:SetupStateReadStatus = 'valid'
    [string[]]$wingetPackages = if ($state.PSObject.Properties['wingetPackages'])
    {
        @($state.wingetPackages | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    }
    else
    {
        @()
    }
    [string[]]$vscodeExtensions = if ($state.PSObject.Properties['vscodeExtensions'])
    {
        @($state.vscodeExtensions | ForEach-Object { [string]$_ } | Where-Object { -not [string]::IsNullOrWhiteSpace($_) } | Sort-Object -Unique)
    }
    else
    {
        @()
    }
    $ownsMsys2 = if ($state.PSObject.Properties['msys2InstalledBySetup'])
    {
        [bool]$state.msys2InstalledBySetup
    }
    else
    {
        $false
    }
    return [ordered]@{
        schemaVersion = 1
        wingetPackages = @($wingetPackages | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        vscodeExtensions = @($vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace($_) })
        msys2InstalledBySetup = $ownsMsys2
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

function Write-GameWipSetupSection
{
    param([Parameter(Mandatory = $true)][string]$Title)
    Write-GameWipSection $Title
}

function Initialize-GameWipSetupProcessPath
{
    $parts = @([Environment]::GetEnvironmentVariable('Path', 'Machine'), [Environment]::GetEnvironmentVariable('Path', 'User')) |
        Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    $env:Path = $parts -join [IO.Path]::PathSeparator
}

function Test-GameWipSetupCommand
{
    param([Parameter(Mandatory = $true)][string]$Name)
    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-GameWipSetupNative
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [int[]]$AllowedExitCodes = @(0)
    )
    $result = Invoke-GameWipNative `
        -Name ([IO.Path]::GetFileNameWithoutExtension($FilePath)) `
        -FilePath $FilePath `
        -Arguments $ArgumentList `
        -AllowedExitCodes $AllowedExitCodes `
        -OutputMode Stream
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

function Assert-GameWipSetupRepository
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)
    foreach ($file in @('CMakeLists.txt', 'CMakePresets.json', '.gitmodules'))
    {
        if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot $file)))
        {
            throw "The setup script is not inside a valid GameWIP checkout; missing $file."
        }
    }
}
