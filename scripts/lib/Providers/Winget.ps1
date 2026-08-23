# GameWIP Winget tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipWingetToolLatestVersion
{
    param([hashtable]$Tool)
    $output = & winget show --id $Tool.provider.package --exact --accept-source-agreements 2>&1 | Out-String
    $match = [regex]::Match($output, '(?m)^Version:\s*(\S+)')
    if ($match.Success)
    {
        return $match.Groups[1].Value
    }
    return $null
}

function Install-GameWipWingetTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    $arguments = @('upgrade', '--id', $Tool.provider.package, '--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements')
    & winget @arguments
    if ($LASTEXITCODE -ne 0)
    {
        throw "WinGet failed to update '$($Tool.id)'."
    }
}
