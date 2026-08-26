# GameWIP WinGet tool provider.

function Test-GameWipWingetNoUpdateExitCode
{
    param([Parameter(Mandatory = $true)][int]$ExitCode); return $ExitCode -eq -1978335189
}

function Get-GameWipWingetToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Install-GameWipWingetTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    $listed = Invoke-GameWipProcess -FilePath winget -Arguments @('list', '--id', [string]$Tool.provider.package, '--exact', '--accept-source-agreements') -OutputMode LogOnly -TimeoutSeconds 60
    $installed = $listed.ExitCode -eq 0 -and (($listed.Stdout -join "`n") -match [regex]::Escape([string]$Tool.provider.package))
    $verb = if ($installed)
    {
        'upgrade'
    }
    else
    {
        'install'
    }
    $arguments = @($verb, '--id', [string]$Tool.provider.package, '--exact', '--silent', '--accept-package-agreements', '--accept-source-agreements')
    if (-not $installed -and $Tool.provider.Contains('installArguments'))
    {
        foreach ($argument in @($Tool.provider.installArguments))
        {
            $arguments += ([string]$argument).Replace('{msys2Root}', [string]$ProjectConfig.managedEnvironment.msys2Root)
        }
    }
    $allowed = @(0)
    if ($verb -eq 'upgrade')
    {
        $allowed += -1978335189
    }
    $exitCode = Invoke-GameWipProviderNative -Name "winget-$verb-$($Tool.id)" -FilePath winget -Arguments $arguments -AllowedExitCodes $allowed
    if (Test-GameWipWingetNoUpdateExitCode -ExitCode $exitCode)
    {
        Write-Host "  Already current: $($Tool.name)"
    }
    Update-GameWipProcessPath
}
