# Python tool-provider support for the project helper.

# ------------------------------------------------------------
# Python environment and package operations
# ------------------------------------------------------------

function Get-GameWipPythonToolLatestVersion
{
    param([hashtable]$Tool)
    $queryTool = $Tool

    if ((Test-GameWipWindowsHost) -and $Tool.provider.Contains('windowsMsys2'))
    {
        $nativeProvider = $Tool.provider.windowsMsys2
        $queryTool = @{
            provider = @{
                kind = 'msys2'
                environment = [string]$nativeProvider.environment
                package = [string]$nativeProvider.package
            }
            capabilities = @{ checkLatest = $true }
        }
    }
    $query = Get-GameWipToolLatestQuery -Tool $queryTool

    if ($query.State -eq 'resolved')
    {
        return $query.Version
    }

    return $null
}

function Invoke-GameWipPythonProviderProcess
{
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$FilePath,
        [string[]]$Arguments = @()
    )

    $cacheRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'pip'
    New-Item -ItemType Directory -Force -Path $cacheRoot | Out-Null
    Invoke-GameWipNative -Name $Name -FilePath $FilePath -Arguments $Arguments -Environment @{ PIP_CACHE_DIR = $cacheRoot } -TimeoutSeconds 600
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

    if ((Test-GameWipWindowsHost) -and $Tool.provider.Contains('windowsMsys2'))
    {
        $nativeProvider = $Tool.provider.windowsMsys2
        $nativeTool = @{
            id = [string]$Tool.id
            provider = @{
                kind = 'msys2'
                environment = [string]$nativeProvider.environment
                package = [string]$nativeProvider.package
            }
        }
        Install-GameWipMsys2Tool -Tool $nativeTool -Version $Version
    }

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
        throw "Managed Python environment was not created at '$environmentPythonPath'."
    }

    if ((Test-GameWipWindowsHost) -and $Tool.provider.Contains('windowsMsys2'))
    {
        return
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
