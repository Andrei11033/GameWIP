# GameWIP Python tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.

function Get-GameWipPythonToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -eq 'resolved')
    {
        return $query.Version
    }
    return $null
}

function Get-GameWipPythonEnvironmentInterpreterPath
{
    param([Parameter(Mandatory = $true)][string]$Root)
    $windowsPath = if ($Root.EndsWith('\'))
    {
        "$($Root)Scripts\python.exe"
    }
    else
    {
        "$Root\Scripts\python.exe"
    }
    if (Test-Path -LiteralPath $windowsPath)
    {
        return $windowsPath
    }
    $msysPath = if ($Root.EndsWith('\'))
    {
        "$($Root)bin\python.exe"
    }
    else
    {
        "$Root\bin\python.exe"
    }
    if (Test-Path -LiteralPath $msysPath)
    {
        return $msysPath
    }
    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
    {
        return $windowsPath
    }
    return if ($Root.EndsWith('/')) { "$($Root)bin/python" } else { "$Root/bin/python" }
}

function Test-GameWipPythonEnvironmentIsMsys
{
    param([Parameter(Mandatory = $true)][string]$PythonPath)
    if (-not (Test-Path -LiteralPath $PythonPath))
    {
        return $false
    }

    # A native venv may itself live below C:\MSYS2\GameWIPTools. sys.prefix is
    # therefore not evidence of an MSYS2 interpreter. Inspect the base runtime
    # that created the venv instead.
    $query = Invoke-GameWipProcess -FilePath $PythonPath -Arguments @('-c', "import sys; print(sys.base_prefix); print(getattr(sys, '_base_executable', sys.executable))") -OutputMode LogOnly -TimeoutSeconds 15
    if ($query.ExitCode -ne 0 -or $query.Stdout.Count -lt 1)
    {
        return $false
    }
    foreach ($line in $query.Stdout)
    {
        $path = ([string]$line).Trim().Replace('/', '\')
        if ($path -match '(?i)\\(?:msys64?|ucrt64|clang64)\\')
        {
            return $true
        }
    }
    return $false
}

function Invoke-GameWipPythonProviderProcess
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    Invoke-GameWipNative -Name $Name -FilePath $FilePath -Arguments $Arguments -TimeoutSeconds 600
}

function New-GameWipPythonToolEnvironment
{
    param(
        [Parameter(Mandatory = $true)][string]$SystemPython,
        [Parameter(Mandatory = $true)][string]$Root
    )
    Invoke-GameWipPythonProviderProcess -Name 'python-provider-venv' -FilePath $SystemPython -Arguments @('-m', 'venv', $Root)
}

function Install-GameWipPythonPackageSpecification
{
    param(
        [Parameter(Mandatory = $true)][string]$Python,
        [Parameter(Mandatory = $true)][string]$Specification
    )
    Invoke-GameWipPythonProviderProcess -Name 'python-provider-pip' -FilePath $Python -Arguments @('-m', 'pip', 'install', '--upgrade', $Specification)
}

function Install-GameWipPythonTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    Initialize-GameWipManagedToolRoot
    $root = Join-Path ((Get-GameWipManagedToolRoot)) 'python'
    $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root

    if ((Test-Path -LiteralPath $environmentPythonPath) -and (Test-GameWipPythonEnvironmentIsMsys -PythonPath $environmentPythonPath))
    {
        Write-Host 'Migrating legacy MSYS2 Python provider environment to native Windows CPython...'
        Invoke-GameWipOwnedTreeRemoval -Path $root -OwnedRoot ((Get-GameWipManagedToolRoot))
        New-Item -ItemType Directory -Force -Path $root | Out-Null
    }

    $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root
    if (-not (Test-Path -LiteralPath $environmentPythonPath))
    {
        $systemPython = Resolve-GameWipPythonProviderHost
        New-GameWipPythonToolEnvironment -SystemPython $systemPython.Path -Root $root
        $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root
    }

    $specification = if ($Version)
    {
        "$($Tool.provider.package)==$Version"
    }
    else
    {
        [string]$Tool.provider.package
    }
    Install-GameWipPythonPackageSpecification -Python $environmentPythonPath -Specification $specification
}
