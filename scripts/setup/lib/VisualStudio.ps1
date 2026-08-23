Set-StrictMode -Version Latest

function Get-GameWipVisualStudioInstance
{
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path -LiteralPath $vswhere))
    {
        return $null
    }

    $path = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Workload.NativeDesktop -property installationPath
    if ($LASTEXITCODE -ne 0 -or -not $path)
    {
        return $null
    }
    return ($path | Select-Object -First 1)
}

function Install-GameWipVisualStudio
{
    param(
        [Parameter(Mandatory = $true)][string]$PackageId,
        [Parameter(Mandatory = $true)][string]$VsConfigPath,
        [switch]$Update
    )

    $override = "--passive --wait --norestart --config `"$VsConfigPath`" --includeRecommended"
    $instance = Get-GameWipVisualStudioInstance
    if (-not $instance)
    {
        Install-GameWipWingetPackage -Id $PackageId -Override $override
        $instance = Get-GameWipVisualStudioInstance
    }

    if (-not $instance)
    {
        throw 'Visual Studio with the Desktop development with C++ workload was not detected after installation.'
    }

    $installer = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\setup.exe'
    if (Test-Path -LiteralPath $installer)
    {
        if ($Update)
        {
            Invoke-GameWipSetupNative -FilePath $installer -ArgumentList @(
                'update', '--installPath', $instance, '--passive', '--norestart'
            ) -AllowedExitCodes @(0, 3010) | Out-Null
        }
        Invoke-GameWipSetupNative -FilePath $installer -ArgumentList @(
            'modify', '--installPath', $instance, '--config', $VsConfigPath,
            '--includeRecommended', '--passive', '--norestart'
        ) -AllowedExitCodes @(0, 3010) | Out-Null
    }

    Write-Host "  Ready: Visual Studio at $instance"
}
