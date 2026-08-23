# GameWIP Python tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipPythonToolLatestVersion
{
    param([hashtable]$Tool)
    try
    {
        return [string](Invoke-RestMethod -Uri "https://pypi.org/pypi/$($Tool.provider.package)/json").info.version
    }
    catch
    {
        return $null
    }
}

function Install-GameWipPythonTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $root = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'python'
    $pythonPath = Join-Path $root 'bin\python.exe'
    if (-not (Test-Path -LiteralPath $pythonPath))
    {
        $systemPython = Resolve-GameWipPython
        & $systemPython.Path -m venv $root
        if ($LASTEXITCODE -ne 0)
        {
            throw 'Could not create the persistent GameWIP Python environment.'
        }
    }
    $specification = if ($Version)
    {
        "$($Tool.provider.package)==$Version"
    }
    else
    {
        [string]$Tool.provider.package
    }
    & $pythonPath -m pip install --upgrade $specification
    if ($LASTEXITCODE -ne 0)
    {
        throw "pip failed to install '$($Tool.id)'."
    }
}
