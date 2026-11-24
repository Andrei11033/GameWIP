# ------------------------------------------------------------
# Linux tool removal inventory
# ------------------------------------------------------------

function Get-GameWipLinuxUninstallInventory
{
    param([Parameter(Mandatory = $true)][string]$RepositoryRoot)

    # Ignore external tool roots already covered by the checkout build tree.
    $toolRoot = Get-GameWipManagedToolRoot
    $buildRoot = Join-Path $RepositoryRoot 'build'

    if (Test-GameWipPathWithinRoot -Path $toolRoot -Root $buildRoot)
    {
        return [pscustomobject]@{
            ManagedToolRoot = $toolRoot
            Proven = @()
            Unknown = @()
            Planned = @()
            Preserved = @()
            RemovePaths = @()
        }
    }

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

    # Classify shared provider-managed tool directories.
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

    # Verify the separate CMake archive marker.
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

    # Match installed archive directories to declared provider entries.
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
                            $preserved.Add("$($version.FullName) (currently running helper)") | Out-Null
                            continue
                        }

                        $planned.Add($version.FullName) | Out-Null
                        $removePaths.Add($version.FullName) | Out-Null
                    }
                }
                elseif ($family.Name -eq 'tracy' -and (Test-GameWipLinuxOwnedDirectory -Path $family.FullName))
                {
                    # Separate marked Linux profiler data from unknown Tracy files.
                    $managedTracyDirectories = [System.Collections.Generic.List[string]]::new()
                    $unknownTracyDirectories = [System.Collections.Generic.List[string]]::new()

                    foreach ($installation in @(Get-ChildItem -LiteralPath $family.FullName -Force))
                    {
                        if ($installation.PSIsContainer -and
                            $installation.Name -eq 'linux' -and
                            (Test-GameWipLinuxOwnedDirectory -Path $installation.FullName) -and
                            (Test-GameWipLinuxTracyInstallation -ToolRoot $installation.FullName))
                        {
                            $managedTracyDirectories.Add($installation.FullName) | Out-Null
                        }
                        else
                        {
                            $unknownTracyDirectories.Add($installation.FullName) | Out-Null
                            $unknown.Add("$($installation.FullName) (Tracy ownership is unknown)") | Out-Null
                            $preserved.Add($installation.FullName) | Out-Null
                        }
                    }

                    # Remove the family only when every entry is recognized.
                    if ($unknownTracyDirectories.Count -eq 0 -and $managedTracyDirectories.Count -ne 0)
                    {
                        $proven.Add("$($family.FullName) (Tracy profiler version marker)") | Out-Null
                        $planned.Add($family.FullName) | Out-Null
                        $removePaths.Add($family.FullName) | Out-Null
                    }
                    else
                    {
                        foreach ($path in $managedTracyDirectories)
                        {
                            $proven.Add("$path (Tracy profiler version marker)") | Out-Null
                            $planned.Add($path) | Out-Null
                            $removePaths.Add($path) | Out-Null
                        }
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

    return [pscustomobject]@{
        ManagedToolRoot = $toolRoot
        Proven = $proven.ToArray()
        Unknown = $unknown.ToArray()
        Planned = $planned.ToArray()
        Preserved = $preserved.ToArray()
        RemovePaths = $removePaths.ToArray()
    }
}
