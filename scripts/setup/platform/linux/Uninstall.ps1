# Linux setup ownership inventory and conservative removal.

# ------------------------------------------------------------
# Managed tool marker validation
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Get-GameWipLinuxPathAttributes
{
    param([Parameter(Mandatory = $true)][string]$Path)

    try
    {
        return [IO.File]::GetAttributes($Path)
    }

    catch
    {
        return $null
    }
}

function Test-GameWipLinuxOwnedDirectory
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $attributes = Get-GameWipLinuxPathAttributes -Path $Path

    if ($null -eq $attributes)
    {
        return $false
    }

    $isDirectory = ($attributes -band [IO.FileAttributes]::Directory) -ne 0
    $isPlainDirectory = ($attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0
    return $isDirectory -and $isPlainDirectory
}

# Remove links as links so managed environments such as Python venvs can be removed safely.
function Invoke-GameWipLinuxOwnedTreeRemoval
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$OwnedRoot
    )

    if ($null -eq (Get-GameWipLinuxPathAttributes -Path $Path))
    {
        return
    }

    $canonicalPath = Assert-GameWipSafeChildPath -Path $Path -OwnedRoot $OwnedRoot
    $rootAttributes = Get-GameWipLinuxPathAttributes -Path $canonicalPath

    if ($null -eq $rootAttributes -or
        ($rootAttributes -band [IO.FileAttributes]::Directory) -eq 0 -or
        ($rootAttributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        throw "Refusing to remove unmanaged Linux tool path '$canonicalPath'."
    }

    # Refuse linked ancestors so traversal cannot leave the managed root.
    $canonicalRoot = Get-GameWipCanonicalPath -Path $OwnedRoot
    $relativePath = [IO.Path]::GetRelativePath($canonicalRoot, $canonicalPath)
    $segments = @($relativePath -split [regex]::Escape([IO.Path]::DirectorySeparatorChar) | Where-Object { $_ })
    $currentPath = $canonicalRoot

    for ($index = 0; $index -lt $segments.Count - 1; ++$index)
    {
        $currentPath = Join-Path $currentPath $segments[$index]
        $attributes = Get-GameWipLinuxPathAttributes -Path $currentPath

        if ($null -ne $attributes -and ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
        {
            throw "Refusing to remove Linux tool data through linked directory '$currentPath'."
        }
    }

    Set-GameWipMutationStarted

    Remove-GameWipOwnedTreeEntries -Path $canonicalPath
}

function Test-GameWipLinuxJsonToolMarker
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$MarkerName,
        [Parameter(Mandatory = $true)][string]$ToolId,
        [string]$Version,
        [hashtable]$Tool
    )

    $markerPath = Join-Path $Path $MarkerName

    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf))
    {
        return $false
    }

    try
    {
        $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json

        if ($marker.schemaVersion -ne 1 -or $marker.toolId -ne $ToolId)
        {
            return $false
        }

        if (-not [string]::IsNullOrWhiteSpace($Version) -and $marker.version -ne $Version)
        {
            return $false
        }

        if ($marker.sha256 -notmatch '^[a-fA-F0-9]{64}$')
        {
            return $false
        }

        if ($null -ne $Tool)
        {
            $declaredAssets = @($Tool.provider.assets.Values | Where-Object {
                    [string]$_.archive -eq [string]$marker.archive -and
                    [string]$_.sha256 -eq [string]$marker.sha256
                })

            if ($declaredAssets.Count -eq 0)
            {
                return $false
            }
        }

        return -not [string]::IsNullOrWhiteSpace([string]$marker.archive)
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxPowerShellMarker
{
    param([Parameter(Mandatory = $true)][string]$Path)
    $markerPath = Join-Path $Path '.gamewip-tool'

    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf))
    {
        return $false
    }

    try
    {
        $metadata = @{}

        foreach ($line in Get-Content -LiteralPath $markerPath)
        {
            if ([string]$line -match '^([^=]+)=(.*)$')
            {
                $metadata[$Matches[1]] = $Matches[2]
            }
        }
        $version = Split-Path -Leaf $Path

        $archivePattern = '^powershell-' + [regex]::Escape($version) + '-linux-(x64|arm64)\.tar\.gz$'

        if ($metadata.version -ne $version -or $metadata.archive -notmatch $archivePattern)
        {
            return $false
        }

        if ($metadata.sha256 -notmatch '^[a-fA-F0-9]{64}$')
        {
            return $false
        }

        return Test-Path -LiteralPath (Join-Path $Path 'pwsh') -PathType Leaf
    }

    catch
    {
        return $false
    }
}

function Test-GameWipLinuxCurrentPowerShell
{
    param([Parameter(Mandatory = $true)][string]$Path)

    try
    {
        $powerShellHome = [IO.Path]::GetFullPath($PSHOME)
        return Test-GameWipPathWithinRoot -Path $powerShellHome -Root $Path
    }

    catch
    {
        return $true
    }
}

function Write-GameWipLinuxPowerShellCleanupRequest
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$LaunchToken
    )

    if ($LaunchToken -notmatch '^[0-9]+-[0-9]+-[0-9]+$')
    {
        throw 'The setup launcher did not provide a valid PowerShell cleanup token.'
    }

    $markerPath = Join-Path $Path '.gamewip-uninstall-pending'
    $stream = [IO.File]::Open($markerPath, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)

    try
    {
        $bytes = [Text.Encoding]::ASCII.GetBytes($LaunchToken)
        $stream.Write($bytes, 0, $bytes.Length)
    }

    finally
    {
        $stream.Dispose()
    }
}

# ------------------------------------------------------------
# Linux tool removal inventory
# ------------------------------------------------------------

function Get-GameWipLinuxUninstallInventory
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    $toolRoot = Get-GameWipManagedToolRoot
    $ownership = Get-GameWipManagedToolRootOwnership -Root $toolRoot
    $payload = if ($null -ne $ownership)
    {
        $ownership.PSObject.Properties['payload']
    }
    else
    {
        $null
    }
    $toolRootOwned = $null -ne $payload -and $null -ne $payload.Value

    if ($toolRootOwned)
    {
        $toolRootOwned = $payload.Value.installedBySetup -eq $true -and $payload.Value.adoptedByUser -ne $true
    }
    $proven = [System.Collections.Generic.List[string]]::new()
    $unknown = [System.Collections.Generic.List[string]]::new()
    $planned = [System.Collections.Generic.List[string]]::new()
    $preserved = [System.Collections.Generic.List[string]]::new()
    $removePaths = [System.Collections.Generic.List[string]]::new()
    $deferredPowerShellPaths = [System.Collections.Generic.List[string]]::new()

    foreach ($name in @('bin', 'npm', 'python', 'powershell'))
    {
        $path = Join-Path $toolRoot $name

        if ($null -eq (Get-GameWipLinuxPathAttributes -Path $path))
        {
            continue
        }

        if ($toolRootOwned -and (Test-GameWipLinuxOwnedDirectory -Path $path))
        {
            $proven.Add("$path (project-tools ownership marker)") | Out-Null
            $planned.Add($path) | Out-Null
            $removePaths.Add($path) | Out-Null
        }
        else
        {
            $reason = if (-not (Test-GameWipLinuxOwnedDirectory -Path $path))
            {
                'not a plain managed directory'
            }
            else
            {
                'project-tools ownership is missing or adopted'
            }
            $unknown.Add("$path ($reason)") | Out-Null
            $preserved.Add($path) | Out-Null
        }
    }

    $cmakePath = Join-Path $toolRoot 'cmake'

    if ($null -ne (Get-GameWipLinuxPathAttributes -Path $cmakePath))
    {
        if ((Test-GameWipLinuxOwnedDirectory -Path $cmakePath) -and
            (Test-GameWipLinuxJsonToolMarker -Path $cmakePath -MarkerName '.gamewip-tool.json' -ToolId cmake))
        {
            $proven.Add("$cmakePath (CMake installation marker)") | Out-Null
            $planned.Add($cmakePath) | Out-Null
            $removePaths.Add($cmakePath) | Out-Null
        }
        else
        {
            $unknown.Add("$cmakePath (CMake ownership marker is missing or invalid)") | Out-Null
            $preserved.Add($cmakePath) | Out-Null
        }
    }

    $toolsRoot = Join-Path $toolRoot 'tools'

    # Recognize only declared directory-archive tool families.
    $directoryArchiveTools = @($ProjectTools.tools | Where-Object {
            $_.provider.kind -in @('githubRelease', 'archive') -and
            $_.provider.Contains('installMode') -and
            $_.provider.installMode -eq 'directoryArchive' -and
            $_.provider.Contains('managedDirectory') -and
            [string]$_.provider.managedDirectory -match '^tools/[^/]+$'
        })

    if ($null -ne (Get-GameWipLinuxPathAttributes -Path $toolsRoot))
    {
        if (-not (Test-GameWipLinuxOwnedDirectory -Path $toolsRoot))
        {
            $unknown.Add("$toolsRoot (not a plain managed directory)") | Out-Null
            $preserved.Add($toolsRoot) | Out-Null
        }
        else
        {
            foreach ($family in @(Get-ChildItem -LiteralPath $toolsRoot -Force))
            {
                $familyTools = @($directoryArchiveTools | Where-Object {
                        (Split-Path -Leaf ([string]$_.provider.managedDirectory)) -eq $family.Name
                    })

                if ($familyTools.Count -eq 1 -and (Test-GameWipLinuxOwnedDirectory -Path $family.FullName))
                {
                    $tool = $familyTools[0]

                    foreach ($version in @(Get-ChildItem -LiteralPath $family.FullName -Directory -Force))
                    {
                        if ((Test-GameWipLinuxOwnedDirectory -Path $version.FullName) -and
                            (Test-GameWipLinuxJsonToolMarker `
                                -Path $version.FullName `
                                -MarkerName '.gamewip-tool.json' `
                                -ToolId ([string]$tool.id) `
                                -Version $version.Name `
                                -Tool $tool))
                        {
                            $proven.Add("$($version.FullName) ($($tool.name) installation marker)") | Out-Null
                            $planned.Add($version.FullName) | Out-Null
                            $removePaths.Add($version.FullName) | Out-Null
                        }
                        else
                        {
                            $unknown.Add("$($version.FullName) ($($tool.name) ownership marker is missing or invalid)") | Out-Null
                            $preserved.Add($version.FullName) | Out-Null
                        }
                    }
                }
                elseif ($family.Name -eq 'powershell' -and (Test-GameWipLinuxOwnedDirectory -Path $family.FullName))
                {
                    foreach ($version in @(Get-ChildItem -LiteralPath $family.FullName -Directory -Force))
                    {
                        if (-not (Test-GameWipLinuxOwnedDirectory -Path $version.FullName) -or
                            -not (Test-GameWipLinuxPowerShellMarker -Path $version.FullName))
                        {
                            $unknown.Add("$($version.FullName) (PowerShell ownership marker is missing or invalid)") | Out-Null
                            $preserved.Add($version.FullName) | Out-Null
                            continue
                        }

                        $proven.Add("$($version.FullName) (PowerShell installation marker)") | Out-Null

                        if (Test-GameWipLinuxCurrentPowerShell -Path $version.FullName)
                        {
                            if ([string]$env:GAMEWIP_SETUP_LAUNCH_TOKEN -match '^[0-9]+-[0-9]+-[0-9]+$')
                            {
                                $planned.Add("$($version.FullName) (removed after setup exits)") | Out-Null
                                $deferredPowerShellPaths.Add($version.FullName) | Out-Null
                            }
                            else
                            {
                                $preserved.Add("$($version.FullName) (currently running helper; use system PowerShell to remove it)") | Out-Null
                            }
                            continue
                        }

                        $planned.Add($version.FullName) | Out-Null
                        $removePaths.Add($version.FullName) | Out-Null
                    }
                }
                else
                {
                    $unknown.Add("$($family.FullName) (not a recognized GameWIP tool directory)") | Out-Null
                    $preserved.Add($family.FullName) | Out-Null
                }
            }

            if (@(Get-ChildItem -LiteralPath $toolsRoot -Force).Count -eq 0)
            {
                $planned.Add($toolsRoot) | Out-Null
                $removePaths.Add($toolsRoot) | Out-Null
            }
        }
    }

    $statePath = Get-GameWipEditorPreferencePath -RepositoryRoot $RepositoryRoot

    return [pscustomobject]@{
        ManagedToolRoot = $toolRoot
        ManagedToolRootOwned = $toolRootOwned
        Proven = $proven.ToArray()
        Unknown = $unknown.ToArray()
        Planned = $planned.ToArray()
        Preserved = $preserved.ToArray()
        RemovePaths = $removePaths.ToArray()
        DeferredPowerShellPaths = $deferredPowerShellPaths.ToArray()
        EditorPreferencePath = $statePath
    }
}

# ------------------------------------------------------------
# Linux uninstall
# ------------------------------------------------------------

function Invoke-GameWipUninstall
{
    param(
        [Parameter(Mandatory = $true)][string]$RepositoryRoot,
        [switch]$Preview
    )

    Write-GameWipSection "Uninstall GameWIP development environment$(if ($Preview) { ' (preview)' })"
    $state = Get-GameWipSetupState
    $inventory = Get-GameWipLinuxUninstallInventory -RepositoryRoot $RepositoryRoot
    $distribution = Get-GameWipLinuxDistribution
    $manager = [string]$distribution.PackageManager
    $recordedSystemPackages = @(
        $state.systemPackages |
            Where-Object { $_.manager -eq $manager } |
            ForEach-Object { [string]$_.package } |
            Sort-Object -Unique
    )
    $installedSystemPackages = if ($recordedSystemPackages.Count -ne 0)
    {
        @(Get-GameWipLinuxInstalledPackages -Manager $manager -Packages $recordedSystemPackages)
    }
    else
    {
        @()
    }
    $installedSystemPackages = @($installedSystemPackages)
    $recorded = [System.Collections.Generic.List[string]]::new()
    $planned = [System.Collections.Generic.List[string]]::new()
    $preserved = [System.Collections.Generic.List[string]]::new()

    $proven = [System.Collections.Generic.List[string]]::new()
    $proven.Add('GameWIP VS Code workflow extension and managed keybinding block') | Out-Null
    $planned.Add('GameWIP VS Code workflow extension and managed keybinding block') | Out-Null
    $planned.AddRange([string[]]$inventory.Planned)
    $preserved.AddRange([string[]]$inventory.Preserved)

    foreach ($extension in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
    {
        $recorded.Add("VS Code extension $extension") | Out-Null
        $planned.Add("VS Code extension $extension") | Out-Null
    }

    foreach ($package in $installedSystemPackages)
    {
        $recorded.Add("$manager package $package (installed by GameWIP)") | Out-Null
        $planned.Add("$manager package $package") | Out-Null
    }

    foreach ($entry in @($state.systemPackages | Where-Object { $_.manager -ne $manager }))
    {
        $preserved.Add("$($entry.manager) package $($entry.package) (not managed by the current package manager)") | Out-Null
    }

    $preserved.Add('Pre-existing or unrecorded system packages and unrelated software') | Out-Null
    $preserved.Add('Dependency caches, run receipts, build-target selection, build trees, and checkout files') | Out-Null
    $preserved.Add('Original VS Code keybindings backup') | Out-Null

    Write-GameWipUninstallSection -Title 'Proven GameWIP-owned' -Items (@($proven.ToArray()) + @($inventory.Proven))
    Write-GameWipUninstallSection -Title 'Recorded GameWIP-owned' -Items $recorded.ToArray()
    Write-GameWipUninstallSection -Title 'Ownership unknown' -Items $inventory.Unknown
    Write-GameWipUninstallSection -Title 'Planned automatic removals' -Items $planned.ToArray()
    Write-GameWipUninstallSection -Title 'Preserved resources' -Items $preserved.ToArray()

    if ($Preview -or ($null -ne $Script:OperationContext -and $Script:OperationContext.Preview))
    {
        Write-Host ''
        Write-Host 'Preview complete; no resources were changed.'
        return
    }

    if (-not (Confirm-GameWipMutation -Summary 'Remove only inventoried GameWIP-owned Linux tools, packages, and editor integration.' -Risk destructive -Plan $planned.ToArray()))
    {
        Add-GameWipOperationPreserved -Message 'Uninstall was cancelled; no resources were removed.'
        return
    }

    Invoke-GameWipMutationBody -Body {
        Invoke-GameWipEditorIntegrationRemoval

        if (Test-GameWipSetupCommand -Name code)
        {
            foreach ($extension in @($state.vscodeExtensions | Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) }))
            {
                Invoke-GameWipSetupNative -FilePath code -ArgumentList @('--uninstall-extension', $extension) -AllowedExitCodes @(0, 1) | Out-Null
            }
        }

        foreach ($path in $inventory.RemovePaths)
        {
            Invoke-GameWipLinuxOwnedTreeRemoval -Path $path -OwnedRoot $inventory.ManagedToolRoot
            Add-GameWipOperationChange -Message "Removed managed Linux tool data: $path"
        }

        foreach ($path in $inventory.DeferredPowerShellPaths)
        {
            Write-GameWipLinuxPowerShellCleanupRequest `
                -Path $path `
                -LaunchToken ([string]$env:GAMEWIP_SETUP_LAUNCH_TOKEN)
            Add-GameWipOperationChange -Message 'Scheduled the active PowerShell runtime for removal after setup exits.'
        }

        if ($installedSystemPackages.Count -ne 0)
        {
            Remove-GameWipLinuxSystemPackages -Manager $manager -Packages $installedSystemPackages
            Add-GameWipOperationChange -Message "Removed GameWIP-added $manager packages: $($installedSystemPackages -join ', ')"
        }

        foreach ($path in @($script:SetupStatePath, $inventory.EditorPreferencePath))
        {
            if (Test-Path -LiteralPath $path -PathType Leaf)
            {
                Set-GameWipMutationStarted
                Remove-Item -LiteralPath $path -Force
            }
        }

        Add-GameWipOperationChange -Message 'Removed setup-owned Linux tools, recorded packages, editor integration, and disposable setup state.'
        Add-GameWipOperationPreserved -Message 'Unrecorded system packages, unrelated software, caches, run receipts, build trees, and checkout files were preserved.'
    }
}
