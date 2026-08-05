Set-StrictMode -Version Latest

function Invoke-Msys2
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string]$Command
    )

    $bash = Join-Path $MsysRoot 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash))
    {
        throw "MSYS2 bash was not found at $bash."
    }
    $env:CHERE_INVOKING = '1'
    Invoke-SetupNative -FilePath $bash -ArgumentList @('-lc', $Command) | Out-Null
}

function Test-Msys2Packages
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string[]]$Packages
    )

    return @(Get-MissingMsys2Packages -MsysRoot $MsysRoot -Packages $Packages).Count -eq 0
}

function Get-MissingMsys2Packages
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string[]]$Packages
    )

    $pacman = Join-Path $MsysRoot 'usr\bin\pacman.exe'
    if (-not (Test-Path -LiteralPath $pacman))
    {
        return $Packages
    }

    $installedPackages = & $pacman -Qq
    if ($LASTEXITCODE -ne 0)
    {
        return $Packages
    }
    $installed = [System.Collections.Generic.HashSet[string]]::new([string[]]$installedPackages)
    return @($Packages | Where-Object { -not $installed.Contains($_) })
}

function Install-GameWipMsys2Packages
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][hashtable]$PackageConfig,
        [switch]$Update
    )

    if ($Update)
    {
        Write-Host 'Updating the complete MSYS2 package database and system...'
        Invoke-Msys2 -MsysRoot $MsysRoot -Command 'pacman -Syu --noconfirm'
        Invoke-Msys2 -MsysRoot $MsysRoot -Command 'pacman -Syu --noconfirm'
    }

    $packages = @($PackageConfig.Common) + @($PackageConfig.Ucrt64) + @($PackageConfig.Clang64)
    $packageArguments = $packages -join ' '
    Invoke-Msys2 -MsysRoot $MsysRoot -Command "pacman --needed --noconfirm -S $packageArguments"

    if (-not (Test-Msys2Packages -MsysRoot $MsysRoot -Packages $packages))
    {
        throw 'One or more required MSYS2 packages failed verification.'
    }
}

function Test-GameWipMsys2Tools
{
    param(
        [Parameter(Mandatory = $true)][string]$MsysRoot,
        [Parameter(Mandatory = $true)][string]$CMakeVersionPattern
    )

    $ucrt = Join-Path $MsysRoot 'ucrt64\bin'
    $clang = Join-Path $MsysRoot 'clang64\bin'
    $tools = @(
        (Join-Path $ucrt 'g++.exe'),
        (Join-Path $ucrt 'gdb.exe'),
        (Join-Path $ucrt 'cmake.exe'),
        (Join-Path $ucrt 'ninja.exe'),
        (Join-Path $ucrt 'doxygen.exe'),
        (Join-Path $ucrt 'clang-tidy.exe'),
        (Join-Path $clang 'clang++.exe'),
        (Join-Path $clang 'cmake.exe'),
        (Join-Path $clang 'ninja.exe')
    )
    foreach ($tool in $tools)
    {
        if (-not (Test-Path -LiteralPath $tool))
        {
            throw "Required MSYS2 tool is missing: $tool"
        }
    }

    $version = & (Join-Path $ucrt 'cmake.exe') --version | Select-Object -First 1
    if ($version -notmatch $CMakeVersionPattern)
    {
        throw "GameWIP requires CMake 4.4.2 or newer on the 4.4 release line, but UCRT64 reports '$version'."
    }
    Write-Host "  Ready: UCRT64 and CLANG64 ($version)"
}
