# Shared setup host and command helpers. Native execution stays in scripts/lib/Process.ps1.

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Host and command helpers
# ------------------------------------------------------------

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
