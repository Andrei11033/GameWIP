# Python tool-provider support for the project helper.

# ------------------------------------------------------------
# Python environment and package operations
# ------------------------------------------------------------

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
    if (Test-GameWipWindowsHost)
    {
        if ($Root.EndsWith('\'))
        {
            return "$($Root)bin\python.exe"
        }

        return "$Root\bin\python.exe"
    }

    if ($Root.EndsWith('/'))
    {
        return "$($Root)bin/python"
    }

    return "$Root/bin/python"
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
    $arguments = @('-m', 'venv')

    if (Test-GameWipWindowsHost)
    {
        $arguments += '--system-site-packages'
    }

    $arguments += $Root
    Invoke-GameWipPythonProviderProcess -Name 'python-provider-venv' -FilePath $SystemPython -Arguments $arguments
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

    if (-not (Test-Path -LiteralPath $environmentPythonPath))
    {
        $systemPython = Resolve-GameWipPython
        New-GameWipPythonToolEnvironment -SystemPython $systemPython.Path -Root $root
        $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root
    }

    if (-not (Test-Path -LiteralPath $environmentPythonPath))
    {
        throw "MSYS2 Python environment was not created at '$environmentPythonPath'."
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
