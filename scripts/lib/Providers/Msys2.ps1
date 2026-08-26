# GameWIP MSYS2 tool provider.

function Get-GameWipMsys2ToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Install-GameWipMsys2Tool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    $bash = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash))
    {
        throw 'MSYS2 is not installed; run setup.bat repair.'
    }
    $dependencies = if ($Tool.provider.Contains('dependencies'))
    {
        @($Tool.provider.dependencies)
    }
    else
    {
        @()
    }
    $packages = @([string]$Tool.provider.package) + @($dependencies | ForEach-Object { [string]$_.package })
    $packageText = @($packages | Sort-Object -Unique | ForEach-Object { "'$_'" }) -join ' '
    Invoke-GameWipProviderNative -Name "pacman-install-$($Tool.id)" -FilePath $bash -Arguments @('-lc', "pacman --noconfirm --needed -S $packageText") | Out-Null
}
