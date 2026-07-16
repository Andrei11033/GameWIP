Set-StrictMode -Version Latest

function Write-SetupSection
{
    param([Parameter(Mandatory = $true)][string]$Title)

    Write-Host ''
    Write-Host "=== $Title ===" -ForegroundColor Cyan
}

function Update-SetupProcessPath
{
    $machinePath = [Environment]::GetEnvironmentVariable('Path', 'Machine')
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $env:Path = "$machinePath;$userPath"
}

function Test-SetupCommand
{
    param([Parameter(Mandatory = $true)][string]$Name)

    return $null -ne (Get-Command $Name -ErrorAction SilentlyContinue)
}

function Invoke-SetupNative
{
    param(
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$ArgumentList = @(),
        [int[]]$AllowedExitCodes = @(0)
    )

    $displayArguments = @($ArgumentList | ForEach-Object {
        if ($_ -match '\s') { '"' + $_ + '"' } else { $_ }
    })
    Write-Host "  > $FilePath $($displayArguments -join ' ')" -ForegroundColor DarkGray
    $previousErrorActionPreference = $ErrorActionPreference
    try
    {
        $ErrorActionPreference = 'Continue'
        & $FilePath @ArgumentList 2>&1 | ForEach-Object { Write-Host "    $_" }
        $exitCode = $LASTEXITCODE
    }
    finally
    {
        $ErrorActionPreference = $previousErrorActionPreference
    }
    if ($AllowedExitCodes -notcontains $exitCode)
    {
        throw "Command failed with exit code ${exitCode}: $FilePath $($ArgumentList -join ' ')"
    }
    Write-Host "  < exit $exitCode" -ForegroundColor DarkGray
    return $exitCode
}

function Test-SetupWindows
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

function Test-SetupRepository
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
