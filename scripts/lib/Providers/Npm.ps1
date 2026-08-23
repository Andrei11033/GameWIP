# GameWIP Npm tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipNpmToolLatestVersion
{
    param([hashtable]$Tool)
    $output = (& npm view $Tool.provider.package version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0)
    {
        return $null
    }
    return $output
}

function Install-GameWipNpmTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $root = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'npm'
    $specification = if ($Version)
    {
        "$($Tool.provider.package)@$Version"
    }
    else
    {
        [string]$Tool.provider.package
    }
    $specifications = @($specification)
    foreach ($dependency in @($Tool.provider.dependencies))
    {
        $specifications += "$($dependency.package)@$($dependency.version)"
    }
    & npm install --global --prefix $root --no-audit --no-fund @specifications
    if ($LASTEXITCODE -ne 0)
    {
        throw "npm failed to install '$($Tool.id)'."
    }
}
