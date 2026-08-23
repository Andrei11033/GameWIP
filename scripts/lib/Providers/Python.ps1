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
    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)) { return Join-Path $Root 'Scripts\python.exe' }
    return Join-Path $Root 'bin/python'
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
    $pythonPath = Get-GameWipPythonEnvironmentInterpreterPath -Root $root
    if (-not (Test-Path -LiteralPath $pythonPath))
    {
        $systemPython = Resolve-GameWipPython
        & $systemPython.Path -m venv $root
        if ($LASTEXITCODE -ne 0) { throw 'Could not create the persistent GameWIP Python environment.' }
    }
    $specification = if ($Version) { "$($Tool.provider.package)==$Version" } else { [string]$Tool.provider.package }
    & $pythonPath -m pip install --upgrade $specification
    if ($LASTEXITCODE -ne 0) { throw "pip failed to install '$($Tool.id)'." }
}
