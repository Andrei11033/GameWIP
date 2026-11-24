# Windows-specific helper commands and managed executable shims.

# ------------------------------------------------------------
# Helper commands
# ------------------------------------------------------------

function Get-GameWipProjectHelperCommand
{
    return '.\gamewip.bat'
}

function Get-GameWipSetupHelperCommand
{
    return '.\setup.bat'
}

# ------------------------------------------------------------
# Tool paths and executable names
# ------------------------------------------------------------

function Get-GameWipExecutableNames
{
    param([Parameter(Mandatory = $true)][string]$Command)

    return @("$Command.exe", "$Command.com", "$Command.cmd", "$Command.bat", "$Command.ps1")
}

function Get-GameWipPathComparison
{
    return [StringComparison]::OrdinalIgnoreCase
}

function Get-GameWipPathComparer
{
    return [StringComparer]::OrdinalIgnoreCase
}

function Get-GameWipPythonEnvironmentExecutableDirectory
{
    param([Parameter(Mandatory = $true)][string]$Root)
    return Join-Path $Root 'Scripts'
}

function Get-GameWipPythonEnvironmentInterpreterPath
{
    param([Parameter(Mandatory = $true)][string]$Root)
    return Join-Path (Get-GameWipPythonEnvironmentExecutableDirectory -Root $Root) 'python.exe'
}

# ------------------------------------------------------------
# Managed tool path and command shim
# ------------------------------------------------------------

function Get-GameWipWindowsManagedToolPath
{
    $ucrtBin = Join-Path (Get-GameWipMsys2Root) 'ucrt64/bin'

    if (Test-Path -LiteralPath $ucrtBin -PathType Container)
    {
        return $ucrtBin
    }
}

function Write-GameWipManagedToolShim
{
    param(
        [Parameter(Mandatory = $true)][string]$ToolId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$ExecutableName
    )

    $root = Get-GameWipManagedToolRoot
    $binRoot = Join-Path $root 'bin'
    New-Item -ItemType Directory -Force -Path $binRoot | Out-Null

    # Point the command shim to the pinned managed executable.
    $relativeExecutable = "..\tools\$ToolId\$Version\$ExecutableName"
    Write-GameWipTextAtomic `
        -Path (Join-Path $binRoot "$ToolId.cmd") `
        -Content "@echo off`r`n`"%~dp0$relativeExecutable`" %*`r`n"
}
