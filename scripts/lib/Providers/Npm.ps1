# GameWIP Npm tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipNpmPackageLatestVersion
{
    param([Parameter(Mandatory = $true)][string]$Package)
    $npm = Resolve-GameWipToolCommand -Command 'npm'
    if ($null -eq $npm) { return $null }
    $output = (& $npm view $Package version 2>&1 | Out-String).Trim()
    if ($LASTEXITCODE -ne 0) { return $null }
    return $output
}

function Get-GameWipNpmToolLatestVersion
{
    param([hashtable]$Tool)
    return Get-GameWipNpmPackageLatestVersion -Package ([string]$Tool.provider.package)
}

function Install-GameWipNpmTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    Initialize-GameWipManagedToolRoot
    $root = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'npm'
    $specification = if ($Version) { "$($Tool.provider.package)@$Version" } else { [string]$Tool.provider.package }
    $specifications = @($specification)
    $providerDependencies = if ($Tool.provider.Contains('dependencies')) { @($Tool.provider.dependencies) } else { @() }
    foreach ($dependency in $providerDependencies) { $specifications += "$($dependency.package)@$($dependency.version)" }
    $npm = Resolve-GameWipToolCommand -Command 'npm'
    if ($null -eq $npm) { throw "npm is unavailable for '$($Tool.id)'. Run setup.bat repair." }
    & $npm install --global --prefix $root --no-audit --no-fund @specifications
    if ($LASTEXITCODE -ne 0) { throw "npm failed to install '$($Tool.id)'." }
}
