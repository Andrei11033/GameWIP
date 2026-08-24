# Shared GameWIP project-tool registry, detection, provider, ownership, and update behavior.

foreach ($providerFile in @('Msys2.ps1', 'Npm.ps1', 'Python.ps1', 'PowerShellGallery.ps1', 'GitHubRelease.ps1', 'Winget.ps1', 'GitSubmodule.ps1', 'External.ps1'))
{
    . (Join-Path $PSScriptRoot (Join-Path 'Providers' $providerFile))
}

function Test-GameWipWindowsHost
{
    return [Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows)
}

function Get-GameWipProjectTool
{
    param([Parameter(Mandatory = $true)][string]$Id)

    $matches = @($ProjectTools.tools | Where-Object { $_.id -eq $Id })
    if ($matches.Count -ne 1)
    {
        throw "Project tool '$Id' is not registered exactly once."
    }
    return $matches[0]
}

function Get-GameWipManagedToolPath
{
    if (-not (Test-GameWipWindowsHost))
    {
        return @()
    }

    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    if ((Test-Path -LiteralPath $root) -and -not (Test-GameWipManagedToolRootOwnership -Root $root))
    {
        # Unknown persistent content must not be preferred as a managed tool
        # source before setup has established or explicitly adopted ownership.
        return @()
    }
    return @(
        (Join-Path $root 'bin'),
        (Join-Path $root 'npm'),
        (Join-Path $root 'npm\bin'),
        (Join-Path $root 'python\bin'),
        (Join-Path $root 'python\Scripts'),
        (Join-Path $root 'powershell')
    )
}

function Get-GameWipProviderManagedToolPath
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if (-not (Test-GameWipWindowsHost))
    {
        return @()
    }

    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    if ((Test-Path -LiteralPath $root) -and -not (Test-GameWipManagedToolRootOwnership -Root $root))
    {
        return @()
    }

    switch ([string]$Tool.provider.kind)
    {
        'python'
        {
            return @(
                (Join-Path $root 'python\bin'),
                (Join-Path $root 'python\Scripts')
            )
        }
        'npm'
        {
            return @(
                (Join-Path $root 'npm'),
                (Join-Path $root 'npm\bin')
            )
        }
        'githubRelease' { return @((Join-Path $root 'bin')) }
        default { return @() }
    }
}

function Get-GameWipManagedToolRootOwnership
{
    param([Parameter(Mandatory = $true)][string]$Root)

    $markerPath = Join-Path $Root '.gamewip-managed.json'
    if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf))
    {
        return $null
    }
    try
    {
        $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json
        $installedBySetup = $marker.PSObject.Properties['installedBySetup'] -and [bool]$marker.installedBySetup
        $adoptedByUser = $marker.PSObject.Properties['adoptedByUser'] -and [bool]$marker.adoptedByUser
        if ($marker.schemaVersion -ne 1 -or
            $marker.owner -ne 'GameWIP' -or
            $marker.resource -ne 'project-tools' -or
            (-not $installedBySetup -and -not $adoptedByUser))
        {
            return $null
        }
        return $marker
    }
    catch
    {
        return $null
    }
}

function Test-GameWipManagedToolRootOwnership
{
    param([Parameter(Mandatory = $true)][string]$Root)
    return $null -ne (Get-GameWipManagedToolRootOwnership -Root $Root)
}

function Initialize-GameWipManagedToolRoot
{
    param([switch]$AdoptExisting)

    if (-not (Test-GameWipWindowsHost))
    {
        return
    }

    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    $created = $false
    $adopted = $false
    if (Test-Path -LiteralPath $root)
    {
        $entries = @(Get-ChildItem -LiteralPath $root -Force -ErrorAction SilentlyContinue)
        if (-not (Test-GameWipManagedToolRootOwnership -Root $root) -and $entries.Count -ne 0)
        {
            if (-not $AdoptExisting)
            {
                throw "Refusing to adopt non-empty GameWIPTools root without persistent ownership proof: '$root'."
            }
            $adopted = $true
        }
    }
    else
    {
        New-Item -ItemType Directory -Force -Path $root | Out-Null
        $created = $true
    }

    if (-not (Test-GameWipManagedToolRootOwnership -Root $root))
    {
        [ordered]@{
            schemaVersion = 1
            owner = 'GameWIP'
            resource = 'project-tools'
            installedBySetup = $created -or -not $adopted
            adoptedByUser = $adopted
            recordedAt = (Get-Date).ToUniversalTime().ToString('o')
        } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $root '.gamewip-managed.json') -Encoding UTF8
    }

    foreach ($name in @('bin', 'tools', 'npm', 'python', 'powershell'))
    {
        New-Item -ItemType Directory -Force -Path (Join-Path $root $name) | Out-Null
    }
}

function Write-GameWipManagedToolShim
{
    param(
        [Parameter(Mandatory = $true)][string]$ToolId,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][string]$ExecutableName
    )

    $root = [string]$ProjectConfig.managedEnvironment.gameWipToolsRoot
    $binRoot = Join-Path $root 'bin'
    New-Item -ItemType Directory -Force -Path $binRoot | Out-Null
    $relativeExecutable = "..\tools\$ToolId\$Version\$ExecutableName"
    $shimPath = Join-Path $binRoot "$ToolId.cmd"
    $shim = "@echo off`r`n`"%~dp0$relativeExecutable`" %*`r`n"
    [IO.File]::WriteAllText($shimPath, $shim, [Text.UTF8Encoding]::new($false))
}

function Get-GameWipExecutableNames
{
    param([Parameter(Mandatory = $true)][string]$Command)

    if (Test-GameWipWindowsHost)
    {
        return @("$Command.exe", "$Command.cmd", "$Command.bat", "$Command.ps1", $Command)
    }
    return @($Command)
}

function Add-GameWipToolCandidate
{
    param(
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.List[object]]$Candidates,
        [Parameter(Mandatory = $true)][AllowEmptyCollection()][System.Collections.Generic.HashSet[string]]$Seen,
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Source
    )

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return
    }
    $resolved = [IO.Path]::GetFullPath($Path)
    if ($Seen.Add($resolved))
    {
        $Candidates.Add([pscustomobject]@{ Path = $resolved; Source = $Source }) | Out-Null
    }
}

function Get-GameWipToolCandidates
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $candidates = [System.Collections.Generic.List[object]]::new()
    $comparer = if (Test-GameWipWindowsHost) { [StringComparer]::OrdinalIgnoreCase } else { [StringComparer]::Ordinal }
    $seen = [System.Collections.Generic.HashSet[string]]::new($comparer)
    $aliases = if ($Tool.detection.Contains('aliases')) { @($Tool.detection.aliases) } else { @() }
    $commands = @([string]$Tool.detection.command) + $aliases

    if ($Tool.detection.Contains('repositoryPath'))
    {
        Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $RepositoryRoot ([string]$Tool.detection.repositoryPath)) -Source 'declared repository tool'
    }

    if (Test-GameWipWindowsHost)
    {
        if ($Tool.provider.kind -eq 'msys2')
        {
            $environment = [string]$Tool.provider.environment
            $bin = if ($environment -eq 'common')
            {
                Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'usr\bin'
            }
            else
            {
                Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) "$environment\bin"
            }
            foreach ($command in $commands)
            {
                foreach ($name in @(Get-GameWipExecutableNames -Command $command))
                {
                    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $bin $name) -Source "managed MSYS2 $environment"
                }
            }
        }
        elseif ($Tool.provider.kind -eq 'winget' -and $Tool.provider.Contains('managedPath'))
        {
            Add-GameWipToolCandidate `
                -Candidates $candidates `
                -Seen $seen `
                -Path (Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) ([string]$Tool.provider.managedPath)) `
                -Source 'managed environment'
        }

        # Prefer the location owned by the tool's declared provider. Other
        # GameWIP-managed copies are still discovered below and reported as
        # additional candidates, but they cannot override the provider copy.
        foreach ($managedRoot in @(Get-GameWipProviderManagedToolPath -Tool $Tool))
        {
            foreach ($command in $commands)
            {
                foreach ($name in @(Get-GameWipExecutableNames -Command $command))
                {
                    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $managedRoot $name) -Source "GameWIPTools $($Tool.provider.kind)"
                }
            }
        }

        foreach ($managedRoot in @(Get-GameWipManagedToolPath))
        {
            foreach ($command in $commands)
            {
                foreach ($name in @(Get-GameWipExecutableNames -Command $command))
                {
                    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $managedRoot $name) -Source 'GameWIPTools'
                }
            }
        }
    }

    foreach ($command in $commands)
    {
        foreach ($resolved in @(Get-Command $command -All -ErrorAction SilentlyContinue))
        {
            if (-not [string]::IsNullOrWhiteSpace([string]$resolved.Source))
            {
                Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path $resolved.Source -Source 'PATH'
            }
        }
    }

    return @($candidates)
}

function Resolve-GameWipToolCommand
{
    param(
        [hashtable]$Tool,
        [string]$Command
    )

    if ($null -ne $Tool)
    {
        $candidates = @(Get-GameWipToolCandidates -Tool $Tool)
        if ($candidates.Count -eq 0)
        {
            return $null
        }
        return $candidates[0].Path
    }

    if ([string]::IsNullOrWhiteSpace($Command))
    {
        throw 'Resolve-GameWipToolCommand requires -Tool or -Command.'
    }

    $names = @(Get-GameWipExecutableNames -Command $Command)
    if (Test-GameWipWindowsHost)
    {
        foreach ($managedRoot in @(Get-GameWipManagedToolPath))
        {
            foreach ($name in $names)
            {
                $candidate = Join-Path $managedRoot $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
            }
        }
        foreach ($environment in @('ucrt64', 'clang64', 'usr'))
        {
            $bin = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) "$environment\bin"
            foreach ($name in $names)
            {
                $candidate = Join-Path $bin $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf) { return [IO.Path]::GetFullPath($candidate) }
            }
        }
    }

    $resolved = Get-Command $Command -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $resolved) { return $resolved.Source }
    return $null
}

function Get-GameWipToolCandidateVersion
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [Parameter(Mandatory = $true)][string]$Path
    )

    $previousPath = $env:PATH
    $previousPreference = $ErrorActionPreference
    try
    {
        $managed = @(Get-GameWipManagedToolPath)
        if ($managed.Count -ne 0)
        {
            $env:PATH = (@($managed) + @($env:PATH)) -join [IO.Path]::PathSeparator
        }
        $ErrorActionPreference = 'Continue'
        $output = (& $Path @($Tool.detection.versionArguments) 2>&1 | Out-String).Trim()
        $match = [regex]::Match($output, [string]$Tool.detection.versionPattern)
        if ($match.Success)
        {
            return $match.Groups[1].Value
        }
        return $null
    }
    catch
    {
        return $null
    }
    finally
    {
        $env:PATH = $previousPath
        $ErrorActionPreference = $previousPreference
    }
}

function Get-GameWipPowerShellGalleryCandidates
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $items = [System.Collections.Generic.List[object]]::new()
    if (Test-GameWipWindowsHost)
    {
        $managedRoot = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) "powershell\$($Tool.provider.package)"
        foreach ($directory in @(Get-ChildItem -LiteralPath $managedRoot -Directory -ErrorAction SilentlyContinue))
        {
            $items.Add([pscustomobject]@{ Version = $directory.Name; Location = $directory.FullName; Source = 'GameWIPTools' }) | Out-Null
        }
    }
    foreach ($module in @(Get-Module -ListAvailable -Name ([string]$Tool.provider.package)))
    {
        $items.Add([pscustomobject]@{ Version = $module.Version.ToString(); Location = $module.ModuleBase; Source = 'PSModulePath' }) | Out-Null
    }
    return @($items | Sort-Object @{ Expression = { if ($_.Source -eq 'GameWIPTools') { 0 } else { 1 } } }, @{ Expression = { try { [version]$_.Version } catch { [version]'0.0' } }; Descending = $true })
}

function Get-GameWipDetectedTool
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if ($Tool.provider.kind -eq 'powershellGallery')
    {
        $candidates = @(Get-GameWipPowerShellGalleryCandidates -Tool $Tool)
        if ($candidates.Count -eq 0)
        {
            return [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; Candidates = @() }
        }
        $selected = $candidates[0]
        return [pscustomobject]@{
            Installed = $true
            Version = $selected.Version
            Location = $selected.Location
            Source = $selected.Source
            Candidates = $candidates
        }
    }

    $candidates = @(Get-GameWipToolCandidates -Tool $Tool)
    if ($candidates.Count -eq 0)
    {
        return [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; Candidates = @() }
    }
    $selected = $candidates[0]
    return [pscustomobject]@{
        Installed = $true
        Version = Get-GameWipToolCandidateVersion -Tool $Tool -Path $selected.Path
        Location = $selected.Path
        Source = $selected.Source
        Candidates = $candidates
    }
}

function Get-GameWipToolCompatibility
{
    param([hashtable]$Tool, $Detected)
    if (-not $Detected.Installed)
    {
        return 'missing'
    }
    if ($Tool.versionPolicy -in @('managed', 'informational'))
    {
        return 'compatible'
    }
    if ([string]::IsNullOrWhiteSpace([string]$Detected.Version))
    {
        return 'unknown'
    }
    if ($Tool.versionPolicy -eq 'exact')
    {
        return $(if ($Detected.Version -eq $Tool.requiredVersion)
            {
                'compatible'
            }
            else
            {
                'mismatch'
            })
    }
    try
    {
        return $(if ([version]$Detected.Version -ge [version]$Tool.requiredVersion)
            {
                'compatible'
            }
            else
            {
                'outdated'
            })
    }
    catch
    {
        return 'unknown'
    }
}

function Test-GameWipDetectedToolFromDeclaredProvider
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [Parameter(Mandatory = $true)]$Detected
    )

    if (-not (Test-GameWipWindowsHost))
    {
        return $true
    }

    switch ([string]$Tool.provider.kind)
    {
        'python' { return [string]$Detected.Source -eq 'GameWIPTools python' }
        'npm' { return [string]$Detected.Source -eq 'GameWIPTools npm' }
        'githubRelease' { return [string]$Detected.Source -eq 'GameWIPTools githubRelease' }
        default { return $true }
    }
}

function Get-GameWipProviderFunction
{
    param([hashtable]$Tool, [ValidateSet('Latest', 'Install')][string]$Operation)
    $providerName = switch ([string]$Tool.provider.kind)
    {
        'msys2'
        {
            'Msys2'
        }; 'npm'
        {
            'Npm'
        }; 'python'
        {
            'Python'
        }; 'powershellGallery'
        {
            'PowerShellGallery'
        }
        'githubRelease'
        {
            'GitHubRelease'
        }; 'winget'
        {
            'Winget'
        }; 'gitSubmodule'
        {
            'GitSubmodule'
        }; 'external'
        {
            'External'
        }
        default
        {
            throw "Unsupported tool provider '$($Tool.provider.kind)'."
        }
    }
    if ($Operation -eq 'Latest')
    {
        return "Get-GameWip${providerName}ToolLatestVersion"
    }
    return "Install-GameWip${providerName}Tool"
}

function Show-GameWipToolList
{
    Write-GameWipSection 'Project tools'
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $capabilities = @('detectInstalled', 'checkLatest', 'update') | Where-Object { [bool]$toolInfo.capabilities[$_] }
        Write-Host ('  {0,-20} {1,-24} {2,-16} {3,-18} {4,-13} {5}' -f $toolInfo.id, $toolInfo.name, $toolInfo.category, $toolInfo.provider.kind, $toolInfo.versionPolicy, ($capabilities -join ','))
    }
}

function Show-GameWipToolStatus
{
    Initialize-GameWipStorage
    Write-GameWipSection 'Project tool status (offline)'
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $required = if ($toolInfo.Contains('requiredVersion')) { $toolInfo.requiredVersion } else { '-' }
        $installed = if ($detected.Installed -and $detected.Version) { $detected.Version } elseif ($detected.Installed) { 'detected' } else { 'missing' }
        Write-Host ('  {0,-20} required={1,-12} installed={2,-12} state={3,-10} provider={4,-18}' -f
            $toolInfo.id,
            $required,
            $installed,
            (Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected),
            $toolInfo.provider.kind)

        if ($detected.Installed)
        {
            Write-Host "    selected=$($detected.Location) [$($detected.Source)]"
            foreach ($candidate in @($detected.Candidates | Select-Object -Skip 1))
            {
                $candidatePath = if ($candidate.PSObject.Properties['Path']) { $candidate.Path } else { $candidate.Location }
                $candidateVersion = if ($candidate.PSObject.Properties['Version']) { $candidate.Version } else { Get-GameWipToolCandidateVersion -Tool $toolInfo -Path $candidatePath }
                Write-Host "    additional=$candidatePath version=$(if ($candidateVersion) { $candidateVersion } else { 'unknown' }) [$($candidate.Source)]"
            }
        }
        else
        {
            Write-Host '    selected=-'
        }
        Write-Host ('    detect={0}; latest={1}; update={2}' -f $toolInfo.capabilities.detectInstalled, $toolInfo.capabilities.checkLatest, $toolInfo.capabilities.update)
    }
}

function Get-GameWipToolUpdatePlan
{
    param([string]$ToolId)

    $selected = if ([string]::IsNullOrWhiteSpace($ToolId) -or $ToolId -eq 'all') { @($ProjectTools.tools) } else { @($ProjectTools.tools | Where-Object { $_.id -eq $ToolId }) }
    if ($selected.Count -eq 0)
    {
        throw "Unknown project tool '$ToolId'."
    }

    $plan = @()
    foreach ($toolInfo in $selected)
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $latest = $null
        $releaseMetadata = $null
        if ($toolInfo.capabilities.checkLatest)
        {
            if ($toolInfo.provider.kind -eq 'githubRelease')
            {
                try
                {
                    $releaseMetadata = Get-GameWipGitHubReleaseMetadata -Tool $toolInfo
                    $latest = $releaseMetadata.Version
                }
                catch
                {
                    Write-Warning "Could not resolve latest GitHub release metadata for '$($toolInfo.id)': $($_.Exception.Message)"
                }
            }
            else
            {
                $functionName = Get-GameWipProviderFunction -Tool $toolInfo -Operation Latest
                $latest = & $functionName -Tool $toolInfo
            }
        }

        $latestDependencies = @{}
        if ($toolInfo.provider.kind -eq 'npm' -and $toolInfo.provider.Contains('dependencies'))
        {
            foreach ($dependency in @($toolInfo.provider.dependencies))
            {
                $dependencyLatest = Get-GameWipNpmPackageLatestVersion -Package ([string]$dependency.package)
                if ($dependencyLatest)
                {
                    $latestDependencies[[string]$dependency.package] = $dependencyLatest
                }
            }
        }
        $plan += [pscustomobject]@{
            Tool = $toolInfo
            Installed = $detected.Version
            Latest = $latest
            LatestDependencies = $latestDependencies
            ReleaseMetadata = $releaseMetadata
        }
    }
    return $plan
}

function Show-GameWipToolUpdatePlan
{
    param([array]$Plan)

    foreach ($item in $Plan)
    {
        $required = if ($item.Tool.Contains('requiredVersion')) { $item.Tool.requiredVersion } else { '-' }
        Write-Host ('  {0,-20} required={1,-12} installed={2,-12} latest={3}' -f
            $item.Tool.id,
            $required,
            $(if ($item.Installed) { $item.Installed } else { 'missing' }),
            $(if ($item.Latest) { $item.Latest } else { 'not-supported/unknown' }))
        $providerDependencies = if ($item.Tool.provider.Contains('dependencies')) { @($item.Tool.provider.dependencies) } else { @() }
        foreach ($dependency in $providerDependencies)
        {
            if (-not $dependency.Contains('version')) { continue }
            $latest = if ($item.LatestDependencies.ContainsKey([string]$dependency.package)) { $item.LatestDependencies[[string]$dependency.package] } else { 'unknown' }
            Write-Host "    dependency $($dependency.package): required=$($dependency.version) latest=$latest"
        }
    }
}

function Format-GameWipProjectToolRegistry
{
    $prettierTool = Get-GameWipProjectTool -Id 'prettier'
    $detected = Get-GameWipDetectedTool -Tool $prettierTool
    if ((Get-GameWipToolCompatibility -Tool $prettierTool -Detected $detected) -ne 'compatible')
    {
        throw 'Prettier is unavailable after the tool update; cannot normalize project-tools.json.'
    }
    $config = Join-Path $RepositoryRoot 'config\quality\prettier.json'
    Invoke-GameWipNative `
        -Name 'prettier-project-tools' `
        -FilePath $detected.Location `
        -Arguments @('--config', $config, '--write', $ProjectToolsPath)
}

function Invoke-GameWipToolUpdate
{
    param(
        [string]$ToolId,
        [switch]$PreviewOnly,
        [switch]$Confirm
    )

    if ([string]::IsNullOrWhiteSpace($ToolId)) { throw "tools update requires -Tool <id|all>." }

    # Resolve and retain the complete online plan before the first mutation.
    $plan = @(Get-GameWipToolUpdatePlan -ToolId $ToolId)
    Write-GameWipSection "Tool update plan$(if ($PreviewOnly) { ' (preview)' })"
    Show-GameWipToolUpdatePlan -Plan $plan

    $unresolved = @(
        $plan | Where-Object {
            $_.Tool.capabilities.update -and
            $_.Tool.capabilities.checkLatest -and
            [string]::IsNullOrWhiteSpace([string]$_.Latest)
        }
    )
    if ($unresolved.Count -ne 0)
    {
        throw "Unable to resolve the complete update plan for: $($unresolved.Tool.id -join ', '). No tracked files were changed."
    }
    if ($PreviewOnly) { return }
    if ($Confirm -and -not (Read-GameWipYesNo -Prompt 'Apply this project-tool update plan?' -Default $false))
    {
        Write-Host 'Project-tool update cancelled; no tracked files were changed.'
        return
    }

    Assert-GameWipCleanTrackedTree
    Initialize-GameWipManagedToolRoot
    $registryMayHaveChanged = $false
    foreach ($item in $plan)
    {
        if (-not $item.Tool.capabilities.update)
        {
            Write-Host "  [skip] $($item.Tool.id): provider does not support updates"
            continue
        }

        if ($item.Tool.versionPolicy -eq 'exact' -and $item.Latest -and $item.Latest -ne $item.Tool.requiredVersion)
        {
            Sync-GameWipToolVersionReference `
                -Tool $item.Tool `
                -NewVersion $item.Latest `
                -ReleaseMetadata $item.ReleaseMetadata
            $registryMayHaveChanged = $true
        }
        if ($item.LatestDependencies.Count -ne 0)
        {
            Sync-GameWipToolDependencyVersions -Tool $item.Tool -LatestDependencies $item.LatestDependencies
            $registryMayHaveChanged = $true
        }

        $version = if ($item.Tool.Contains('requiredVersion')) { [string]$item.Tool.requiredVersion } else { $null }
        $functionName = Get-GameWipProviderFunction -Tool $item.Tool -Operation Install
        & $functionName -Tool $item.Tool -Version $version
    }

    if ($registryMayHaveChanged)
    {
        Format-GameWipProjectToolRegistry
    }
    Invoke-GameWipQuality -Mode check
    & git -C $RepositoryRoot status --short
}

function Set-GameWipPreciseTextVersionReference
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Reference,
        [Parameter(Mandatory = $true)][string]$OldVersion,
        [Parameter(Mandatory = $true)][string]$NewVersion
    )

    if ($Reference.kind -eq 'path') { return }
    $path = Join-Path $RepositoryRoot ([string]$Reference.path)
    $content = Get-Content -Raw -LiteralPath $path
    $expectedCount = if ($Reference.Contains('expectedCount')) { [int]$Reference.expectedCount } else { 1 }
    if ($Reference.kind -eq 'cmakeMinimum')
    {
        $oldText = "cmake_minimum_required(VERSION $OldVersion)"
        $newText = "cmake_minimum_required(VERSION $NewVersion)"
    }
    elseif ($Reference.kind -eq 'text')
    {
        $pattern = [string]$Reference.pattern
        $oldText = $pattern.Replace('{version}', $OldVersion)
        $newText = $pattern.Replace('{version}', $NewVersion)
    }
    else
    {
        throw "Unknown live version reference kind '$($Reference.kind)'."
    }

    $count = [regex]::Matches($content, [regex]::Escape($oldText)).Count
    if ($count -ne $expectedCount)
    {
        throw "Live version reference '$($Reference.path)' expected $expectedCount exact match(es), found $count."
    }
    [IO.File]::WriteAllText($path, $content.Replace($oldText, $newText), [Text.UTF8Encoding]::new($false))
}

function Sync-GameWipToolDependencyVersions
{
    param([hashtable]$Tool, [hashtable]$LatestDependencies)

    if ($LatestDependencies.Count -eq 0) { return }
    $registry = Get-Content -Raw -LiteralPath $ProjectToolsPath | ConvertFrom-Json
    $registryTool = $registry.tools | Where-Object { $_.id -eq $Tool.id } | Select-Object -First 1
    $providerDependencies = if ($Tool.provider.Contains('dependencies')) { @($Tool.provider.dependencies) } else { @() }
    foreach ($dependency in $providerDependencies)
    {
        if (-not $dependency.Contains('version') -or -not $LatestDependencies.ContainsKey([string]$dependency.package)) { continue }
        $latest = [string]$LatestDependencies[[string]$dependency.package]
        if ($latest -eq [string]$dependency.version) { continue }
        $registryDependency = $registryTool.provider.dependencies | Where-Object { $_.package -eq $dependency.package } | Select-Object -First 1
        if ($null -eq $registryDependency) { throw "Provider dependency '$($dependency.package)' disappeared from project-tools.json." }
        $registryDependency.version = $latest
        $dependency.version = $latest
    }
    [IO.File]::WriteAllText($ProjectToolsPath, (($registry | ConvertTo-Json -Depth 30) + "`n"), [Text.UTF8Encoding]::new($false))
}

function Sync-GameWipToolVersionReference
{
    param(
        [hashtable]$Tool,
        [Parameter(Mandatory = $true)][string]$NewVersion,
        [AllowNull()]$ReleaseMetadata
    )

    if ($Tool.provider.kind -eq 'githubRelease')
    {
        if ($null -eq $ReleaseMetadata -or
            $ReleaseMetadata.Version -ne $NewVersion -or
            $ReleaseMetadata.Assets.Count -lt 2)
        {
            throw "Incomplete planned release metadata for '$($Tool.id)' $NewVersion. No tracked files were changed."
        }
    }

    $oldVersion = [string]$Tool.requiredVersion
    $references = if ($Tool.Contains('references')) { @($Tool.references) } else { @() }
    foreach ($reference in $references)
    {
        if ([string]$reference.path -like 'docs/releases/*') { continue }
        Set-GameWipPreciseTextVersionReference -Reference $reference -OldVersion $oldVersion -NewVersion $NewVersion
    }

    $registry = Get-Content -Raw -LiteralPath $ProjectToolsPath | ConvertFrom-Json
    $registryTool = $registry.tools | Where-Object { $_.id -eq $Tool.id } | Select-Object -First 1
    if ($null -eq $registryTool) { throw "Tool '$($Tool.id)' disappeared from project-tools.json." }
    $registryTool.requiredVersion = $NewVersion

    if ($Tool.provider.kind -eq 'githubRelease')
    {
        $registryTool.provider.releaseTag = $ReleaseMetadata.Tag
        $Tool.provider.releaseTag = $ReleaseMetadata.Tag
        foreach ($key in @('linux-amd64', 'windows-amd64'))
        {
            $plannedAsset = $ReleaseMetadata.Assets[$key]
            if ($null -eq $plannedAsset)
            {
                throw "Planned release metadata for '$($Tool.id)' is missing '$key'."
            }
            $registryTool.provider.assets.$key.archive = $plannedAsset.archive
            $registryTool.provider.assets.$key.sha256 = $plannedAsset.sha256
            $Tool.provider.assets[$key].archive = $plannedAsset.archive
            $Tool.provider.assets[$key].sha256 = $plannedAsset.sha256
        }
    }

    [IO.File]::WriteAllText($ProjectToolsPath, (($registry | ConvertTo-Json -Depth 30) + "`n"), [Text.UTF8Encoding]::new($false))
    $Tool.requiredVersion = $NewVersion
}

function Invoke-GameWipToolAction
{
    param([Parameter(Mandatory = $true)][string]$Name, [string]$ToolId, [switch]$PreviewOnly)
    switch ($Name)
    {
        'list'
        {
            Show-GameWipToolList
        }
        'status'
        {
            Show-GameWipToolStatus
        }
        'check-updates'
        {
            Write-GameWipSection 'Project tool updates (online)'; Show-GameWipToolUpdatePlan -Plan @(Get-GameWipToolUpdatePlan -ToolId $ToolId)
        }
        'update'
        {
            Invoke-GameWipToolUpdate -ToolId $ToolId -PreviewOnly:$PreviewOnly
        }
    }
}

function Get-GameWipToolchainPathPrefix
{
    param([Parameter(Mandatory = $true)][string]$PresetName)

    if ($PresetName -eq 'asan')
    {
        return Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'clang64\bin'
    }
    return Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin'
}

function Test-GameWipProjectReadiness
{
    param([switch]$ThrowOnFailure)

    $failures = [System.Collections.Generic.List[string]]::new()
    Write-GameWipSection 'Project readiness'
    foreach ($toolInfo in @($ProjectTools.tools | Where-Object { $_.capabilities.detectInstalled -and $_.versionPolicy -ne 'informational' }))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $compatibility = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected
        if ($compatibility -eq 'compatible')
        {
            Write-Host "  [ready] $($toolInfo.name): $($detected.Location)"
        }
        else
        {
            Write-Host "  [$compatibility] $($toolInfo.name): $(if ($detected.Location) { $detected.Location } else { 'not found' })" -ForegroundColor Yellow
            $failures.Add([string]$toolInfo.name) | Out-Null
        }
    }

    if (Test-Path -LiteralPath (Join-Path $RepositoryRoot '.git')) { Write-Host '  [ready] Git repository metadata' }
    else { Write-Host '  [missing] Git repository metadata' -ForegroundColor Yellow; $failures.Add('Git repository metadata') | Out-Null }

    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($RepositoryRoot))
    if ($drive.IsReady) { Write-Host ('  Free disk space: {0:N1} GB' -f ($drive.AvailableFreeSpace / 1GB)) }

    if ($failures.Count -ne 0)
    {
        $message = "$($failures.Count) project requirement(s) are missing or incompatible. Run .\setup.bat repair, then rerun gamewip."
        if ($ThrowOnFailure) { throw $message }
        Write-Host "  $message" -ForegroundColor Yellow
        return $false
    }
    Write-Host '  Ready: the complete declared project toolchain is available.' -ForegroundColor Green
    return $true
}

function Confirm-GameWipToolchain
{
    param([Parameter(Mandatory = $true)][string]$PresetName)
    $prefix = Get-GameWipToolchainPathPrefix $PresetName
    if (-not (Test-Path -LiteralPath (Join-Path $prefix 'cmake.exe')))
    {
        Test-GameWipProjectReadiness -ThrowOnFailure | Out-Null
    }
}

function Resolve-GameWipPython
{
    $candidate = $null
    $source = $null
    $pythonOverride = Get-Variable -Name PythonPath -ValueOnly -ErrorAction SilentlyContinue
    if (-not [string]::IsNullOrWhiteSpace([string]$pythonOverride))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$pythonOverride)
        $source = '-PythonPath override'
        if (-not (Test-Path -LiteralPath $candidate)) { throw "Python override does not exist: $candidate" }
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_PYTHON))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$env:GAMEWIP_PYTHON)
        $source = 'GAMEWIP_PYTHON override'
        if (-not (Test-Path -LiteralPath $candidate)) { throw "GAMEWIP_PYTHON does not exist: $candidate" }
    }
    elseif (Test-GameWipWindowsHost)
    {
        $configured = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin\python.exe'
        if (Test-Path -LiteralPath $configured) { $candidate = $configured; $source = 'GameWIP UCRT64 toolchain' }
    }

    if ($null -eq $candidate)
    {
        foreach ($name in @('python3', 'python', 'python.exe'))
        {
            $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -ne $command) { $candidate = $command.Source; $source = 'PATH fallback'; break }
        }
    }
    if ($null -eq $candidate) { throw 'Python is unavailable. Run setup.bat repair on Windows or provision Python on the CI runner.' }

    $versionOutput = @(& $candidate --version 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Python failed to start from '$candidate' with exit code $LASTEXITCODE." }
    return [pscustomobject]@{ Path = $candidate; Source = $source; Version = (($versionOutput | Out-String).Trim()) }
}

function Resolve-GameWipPythonProviderHost
{
    $candidate = $null
    $source = $null
    if (Test-GameWipWindowsHost)
    {
        $pythonOverride = Get-Variable -Name PythonProviderHostPath -ValueOnly -ErrorAction SilentlyContinue
        if (-not [string]::IsNullOrWhiteSpace([string]$pythonOverride))
        {
            $candidate = Resolve-GameWipRepositoryPath -Path ([string]$pythonOverride)
            $source = '-PythonProviderHostPath override'
            if (-not (Test-Path -LiteralPath $candidate)) { throw "Python provider host override does not exist: $candidate" }
        }
        else
        {
            try
            {
                $pyLauncher = & where.exe py.exe 2>$null | Select-Object -First 1
                if ($null -ne $pyLauncher)
                {
                    $versionOutput = @(& $pyLauncher -3.14 --version 2>&1)
                    if ($LASTEXITCODE -eq 0)
                    {
                        $candidate = $pyLauncher
                        $source = 'Windows Python launcher (py.exe) for 3.14'
                    }
                }
            }
            catch { }
        }
    }
    else
    {
        $pythonOverride = Get-Variable -Name PythonProviderHostPath -ValueOnly -ErrorAction SilentlyContinue
        if (-not [string]::IsNullOrWhiteSpace([string]$pythonOverride))
        {
            $candidate = Resolve-GameWipRepositoryPath -Path ([string]$pythonOverride)
            $source = '-PythonProviderHostPath override'
            if (-not (Test-Path -LiteralPath $candidate)) { throw "Python provider host override does not exist: $candidate" }
        }
        else
        {
            foreach ($name in @('python3', 'python', 'python.exe'))
            {
                $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
                if ($null -ne $command) { $candidate = $command.Source; $source = 'PATH fallback'; break }
            }
        }
    }

    if ($null -eq $candidate) { throw 'Python provider host is unavailable. Run setup.bat tools on Windows or provision Python on the CI runner.' }

    $versionOutput = @(& $candidate -c "import sys; print(sys.executable)" 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "Python provider host failed to start from '$candidate' with exit code $LASTEXITCODE." }
    $pythonExecutable = ($versionOutput | Out-String).Trim()
    if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Windows))
    {
        if ($pythonExecutable -ilike '*\msys*' -or $pythonExecutable -ilike '*\ucrt64\*')
        {
            throw "Python provider host must not be MSYS2/UCRT64 Python; found: $pythonExecutable"
        }
    }
    return [pscustomobject]@{ Path = $pythonExecutable; Source = $source; Version = "Python 3.14 (provider host)" }
}

function Resolve-GameWipClangFormat
{
    $candidate = $null
    $source = $null
    $clangFormatOverride = Get-Variable -Name ClangFormatPath -ValueOnly -ErrorAction SilentlyContinue
    if (-not [string]::IsNullOrWhiteSpace([string]$clangFormatOverride))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$clangFormatOverride)
        $source = '-ClangFormatPath override'
        if (-not (Test-Path -LiteralPath $candidate)) { throw "clang-format override does not exist: $candidate" }
    }
    elseif (-not [string]::IsNullOrWhiteSpace($env:GAMEWIP_CLANG_FORMAT))
    {
        $candidate = Resolve-GameWipRepositoryPath -Path ([string]$env:GAMEWIP_CLANG_FORMAT)
        $source = 'GAMEWIP_CLANG_FORMAT override'
        if (-not (Test-Path -LiteralPath $candidate)) { throw "GAMEWIP_CLANG_FORMAT does not exist: $candidate" }
    }
    elseif (Test-GameWipWindowsHost)
    {
        $configured = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'ucrt64\bin\clang-format.exe'
        if (Test-Path -LiteralPath $configured) { $candidate = $configured; $source = 'GameWIP UCRT64 toolchain' }
    }

    if ($null -eq $candidate)
    {
        foreach ($name in @('clang-format', 'clang-format.exe'))
        {
            $command = Get-Command $name -ErrorAction SilentlyContinue | Select-Object -First 1
            if ($null -ne $command) { $candidate = $command.Source; $source = 'PATH fallback'; break }
        }
    }
    if ($null -eq $candidate) { throw 'clang-format is unavailable. Run setup.bat repair on Windows or provision clang-format on the CI runner.' }

    $versionOutput = @(& $candidate --version 2>&1)
    if ($LASTEXITCODE -ne 0) { throw "clang-format failed to start from '$candidate' with exit code $LASTEXITCODE." }
    return [pscustomobject]@{ Path = $candidate; Source = $source; Version = (($versionOutput | Out-String).Trim()) }
}
