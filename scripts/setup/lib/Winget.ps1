# GameWIP setup WinGet package installation, update, ownership recording, and uninstall behavior.

Set-StrictMode -Version Latest

function Test-GameWipWingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)

    & winget list --id $Id --exact --source winget --accept-source-agreements | Out-Null
    return $LASTEXITCODE -eq 0
}

function Install-GameWipWingetPackage
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string]$Override
    )

    $arguments = @(
        'install', '--id', $Id, '--exact', '--source', 'winget',
        '--accept-package-agreements', '--accept-source-agreements', '--silent'
    )
    if ($Override)
    {
        $arguments += @('--override', $Override)
    }
    $wasInstalled = Test-GameWipWingetPackage -Id $Id
    Invoke-GameWipSetupNative -FilePath 'winget' -ArgumentList $arguments | Out-Null
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
        Write-Host "  Already absent: $Id"
        return
    }
    Invoke-GameWipSetupNative -FilePath 'winget' -ArgumentList @(
        'uninstall', '--id', $Id, '--exact', '--source', 'winget',
        '--accept-source-agreements', '--silent'
    ) | Out-Null
}

function Invoke-GameWipWingetPackageUpdate
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string]$Override
    )

    if (-not (Test-GameWipWingetPackage -Id $Id))
    {
        Install-GameWipWingetPackage -Id $Id -Override $Override
        return
    }

    $arguments = @(
        'upgrade', '--id', $Id, '--exact', '--source', 'winget',
        '--accept-package-agreements', '--accept-source-agreements', '--silent'
    )
    if ($Override)
    {
        $arguments += @('--override', $Override)
    }

    & winget @arguments
    $exitCode = [int]$LASTEXITCODE
    if (Test-GameWipWingetNoUpdateExitCode -ExitCode $exitCode)
    {
        Write-Host "  Already current: $Id"
    }
    elseif ($exitCode -ne 0)
    {
        throw "WinGet failed to update '$Id' with exit code $exitCode."
    }
    Initialize-GameWipSetupProcessPath
}

function Install-GameWipConfiguredWingetToolSet
{
    param(
        [Parameter(Mandatory = $true)][array]$Packages,
        [switch]$Update
    )

    foreach ($package in $Packages)
    {
        Write-Host "Checking $($package.Name)..."
        $present = $false
        if ($package.ContainsKey('Command'))
        {
            $present = Test-GameWipSetupCommand -Name $package.Command
        }
        elseif ($package.ContainsKey('Path'))
        {
            $present = Test-Path -LiteralPath $package.Path
        }

        if ($Update -and (Test-GameWipWingetPackage -Id $package.Id))
        {
            Invoke-GameWipWingetPackageUpdate -Id $package.Id
        }
        elseif ($Update -and $present)
        {
            Write-Warning "$($package.Name) exists but is not registered with WinGet; skipping automatic replacement to avoid a duplicate installation."
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
