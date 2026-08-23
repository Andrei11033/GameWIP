Set-StrictMode -Version Latest

function Invoke-GameWipEditorIntegrationRemoval
{
    $extensionRoot = Join-Path $env:USERPROFILE '.vscode\extensions'
    $extension = Join-Path $extensionRoot 'gamewip.gamewip-workflows'
    if (Test-Path -LiteralPath $extension)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $extension -OwnedRoot $extensionRoot
    }
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

function Write-GameWipUninstallSection
{
    param([string]$Title, [string[]]$Items)
    Write-Host ''
    Write-Host "${Title}:"
    if (@($Items).Count -eq 0)
    {
        Write-Host '  (none)'; return
    }
    foreach ($item in $Items)
    {
        Write-Host "  - $item"
    }
}

function Invoke-GameWipUninstall
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [Parameter(Mandatory = $true)][hashtable]$SetupConfig,
        [Parameter(Mandatory = $true)][hashtable]$ProjectTools,
        [switch]$Preview
    )
    Write-GameWipSetupSection "Uninstall GameWIP development environment$(if ($Preview) { ' (preview)' })"
    $stateExists = Test-Path -LiteralPath $script:SetupStatePath -PathType Leaf
    $state = Get-GameWipSetupState
    if (-not $stateExists)
    {
        Write-Host 'Disposable setup state is missing; discovery is using attached resource evidence and conservative inference.' -ForegroundColor Yellow
    }

    $managedMarker = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) '.gamewip-managed.json'
    $markerProvesOwnership = $false
    if (Test-Path -LiteralPath $managedMarker -PathType Leaf)
    {
        try
        {
            $marker = Get-Content -Raw -LiteralPath $managedMarker | ConvertFrom-Json
            $markerProvesOwnership = $marker.schemaVersion -eq 1 -and $marker.owner -eq 'GameWIP' -and $marker.resource -eq 'msys2' -and $marker.installedBySetup -eq $true
        }
        catch
        {
            $markerProvesOwnership = $false
        }
    }

    $proven = @('GameWIP VS Code workflow integration and managed keybinding block')
    if (Test-Path -LiteralPath ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot))
    {
        $proven += 'C:\MSYS2\GameWIPTools persistent managed tool tree'
    }
    if ($markerProvesOwnership)
    {
        $proven += 'C:\MSYS2 installation (persistent ownership marker)'
    }
    $recorded = @($state.wingetPackages | ForEach-Object { "WinGet package $_" }) + @($state.vscodeExtensions | ForEach-Object { "VS Code extension $_" })
    if ($state.msys2InstalledBySetup -and -not $markerProvesOwnership)
    {
        $recorded += 'C:\MSYS2 installation (disposable setup state)'
    }
    $stronglyInferred = @()
    $unknown = @()
    if ((Test-Path -LiteralPath ([string]$ProjectConfig.managedEnvironment.msys2Root)) -and -not $markerProvesOwnership -and -not $state.msys2InstalledBySetup)
    {
        $stronglyInferred += 'C:\MSYS2 resembles the GameWIP setup root but has no surviving ownership proof'
    }
    if ((Test-Path -LiteralPath $managedMarker) -and -not $markerProvesOwnership)
    {
        $unknown += 'Invalid or unrecognized C:\MSYS2 ownership marker'
    }

    $canInstall = @($SetupConfig.wingetPackages | ForEach-Object { "WinGet: $($_.id)" })
    $canInstall += @($ProjectTools.tools | ForEach-Object { "$($_.id) via $($_.provider.kind)" })
    $planned = @('GameWIP VS Code workflow integration and keybindings') + $recorded
    $planned += @('C:\MSYS2\GameWIPTools', 'build/gamewip/cache/tracy', 'disposable setup/editor state')
    $preserved = @('Repository and user-created files', 'Pre-existing or ownership-unknown applications', 'C:\MSYS2 (may contain user-added packages/files)')
    $manual = @('Review C:\MSYS2 and remove it manually only if its remaining contents are no longer needed.')

    Write-GameWipUninstallSection -Title 'Proven GameWIP-owned' -Items $proven
    Write-GameWipUninstallSection -Title 'Recorded GameWIP-owned' -Items $recorded
    Write-GameWipUninstallSection -Title 'Strongly inferred' -Items $stronglyInferred
    Write-GameWipUninstallSection -Title 'Ownership unknown' -Items $unknown
    Write-GameWipUninstallSection -Title 'Resources this GameWIP setup can install' -Items $canInstall
    Write-GameWipUninstallSection -Title 'Planned automatic removals' -Items $planned
    Write-GameWipUninstallSection -Title 'Preserved resources' -Items $preserved
    Write-GameWipUninstallSection -Title 'Manual follow-up' -Items $manual
    if ($Preview)
    {
        Write-Host ''; Write-Host 'Preview complete; no resources were changed.'; return
    }

    Invoke-GameWipEditorIntegrationRemoval
    if (Test-GameWipSetupCommand -Name 'code')
    {
        foreach ($extensionId in @($state.vscodeExtensions))
        {
            Invoke-GameWipSetupNative -FilePath 'code' -ArgumentList @('--uninstall-extension', $extensionId) -AllowedExitCodes @(0, 1) | Out-Null
        }
    }
    $managedToolsRoot = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    if (Test-Path -LiteralPath $managedToolsRoot)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $managedToolsRoot -OwnedRoot ([string]$ProjectConfig.managedEnvironment.msys2Root)
    }
    $tracyCache = Join-Path $RepositoryRoot (Join-Path $ProjectConfig.storage.cache 'tracy')
    if (Test-Path -LiteralPath $tracyCache)
    {
        Invoke-GameWipOwnedTreeRemoval -Path $tracyCache -OwnedRoot (Join-Path $RepositoryRoot $ProjectConfig.storage.cache)
    }
    if (@($state.wingetPackages).Count -eq 0)
    {
        Write-Host '  No setup-owned WinGet packages were recorded; pre-existing applications are unchanged.'
    }
    foreach ($id in @($state.wingetPackages))
    {
        Uninstall-GameWipWingetPackage -Id $id
    }
    if (($markerProvesOwnership -or $state.msys2InstalledBySetup) -and (Test-Path -LiteralPath 'C:\MSYS2'))
    {
        Write-Warning 'MSYS2 was installed by GameWIP, but its directory may now contain user data. Remove C:\MSYS2 manually after reviewing it.'
    }
    Remove-Item -LiteralPath $script:SetupStatePath -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot) -Force -ErrorAction SilentlyContinue
    Write-Host '  Uninstall complete. Pre-existing software and the repository were preserved.'
}
