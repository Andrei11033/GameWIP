Set-StrictMode -Version Latest

function Get-GameWipSetupState
{
    if (-not (Test-Path -LiteralPath $script:SetupStatePath))
    {
        return [ordered]@{ schemaVersion = 1; wingetPackages = @(); vscodeExtensions = @(); msys2InstalledBySetup = $false }
    }
    $state = Get-Content -LiteralPath $script:SetupStatePath -Raw | ConvertFrom-Json
    [object[]]$wingetPackages = if ($state.PSObject.Properties['wingetPackages'])
    {
        @($state.wingetPackages)
    }
    else
    {
        @()
    }
    [object[]]$vscodeExtensions = if ($state.PSObject.Properties['vscodeExtensions'])
    {
        @($state.vscodeExtensions)
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
        wingetPackages = $wingetPackages
        vscodeExtensions = $vscodeExtensions
        msys2InstalledBySetup = $ownsMsys2
    }
}

function Add-GameWipOwnedVsCodeExtension
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $state = Get-GameWipSetupState
    $state.vscodeExtensions = @(@($state.vscodeExtensions) + @($Id) | Sort-Object -Unique)
    Save-GameWipSetupState -State $state
}

function Save-GameWipSetupState
{
    param([Parameter(Mandatory = $true)]$State)
    New-Item -ItemType Directory -Force -Path (Split-Path -Parent $script:SetupStatePath) | Out-Null
    $State | ConvertTo-Json | Set-Content -LiteralPath $script:SetupStatePath -Encoding UTF8
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

    Write-Host ''
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Initialize-GameWipSetupProcessPath
{
    $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machinePath;$userPath"
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

    $displayArguments = @($ArgumentList | ForEach-Object {
            if ($_ -match '\s')
            {
                '"' + $_ + '"'
            }
            else
            {
                $_
            }
        })
    $commandLine = "$FilePath $($displayArguments -join ' ')".Trim()
    $step = $null
    if ($null -ne $script:SetupRun)
    {
        $step = Initialize-GameWipToolRunStep -Run $script:SetupRun -Name ([IO.Path]::GetFileNameWithoutExtension($FilePath)) -CommandLine $commandLine
    }
    Write-Host "  > $commandLine" -ForegroundColor DarkGray
    if ($null -ne $step)
    {
        Write-Host "    log: $($step.LogPath)" -ForegroundColor DarkGray
    }
    $previousErrorActionPreference = $ErrorActionPreference
    $exitCode = 0
    try
    {
        $ErrorActionPreference = 'Continue'
        if ($null -ne $step)
        {
            & $FilePath @ArgumentList 2>&1 | ForEach-Object { [string]$_ } | Tee-Object -FilePath $step.LogPath | ForEach-Object { Write-Host "    $_" }
        }
        else
        {
            & $FilePath @ArgumentList 2>&1 | ForEach-Object { Write-Host "    $_" }
        }
        $exitCode = if ($null -ne $LASTEXITCODE)
        {
            [int]$LASTEXITCODE
        }
        else
        {
            0
        }
    }
    catch
    {
        $exitCode = 1
        if ($null -ne $step)
        {
            Complete-GameWipToolRunStep -Run $script:SetupRun -Step $step -ExitCode $exitCode
        }
        throw
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($null -ne $step)
    {
        Complete-GameWipToolRunStep -Run $script:SetupRun -Step $step -ExitCode $exitCode
    }
    if ($AllowedExitCodes -notcontains $exitCode)
    {
        throw "Command failed with exit code ${exitCode}: $FilePath $($ArgumentList -join ' ')"
    }
    Write-Host "  < exit $exitCode" -ForegroundColor DarkGray
    return $exitCode
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

    $required = @('CMakeLists.txt', 'CMakePresets.json', '.gitmodules')
    foreach ($file in $required)
    {
        if (-not (Test-Path -LiteralPath (Join-Path $RepositoryRoot $file)))
        {
            throw "The setup script is not inside a valid GameWIP checkout; missing $file."
        }
    }
}
