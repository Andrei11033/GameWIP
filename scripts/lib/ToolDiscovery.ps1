# GameWIP deterministic tool discovery and installed-version selection.

Set-StrictMode -Version Latest

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
    if ((Test-GameWipWindowsHost) -and [IO.Path]::GetExtension($resolved).ToLowerInvariant() -notin @('.exe', '.com', '.cmd', '.bat', '.ps1'))
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
    $candidates = [System.Collections.Generic.List[object]]::new()
    $comparer = if (Test-GameWipWindowsHost)
    {
        [StringComparer]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparer]::Ordinal
    }
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

    if ($Tool.detection.Contains('repositoryPath'))
    {
        Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $RepositoryRoot ([string]$Tool.detection.repositoryPath)) -Source 'declared repository tool' -Priority 10 -SelectionReason 'repository-declared tool path'
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
                    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $bin $name) -Source "managed MSYS2 $environment" -Priority 20 -SelectionReason 'declared provider location'
                }
            }
        }
        elseif ($Tool.provider.kind -eq 'winget' -and $Tool.provider.Contains('managedPath'))
        {
            Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) ([string]$Tool.provider.managedPath)) -Source 'managed environment' -Priority 20 -SelectionReason 'declared provider location'
        }

        foreach ($managedRoot in @(Get-GameWipProviderManagedToolPath -Tool $Tool))
        {
            foreach ($command in $commands)
            {
                foreach ($name in @(Get-GameWipExecutableNames -Command $command))
                {
                    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $managedRoot $name) -Source "GameWIPTools $($Tool.provider.kind)" -Priority 20 -SelectionReason 'declared provider-managed location'
                }
            }
        }
        foreach ($managedRoot in @(Get-GameWipManagedToolPath))
        {
            foreach ($command in $commands)
            {
                foreach ($name in @(Get-GameWipExecutableNames -Command $command))
                {
                    Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path (Join-Path $managedRoot $name) -Source 'GameWIPTools' -Priority 50 -SelectionReason 'other GameWIP-managed location'
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
                Add-GameWipToolCandidate -Candidates $candidates -Seen $seen -Path $resolved.Source -Source 'PATH' -Priority 100 -SelectionReason 'PATH fallback'
            }
        }
    }
    return @($candidates | Sort-Object Priority, Path)
}

function Get-GameWipPowerShellGalleryCandidates
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    $items = [System.Collections.Generic.List[object]]::new()
    if (Test-GameWipWindowsHost)
    {
        $managedRoot = Join-Path ((Get-GameWipManagedToolRoot)) "powershell\$($Tool.provider.package)"
        foreach ($directory in @(Get-ChildItem -LiteralPath $managedRoot -Directory -ErrorAction SilentlyContinue))
        {
            $items.Add([pscustomobject]@{ Version = $directory.Name; Location = $directory.FullName; Source = 'GameWIPTools powershellGallery'; Priority = 20; SelectionReason = 'declared provider-managed location' }) | Out-Null
        }
    }
    foreach ($module in @(Get-Module -ListAvailable -Name ([string]$Tool.provider.package)))
    {
        $items.Add([pscustomobject]@{ Version = $module.Version.ToString(); Location = $module.ModuleBase; Source = 'PSModulePath'; Priority = 100; SelectionReason = 'PSModulePath fallback' }) | Out-Null
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

function Resolve-GameWipToolCommand
{
    param([hashtable]$Tool, [string]$Command)
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
                if (Test-Path -LiteralPath $candidate -PathType Leaf)
                {
                    return [IO.Path]::GetFullPath($candidate)
                }
            }
        }
        foreach ($environment in @('ucrt64', 'clang64', 'usr'))
        {
            $bin = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) "$environment\bin"
            foreach ($name in $names)
            {
                $candidate = Join-Path $bin $name
                if (Test-Path -LiteralPath $candidate -PathType Leaf)
                {
                    return [IO.Path]::GetFullPath($candidate)
                }
            }
        }
    }
    $resolved = Get-Command $Command -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($null -ne $resolved)
    {
        return $resolved.Source
    }
    return $null
}

function Get-GameWipDetectedTool
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    if ($Tool.provider.kind -eq 'powershellGallery')
    {
        $candidates = @(Get-GameWipPowerShellGalleryCandidates -Tool $Tool)
        if ($candidates.Count -eq 0)
        {
            return [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; SelectionReason = $null; Candidates = @() }
        }
        $selected = $candidates[0]
        return [pscustomobject]@{ Installed = $true; Version = $selected.Version; Location = $selected.Location; Source = $selected.Source; SelectionReason = $selected.SelectionReason; Candidates = $candidates }
    }
    $candidates = @(Get-GameWipToolCandidates -Tool $Tool)
    if ($candidates.Count -eq 0)
    {
        return [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; SelectionReason = $null; Candidates = @() }
    }
    $selected = $candidates[0]
    $version = Get-GameWipToolCandidateVersion -Tool $Tool -Path $selected.Path
    $usesPackageQueryHost = $Tool.provider.kind -in @('python', 'npm') -and [string]$Tool.detection.command -ne [string]$Tool.provider.package
    if ($usesPackageQueryHost -and $null -eq $version)
    {
        # A package-query host may exist more than once on PATH. Select the
        # first host that contains the package instead of treating an earlier
        # interpreter without it as authoritative.
        foreach ($candidate in @($candidates | Select-Object -Skip 1))
        {
            $candidateVersion = Get-GameWipToolCandidateVersion -Tool $Tool -Path $candidate.Path
            if ($null -ne $candidateVersion)
            {
                $selected = $candidate
                $version = $candidateVersion
                break
            }
        }
    }
    if ($usesPackageQueryHost -and $null -eq $version)
    {
        # The executables are only package-query hosts (for example, Python
        # used to query jsonschema), not evidence that the package exists.
        return [pscustomobject]@{ Installed = $false; Version = $null; Location = $null; Source = $null; SelectionReason = $null; Candidates = $candidates }
    }
    return [pscustomobject]@{
        Installed = $true
        Version = $version
        Location = $selected.Path
        Source = $selected.Source
        SelectionReason = $selected.SelectionReason
        Candidates = $candidates
    }
}

function Show-GameWipToolStatus
{
    Initialize-GameWipStorage
    Write-GameWipSection 'Project tool status (offline)'
    Write-Host '  Versions are read from this machine; no network requests are made.'
    Write-Host ''
    Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-11} {4}' -f 'Tool', 'Requirement', 'Installed', 'State', 'Provider')
    Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-11} {4}' -f ('-' * 20), ('-' * 13), ('-' * 13), ('-' * 11), ('-' * 18))
    $results = [System.Collections.Generic.List[object]]::new()
    foreach ($toolInfo in @($ProjectTools.tools))
    {
        $detected = Get-GameWipDetectedTool -Tool $toolInfo
        $required = if ($toolInfo.Contains('requiredVersion'))
        {
            [string]$toolInfo.requiredVersion
        }
        else
        {
            [string]$toolInfo.versionPolicy
        }
        $installed = if ($detected.Installed -and $detected.Version)
        {
            [string]$detected.Version
        }
        elseif ($detected.Installed)
        {
            'unknown'
        }
        else
        {
            '-'
        }
        if ($installed.Length -gt 13)
        {
            $installed = $installed.Substring(0, 10) + '...'
        }
        $state = Get-GameWipToolCompatibility -Tool $toolInfo -Detected $detected
        Write-Host ('  {0,-20} {1,-13} {2,-13} {3,-11} {4}' -f $toolInfo.id, $required, $installed, $state, $toolInfo.provider.kind)
        $results.Add([pscustomobject]@{ Tool = $toolInfo; Detected = $detected; State = $state }) | Out-Null
    }

    $groups = @($results | Group-Object State | Sort-Object Name)
    Write-Host ''
    Write-Host ('  Summary: ' + (@($groups | ForEach-Object { '{0} {1}' -f $_.Count, $_.Name }) -join '; ') + '.')

    $details = @($results | Where-Object {
            $_.Tool.versionPolicy -ne 'informational' -and $_.Detected.Installed -and (@($_.Detected.Candidates).Count -gt 1 -or $_.State -ne 'compatible')
        })
    if ($details.Count -eq 0)
    {
        return
    }

    Write-GameWipSection 'Selection details'
    Write-Host '  Declared provider locations take precedence over PATH fallbacks.'
    foreach ($result in $details)
    {
        $toolInfo = $result.Tool
        $detected = $result.Detected
        Write-Host ''
        Write-Host "  $($toolInfo.id)"
        Write-Host "    Using:  $($detected.Location)"
        Write-Host "    Source: $($detected.Source) ($($detected.SelectionReason))"
        $alternatives = @($detected.Candidates | Select-Object -Skip 1)
        if ($alternatives.Count -ne 0)
        {
            Write-Host '    Also found:'
            foreach ($candidate in $alternatives)
            {
                $candidatePath = if ($candidate.PSObject.Properties['Path'])
                {
                    $candidate.Path
                }
                else
                {
                    $candidate.Location
                }
                $candidateVersion = if ($candidate.PSObject.Properties['Version'])
                {
                    $candidate.Version
                }
                else
                {
                    Get-GameWipToolCandidateVersion -Tool $toolInfo -Path $candidatePath
                }
                $version = if ($candidateVersion)
                {
                    [string]$candidateVersion
                }
                else
                {
                    'unknown version'
                }
                Write-Host "      - $candidatePath ($version; $($candidate.Source))"
            }
        }
    }
}
