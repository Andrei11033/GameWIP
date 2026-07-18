Set-StrictMode -Version Latest

function Remove-GameWipEditorIntegration
{
    $extension = Join-Path $env:USERPROFILE '.vscode\extensions\gamewip.gamewip-workflows'
    if (Test-Path -LiteralPath $extension) { Remove-Item -LiteralPath $extension -Recurse -Force }

    $keybindings = Join-Path $env:APPDATA 'Code\User\keybindings.json'
    if (Test-Path -LiteralPath $keybindings)
    {
        $content = Get-Content -LiteralPath $keybindings -Raw
        $pattern = '(?ms)\s*,?\s*// GameWIP managed keybindings begin.*?// GameWIP managed keybindings end\s*'
        $updated = [regex]::Replace($content, $pattern, '')
        [IO.File]::WriteAllText($keybindings, $updated, [Text.UTF8Encoding]::new($false))
    }
    Write-Host '  Removed repository-owned VS Code integration.'
}

function Invoke-GameWipUninstall
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)
    Write-SetupSection 'Uninstall GameWIP development environment'
    $state = Get-GameWipSetupState

    Remove-GameWipEditorIntegration
    if (Test-SetupCommand -Name 'code')
    {
        foreach ($extensionId in @($state.vscodeExtensions))
        {
            Invoke-SetupNative -FilePath 'code' -ArgumentList @('--uninstall-extension', $extensionId) -AllowedExitCodes @(0, 1) | Out-Null
        }
    }
    foreach ($path in @('.tracy', 'build\setup', 'build\dev', 'build\docs'))
    {
        $target = Join-Path $RepositoryRoot $path
        if (Test-Path -LiteralPath $target) { Remove-Item -LiteralPath $target -Recurse -Force }
    }
    if (@($state.wingetPackages).Count -eq 0)
    {
        Write-Host '  No setup-owned WinGet packages were recorded; pre-existing applications are unchanged.'
    }
    foreach ($id in @($state.wingetPackages)) { Uninstall-WingetPackage -Id $id }

    if ($state.msys2InstalledBySetup -and (Test-Path -LiteralPath 'C:\MSYS2'))
    {
        Write-Warning 'MSYS2 was installed by GameWIP, but its directory may now contain user data. Remove C:\MSYS2 manually after reviewing it.'
    }
    Remove-Item -LiteralPath $script:SetupStatePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $RepositoryRoot '.gamewip-setup.json') -Force -ErrorAction SilentlyContinue
    Write-Host '  Uninstall complete. Pre-existing software and the repository were preserved.'
}
