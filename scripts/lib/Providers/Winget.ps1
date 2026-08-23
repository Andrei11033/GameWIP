# GameWIP WinGet tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipWingetToolLatestVersion
{
    param([hashtable]$Tool)
    $output = & winget show --id $Tool.provider.package --exact --accept-source-agreements 2>&1 | Out-String
    $match = [regex]::Match($output, '(?m)^Version:\s*(\S+)')
    if ($match.Success) { return $match.Groups[1].Value }
    return $null
}

function Install-GameWipWingetTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    $listed = & winget list --id $Tool.provider.package --exact --accept-source-agreements 2>&1 | Out-String
    $installed = $LASTEXITCODE -eq 0 -and $listed -match [regex]::Escape([string]$Tool.provider.package)
    $verb = if ($installed) { 'upgrade' } else { 'install' }
    $arguments = @($verb, '--id', [string]$Tool.provider.package, '--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements')
    if (-not $installed -and $Tool.provider.Contains('installArguments'))
    {
        foreach ($argument in @($Tool.provider.installArguments))
        {
            $arguments += ([string]$argument).Replace('{msys2Root}', [string]$ProjectConfig.managedEnvironment.msys2Root)
        }
    }
    & winget @arguments
    if ($LASTEXITCODE -ne 0) { throw "WinGet failed to install/update '$($Tool.id)'." }

    # WinGet updates persistent environment state, not necessarily this process.
    # Refresh PATH so later setup stages can use a newly installed provider.
    $pathParts = @(
        [Environment]::GetEnvironmentVariable('Path', 'Machine'),
        [Environment]::GetEnvironmentVariable('Path', 'User')
    ) | Where-Object { -not [string]::IsNullOrWhiteSpace($_) }
    $env:Path = $pathParts -join [IO.Path]::PathSeparator
}
