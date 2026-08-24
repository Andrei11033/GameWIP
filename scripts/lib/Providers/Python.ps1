# GameWIP Python tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipPythonToolLatestVersion
{
    param([hashtable]$Tool)
    try { return [string](Invoke-RestMethod -Uri "https://pypi.org/pypi/$($Tool.provider.package)/json").info.version }
    catch { return $null }
}

function Get-GameWipPythonEnvironmentInterpreterPath
{
    param([Parameter(Mandatory = $true)][string]$Root)
    $windowsPath = if ($Root.EndsWith('\')) { "$($Root)Scripts\python.exe" } else { "$Root\Scripts\python.exe" }
    if (Test-Path -LiteralPath $windowsPath) { return $windowsPath }
    $msysPath = if ($Root.EndsWith('\')) { "$($Root)bin\python.exe" } else { "$Root\bin\python.exe" }
    if (Test-Path -LiteralPath $msysPath) { return $msysPath }
    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)) { return $windowsPath }
    return if ($Root.EndsWith('/')) { "$($Root)bin/python" } else { "$Root/bin/python" }
}

function Test-GameWipPythonEnvironmentIsMsys
{
    param([Parameter(Mandatory = $true)][string]$PythonPath)
    if (-not (Test-Path -LiteralPath $PythonPath)) { return $false }
    $output = @(& $PythonPath -c "import sys; print(sys.prefix)" 2>&1)
    if ($LASTEXITCODE -ne 0) { return $false }
    $prefix = ($output | Out-String).Trim()
    return $prefix -ilike '*\msys*' -or $prefix -ilike '*\ucrt64*'
}

function New-GameWipPythonToolEnvironment
{
    param(
        [Parameter(Mandatory = $true)][string]$SystemPython,
        [Parameter(Mandatory = $true)][string]$Root
    )
    & $SystemPython -m venv $Root
    if ($LASTEXITCODE -ne 0) { throw 'Could not create the persistent GameWIP Python environment.' }
}

function Install-GameWipPythonPackageSpecification
{
    param(
        [Parameter(Mandatory = $true)][string]$Python,
        [Parameter(Mandatory = $true)][string]$Specification
    )
    & $Python -m pip install --upgrade $Specification
    if ($LASTEXITCODE -ne 0) { throw "pip failed to install '$Specification'." }
}

function Install-GameWipPythonTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    if (-not [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
    {
        throw "Persistent Python-provider installation is Windows GameWIPTools state; CI must provision '$($Tool.id)' ephemerally from the registry pin."
    }
    Initialize-GameWipManagedToolRoot
    $root = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'python'
    $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root

    if (Test-Path -LiteralPath $root)
    {
        if (Test-Path -LiteralPath $environmentPythonPath)
        {
            if (Test-GameWipPythonEnvironmentIsMsys -PythonPath $environmentPythonPath)
            {
                Write-Host "Migrating legacy MSYS2 Python provider environment to native Windows CPython..."
                Remove-Item -LiteralPath $root -Recurse -Force
            }
        }
    }

    $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root
    if (-not (Test-Path -LiteralPath $environmentPythonPath))
    {
        $systemPython = Resolve-GameWipPythonProviderHost
        New-GameWipPythonToolEnvironment -SystemPython $systemPython.Path -Root $root
        $environmentPythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root
    }
    $specification = if ($Version) { "$($Tool.provider.package)==$Version" } else { [string]$Tool.provider.package }
    Install-GameWipPythonPackageSpecification -Python $environmentPythonPath -Specification $specification
}
