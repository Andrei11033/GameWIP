# Visual Studio discovery, installation, update, and workload verification.

# ------------------------------------------------------------
# Visual Studio discovery and installation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipVisualStudioPath
{
    param([switch]$RequireDesktopWorkload)
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'

    if (-not (Test-Path -LiteralPath $vswhere))
    {
        return $null
    }

    $arguments = @('-latest', '-products', '*')

    if ($RequireDesktopWorkload)
    {
        $arguments += @('-requires', 'Microsoft.VisualStudio.Workload.NativeDesktop')
    }
    $arguments += '-property'
    $arguments += 'installationPath'
    $result = Invoke-GameWipProcess -FilePath $vswhere -Arguments $arguments -OutputMode LogOnly -TimeoutSeconds 30

    if ($result.ExitCode -ne 0 -or $result.Stdout.Count -eq 0)
    {
        return $null
    }

    return [string]($result.Stdout | Select-Object -First 1)
}

function Get-GameWipVisualStudioInstallation
{
    return Get-GameWipVisualStudioPath
}

function Get-GameWipVisualStudioInstance
{
    return Get-GameWipVisualStudioPath -RequireDesktopWorkload
}

function Install-GameWipVisualStudio
{
    param([Parameter(Mandatory = $true)][string]$PackageId, [Parameter(Mandatory = $true)][string]$VsConfigPath, [switch]$Update)
    $override = "--passive --wait --norestart --config `"$VsConfigPath`" --includeRecommended"
    $installation = Get-GameWipVisualStudioInstallation

    if (-not $installation)
    {
        if (-not (Confirm-GameWipExternalInstall -Name 'Visual Studio Community' -Provider WinGet -Package $PackageId))
        {
            Add-GameWipOperationPreserved -Message 'Left Visual Studio uninstalled after the WinGet prompt was declined.'
            Write-GameWipSemanticText -Object 'Skipped Visual Studio; it remains uninstalled.' -Semantic Warning
            return $false
        }
        Install-GameWipWingetPackage -Id $PackageId -Override $override
        $installation = Get-GameWipVisualStudioInstallation
    }

    if (-not $installation)
    {
        throw 'A Visual Studio installation was not detected after setup.'
    }

    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'

    if (-not (Test-Path -LiteralPath $installer -PathType Leaf))
    {
        throw "The Visual Studio Installer was not found at '$installer'."
    }

    if ($Update)
    {
        Invoke-GameWipSetupNative -FilePath $installer -ArgumentList @('update', '--installPath', $installation, '--passive', '--norestart') -AllowedExitCodes @(0, 3010) | Out-Null
    }
    Invoke-GameWipSetupNative -FilePath $installer -ArgumentList @('modify', '--installPath', $installation, '--config', $VsConfigPath, '--includeRecommended', '--passive', '--norestart') -AllowedExitCodes @(0, 3010) | Out-Null
    $instance = Get-GameWipVisualStudioInstance

    if (-not $instance)
    {
        throw 'Visual Studio was modified, but its Desktop development with C++ workload was not detected.'
    }

    Write-Host "  Ready: Visual Studio at $instance"
    return $true
}
