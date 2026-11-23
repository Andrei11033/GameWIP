# Shared uninstall inventory and editor cleanup.

# ------------------------------------------------------------
# Uninstall presentation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Write-GameWipUninstallSection
{
    param([Parameter(Mandatory = $true)][string]$Title, [string[]]$Items = @())
    Write-Host ''
    Write-Host "${Title}:"

    if ($Items.Count -eq 0)
    {
        Write-Host '  (none)'
        return
    }

    foreach ($item in $Items)
    {
        Write-Host "  - $item"
    }
}

# ------------------------------------------------------------
# Shared VS Code integration cleanup
# ------------------------------------------------------------

function Invoke-GameWipEditorIntegrationRemoval
{
    $extensionSource = Join-Path $RepositoryRoot 'scripts/setup/editor/gamewip-workflows'
    $package = Get-Content -Raw -LiteralPath (Join-Path $extensionSource 'package.json') | ConvertFrom-Json
    $extensionId = "$($package.publisher).$($package.name)"

    if (Test-GameWipSetupCommand -Name code)
    {
        Invoke-GameWipSetupNative -FilePath code -ArgumentList @('--uninstall-extension', $extensionId) -AllowedExitCodes @(0, 1) | Out-Null
    }
    else
    {
        Add-GameWipOperationWarning -Message "VS Code is unavailable; could not remove '$extensionId'."
    }

    $keybindings = Join-Path (Get-GameWipVsCodeUserDirectory) 'keybindings.json'
    $legacyBackup = "$keybindings.gamewip-backup"
    $managedBackup = Join-Path (Get-GameWipStorageAreaPath -Area state) 'vscode-keybindings.json'
    $hasOriginalKeybindings = (Test-Path -LiteralPath $managedBackup -PathType Leaf) -or
        (Test-Path -LiteralPath $legacyBackup -PathType Leaf)

    if (Test-Path -LiteralPath $keybindings -PathType Leaf)
    {
        $text = Get-Content -Raw -LiteralPath $keybindings
        $updated = [regex]::Replace($text, '(?ms)\s*,?\s*// GameWIP managed keybindings begin.*?// GameWIP managed keybindings end\s*', '')

        if ($updated -ne $text)
        {
            Write-GameWipTextAtomic -Path $keybindings -Content $updated
        }

        if ($updated -match '^\s*\[\s*\]\s*$' -and -not $hasOriginalKeybindings)
        {
            Remove-Item -LiteralPath $keybindings -Force
        }
    }

    $legacyBackupAttributes = Get-GameWipFileSystemAttributes -Path $legacyBackup

    if ($null -ne $legacyBackupAttributes -and
        ($legacyBackupAttributes -band [IO.FileAttributes]::Directory) -eq 0 -and
        ($legacyBackupAttributes -band [IO.FileAttributes]::ReparsePoint) -eq 0)
    {
        Remove-Item -LiteralPath $legacyBackup -Force
    }
    elseif ($null -ne $legacyBackupAttributes)
    {
        Add-GameWipOperationWarning -Message "Preserved linked or non-file VS Code backup path '$legacyBackup'."
    }
}
