Set-StrictMode -Version Latest

function Test-WingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)

    & winget list --id $Id --exact --source winget --accept-source-agreements | Out-Null
    return $LASTEXITCODE -eq 0
}

function Install-WingetPackage
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
    $wasInstalled = Test-WingetPackage -Id $Id
    Invoke-SetupNative -FilePath 'winget' -ArgumentList $arguments | Out-Null
    if (-not $wasInstalled) { Add-GameWipOwnedWingetPackage -Id $Id }
    Update-SetupProcessPath
}

function Uninstall-WingetPackage
{
    param([Parameter(Mandatory = $true)][string]$Id)
    if (-not (Test-WingetPackage -Id $Id))
    {
        Write-Host "  Already absent: $Id"
        return
    }
    Invoke-SetupNative -FilePath 'winget' -ArgumentList @(
        'uninstall', '--id', $Id, '--exact', '--source', 'winget',
        '--accept-source-agreements', '--silent'
    ) | Out-Null
}

function Update-WingetPackage
{
    param(
        [Parameter(Mandatory = $true)][string]$Id,
        [string]$Override
    )

    if (-not (Test-WingetPackage -Id $Id))
    {
        Install-WingetPackage -Id $Id -Override $Override
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
    if ($LASTEXITCODE -ne 0)
    {
        Write-Warning "winget did not apply an update for $Id. The installed package will still be verified."
    }
    Update-SetupProcessPath
}

function Install-ConfiguredWingetTools
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
            $present = Test-SetupCommand -Name $package.Command
        }
        elseif ($package.ContainsKey('Path'))
        {
            $present = Test-Path -LiteralPath $package.Path
        }

        if ($Update -and (Test-WingetPackage -Id $package.Id))
        {
            Update-WingetPackage -Id $package.Id
        }
        elseif ($Update -and $present)
        {
            Write-Warning "$($package.Name) exists but is not registered with WinGet; skipping automatic replacement to avoid a duplicate installation."
        }
        elseif (-not $present)
        {
            Install-WingetPackage -Id $package.Id
        }
        else
        {
            Write-Host "  Ready: $($package.Name)"
        }
    }
}
