# ------------------------------------------------------------
# Candidate discovery
# ------------------------------------------------------------

function Add-GameWipToolCandidate
{
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[object]]$Candidates,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Seen,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Source,
        [Parameter(Mandatory = $true)][int]$Priority,
        [Parameter(Mandatory = $true)][string]$SelectionReason
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return
    }

    $resolved = [IO.Path]::GetFullPath($Path)

    if ((Test-GameWipWindowsHost) -and -not (Test-GameWipWindowsExecutablePath -Path $resolved))
    {
        return
    }

    if ($Seen.Add($resolved))
    {
        $Candidates.Add([pscustomobject]@{
                Path = $resolved
                Source = $Source
                Priority = $Priority
                SelectionReason = $SelectionReason
            }) | Out-Null
    }
}

function Get-GameWipToolCandidates
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if (-not (Test-GameWipToolHostSupport -Tool $Tool))
    {
        return @()
    }

    $candidates = [System.Collections.Generic.List[object]]::new()
    $comparer = Get-GameWipPathComparer
    $seen = [System.Collections.Generic.HashSet[string]]::new($comparer)
    $aliases = if ($Tool.detection.Contains('aliases'))
    {
        @($Tool.detection.aliases)
    }
    else
    {
        @()
    }
    $commands = @([string]$Tool.detection.command) + $aliases

    # Check the repository entry point for this host and Windows provider locations.
    $hostAssetKey = Get-GameWipHostAssetKey
    $hostPlatform = if ($null -ne $hostAssetKey)
    {
        [string]($hostAssetKey -split '-', 2)[0]
    }
    else
    {
        ''
    }

    if ($Tool.detection.Contains('repositoryPaths') -and $Tool.detection.repositoryPaths.Contains($hostPlatform))
    {
        Add-GameWipToolCandidate `
            -Candidates $candidates `
            -Seen $seen `
            -Path (Join-Path $RepositoryRoot ([string]$Tool.detection.repositoryPaths[$hostPlatform])) `
            -Source 'declared repository tool' `
            -Priority 10 `
            -SelectionReason 'repository-declared tool path'
    }

    if (Test-GameWipWindowsHost)
    {
        Add-GameWipWindowsToolCandidates `
            -Tool $Tool `
            -Commands $commands `
            -Candidates $candidates `
            -Seen $seen
    }

    # Search provider-managed and shared managed tool roots.
    foreach ($managedRoot in @(Get-GameWipProviderManagedToolPath -Tool $Tool))
    {
        foreach ($command in $commands)
        {
            foreach ($name in @(Get-GameWipExecutableNames -Command $command))
            {
                Add-GameWipToolCandidate `
                    -Candidates $candidates `
                    -Seen $seen `
                    -Path (Join-Path $managedRoot $name) `
                    -Source "GameWIPTools $($Tool.provider.kind)" `
                    -Priority 20 `
                    -SelectionReason 'declared provider-managed location'
            }
        }
    }

    foreach ($managedRoot in @(Get-GameWipManagedToolPath))
    {
        foreach ($command in $commands)
        {
            foreach ($name in @(Get-GameWipExecutableNames -Command $command))
            {
                Add-GameWipToolCandidate `
                    -Candidates $candidates `
                    -Seen $seen `
                    -Path (Join-Path $managedRoot $name) `
                    -Source 'GameWIPTools' `
                    -Priority 50 `
                    -SelectionReason 'other GameWIP-managed location'
            }
        }
    }

    # Use PATH as the final fallback.
    foreach ($command in $commands)
    {
        foreach ($resolved in @(Get-Command $command -All -ErrorAction SilentlyContinue))
        {
            if (-not [string]::IsNullOrWhiteSpace([string]$resolved.Source))
            {
                Add-GameWipToolCandidate `
                    -Candidates $candidates `
                    -Seen $seen `
                    -Path $resolved.Source `
                    -Source 'PATH' `
                    -Priority 100 `
                    -SelectionReason 'PATH fallback'
            }
        }
    }

    return @($candidates | Sort-Object Priority, Path)
}

function Get-GameWipPowerShellGalleryCandidates
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $items = [System.Collections.Generic.List[object]]::new()
    $providerRoots = @(Get-GameWipProviderManagedToolPath -Tool $Tool)

    # Prefer package copies owned by GameWIPTools.
    if ($providerRoots.Count -ne 0)
    {
        $managedRoot = Join-Path $providerRoots[0] ([string]$Tool.provider.package)

        foreach ($directory in @(Get-ChildItem -LiteralPath $managedRoot -Directory -ErrorAction SilentlyContinue))
        {
            if ($directory.Name.StartsWith('.', [StringComparison]::Ordinal))
            {
                continue
            }

            $manifest = Join-Path $directory.FullName "$($Tool.provider.package).psd1"

            if (-not (Test-Path -LiteralPath $manifest -PathType Leaf))
            {
                continue
            }

            $items.Add([pscustomobject]@{
                    Version = $directory.Name
                    Location = $directory.FullName
                    Source = 'GameWIPTools powershellGallery'
                    Priority = 20
                    SelectionReason = 'declared provider-managed location'
                }) | Out-Null
        }
    }

    # Fall back to modules available through PSModulePath.
    foreach ($module in @(Get-Module -ListAvailable -Name ([string]$Tool.provider.package)))
    {
        $items.Add([pscustomobject]@{
                Version = $module.Version.ToString()
                Location = $module.ModuleBase
                Source = 'PSModulePath'
                Priority = 100
                SelectionReason = 'PSModulePath fallback'
            }) | Out-Null
    }

    return @($items | Sort-Object Priority, @{ Expression = { try
                {
                    [version]$_.Version
                }

                catch
                {
                    [version]'0.0'
                } }; Descending = $true
        })
}
