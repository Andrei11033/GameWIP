# GameWIP MSYS2 tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipMsys2ToolLatestVersion
{
    param([hashtable]$Tool)
    $bash = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash)) { return $null }
    $output = & $bash -lc "pacman -Si '$($Tool.provider.package)' 2>/dev/null" | Out-String
    $match = [regex]::Match($output, '(?m)^Version\s*:\s*([^\s-]+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

function Install-GameWipMsys2Tool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    $bash = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'usr\bin\bash.exe'
    if (-not (Test-Path -LiteralPath $bash)) { throw 'MSYS2 is not installed; run setup.bat repair.' }
    $providerDependencies = if ($Tool.provider.Contains('dependencies')) { @($Tool.provider.dependencies) } else { @() }
    $packages = @([string]$Tool.provider.package) + @($providerDependencies | ForEach-Object { [string]$_.package })
    $arguments = @($packages | Sort-Object -Unique | ForEach-Object { "'$_'" }) -join ' '
    & $bash -lc "pacman --noconfirm --needed -S $arguments"
    if ($LASTEXITCODE -ne 0) { throw "pacman failed to install '$($Tool.id)' and its declared requirements." }
}
