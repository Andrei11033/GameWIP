# GameWIP npm tool provider.

function Get-GameWipNpmPackageLatestVersion
{
    param([Parameter(Mandatory = $true)][string]$Package)
    $query = Get-GameWipNpmPackageLatestQuery -Package $Package
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Get-GameWipNpmToolLatestVersion
{
    param([hashtable]$Tool)
    return Get-GameWipNpmPackageLatestVersion -Package ([string]$Tool.provider.package)
}

function Get-GameWipNpmGlobalModuleRoot
{
    if (Test-GameWipWindowsHost)
    {
        return Join-Path ((Get-GameWipManagedToolRoot)) 'npm\lib\node_modules'
    }
    $npm = Resolve-GameWipToolCommand -Command npm
    if ($null -eq $npm)
    {
        throw 'npm is unavailable; cannot resolve the global module root.'
    }
    $result = Invoke-GameWipProcess -FilePath $npm -Arguments @('root', '--global') -OutputMode LogOnly -TimeoutSeconds 20
    $output = ($result.Stdout -join "`n").Trim()
    if ($result.ExitCode -ne 0 -or [string]::IsNullOrWhiteSpace($output))
    {
        throw 'Unable to resolve the global npm module root.'
    }
    return $output
}

function Install-GameWipNpmTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    Initialize-GameWipManagedToolRoot
    $root = Join-Path ((Get-GameWipManagedToolRoot)) 'npm'
    $specification = if ($Version)
    {
        "$($Tool.provider.package)@$Version"
    }
    else
    {
        [string]$Tool.provider.package
    }
    $specifications = @($specification)
    foreach ($dependency in $(if ($Tool.provider.Contains('dependencies'))
            {
                @($Tool.provider.dependencies)
            }
            else
            {
                @()
            }))
    {
        $specifications += "$($dependency.package)@$($dependency.version)"
    }
    $npm = Resolve-GameWipToolCommand -Command npm
    if ($null -eq $npm)
    {
        throw "npm is unavailable for '$($Tool.id)'. Run setup.bat repair."
    }
    Invoke-GameWipProviderNative -Name "npm-install-$($Tool.id)" -FilePath $npm -Arguments (@('install', '--global', '--prefix', $root, '--no-audit', '--no-fund') + $specifications) | Out-Null
}
