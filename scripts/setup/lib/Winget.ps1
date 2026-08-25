# GameWIP setup WinGet package operations. All native calls use the shared process layer.

Set-StrictMode -Version Latest

function Test-GameWipWingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)
    $result = Invoke-GameWipProcess -FilePath winget -Arguments @('list', '--id', $Id, '--exact', '--source', 'winget', '--accept-source-agreements') -OutputMode LogOnly -TimeoutSeconds 60
    return $result.ExitCode -eq 0 -and (($result.Stdout -join "`n") -match [regex]::Escape($Id))
}

function Install-GameWipWingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id, [string]$Override)
    $arguments = @('install', '--id', $Id, '--exact', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements', '--silent')
    if ($Override)
    {
        $arguments += @('--override', $Override)
    }
    $wasInstalled = Test-GameWipWingetPackage -Id $Id
    Invoke-GameWipSetupNative -FilePath winget -ArgumentList $arguments | Out-Null
    if (-not $wasInstalled)
    {
        Add-GameWipOwnedWingetPackage -Id $Id
    }
    Initialize-GameWipSetupProcessPath
}

function Uninstall-GameWipWingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)
    if (-not (Test-GameWipWingetPackage -Id $Id))
    {
        Write-Host "  Already absent: $Id"; return
    }
    Invoke-GameWipSetupNative -FilePath winget -ArgumentList @('uninstall', '--id', $Id, '--exact', '--source', 'winget', '--accept-source-agreements', '--silent') | Out-Null
}

function Invoke-GameWipWingetPackageUpdate
{
    param([Parameter(Mandatory = $true)][string]$Id, [string]$Override)
    if (-not (Test-GameWipWingetPackage -Id $Id))
    {
        Install-GameWipWingetPackage -Id $Id -Override $Override; return
    }
    $arguments = @('upgrade', '--id', $Id, '--exact', '--source', 'winget', '--accept-package-agreements', '--accept-source-agreements', '--silent')
    if ($Override)
    {
        $arguments += @('--override', $Override)
    }
    Invoke-GameWipSetupNative -FilePath winget -ArgumentList $arguments -AllowedExitCodes @(0, -1978335189) | Out-Null
    Initialize-GameWipSetupProcessPath
}

function Install-GameWipConfiguredWingetToolSet
{
    param([Parameter(Mandatory = $true)][array]$Packages, [switch]$Update)
    foreach ($package in $Packages)
    {
        $present = if ($package.ContainsKey('Command'))
        {
            Test-GameWipSetupCommand -Name $package.Command
        }
        elseif ($package.ContainsKey('Path'))
        {
            Test-Path -LiteralPath $package.Path
        }
        else
        {
            $false
        }
        if ($Update -and (Test-GameWipWingetPackage -Id $package.Id))
        {
            Invoke-GameWipWingetPackageUpdate -Id $package.Id
        }
        elseif ($Update -and $present)
        {
            Add-GameWipOperationWarning -Message "$($package.Name) exists outside WinGet; preserving it to avoid duplicate installation."
        }
        elseif (-not $present)
        {
            Install-GameWipWingetPackage -Id $package.Id
        }
        else
        {
            Write-Host "  Ready: $($package.Name)"
        }
    }
}
