# WinGet tool provider support.

# ------------------------------------------------------------
# WinGet discovery and installation
# ------------------------------------------------------------

function Test-GameWipWingetNoUpdateExitCode
{
    param([Parameter(Mandatory = $true)][int]$ExitCode)
    return $ExitCode -eq -1978335189
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

function Get-GameWipWingetInstallStatus
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $listed = Invoke-GameWipProcess -FilePath winget -Arguments @('list', '--id', $Id, '--exact', '--accept-source-agreements') -OutputMode LogOnly -TimeoutSeconds 60
    $installed = $listed.ExitCode -eq 0 -and (($listed.Stdout -join "`n") -match [regex]::Escape($Id))

    return [pscustomobject]@{
        Installed = $installed
        Result = $listed
    }
}

function Install-GameWipWingetTool
{
    param(
        [hashtable]$Tool,
        [AllowNull()][string]$Version,
        [AllowNull()]$InstalledStatus
    )
    $null = $Version
    $status = if ($null -ne $InstalledStatus)
    {
        $InstalledStatus
    }
    else
    {
        Get-GameWipWingetInstallStatus -Id ([string]$Tool.provider.package)
    }
    $installed = [bool]$status.Installed
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
            $arguments += ([string]$argument).Replace('{msys2Root}', (Get-GameWipMsys2Root))
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
