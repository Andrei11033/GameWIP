# Project-tool provider and reference validation.

Set-StrictMode -Version Latest

# ------------------------------------------------------------
# Provider and reference contracts
# ------------------------------------------------------------

function Assert-GameWipProjectToolConfig
{
    Assert-GameWipUniqueId -Items @($ProjectTools.tools) -Label 'project tool'
    $providerKinds = @('msys2', 'npm', 'python', 'powershellGallery', 'githubRelease', 'archive', 'winget', 'external')
    $liveReferences = @{}

    foreach ($toolInfo in @($ProjectTools.tools))
    {
        # Validate the provider-independent contract before checking details
        # that belong to a specific installation provider.
        if ($providerKinds -notcontains $toolInfo.provider.kind)
        {
            throw "Tool '$($toolInfo.id)' uses unsupported provider '$($toolInfo.provider.kind)'."
        }

        if ($toolInfo.versionPolicy -in @('exact', 'minimum') -and -not $toolInfo.Contains('requiredVersion'))
        {
            throw "Tool '$($toolInfo.id)' requires a declared version."
        }

        if ($toolInfo.capabilities.update -and -not $toolInfo.capabilities.detectInstalled)
        {
            throw "Updatable tool '$($toolInfo.id)' must support installed-version detection."
        }

        if ($toolInfo.detection.Contains('commandConfigVersionPath'))
        {
            $configVersion = [string](Resolve-GameWipCommandConfigValue -Path ([string]$toolInfo.detection.commandConfigVersionPath))

            if ([string]::IsNullOrWhiteSpace($configVersion))
            {
                throw "Tool '$($toolInfo.id)' references an empty command configuration version."
            }
        }

        $providerDependencies = if ($toolInfo.provider.Contains('dependencies'))
        {
            @($toolInfo.provider.dependencies)
        }
        else
        {
            @()
        }

        # Provider-specific fields are checked together so each provider's
        # installation code can rely on one complete configuration contract.
        switch ([string]$toolInfo.provider.kind)
        {
            'msys2'
            {
                if (-not $toolInfo.provider.Contains('environment') -or [string]$toolInfo.provider.environment -notin @('common', 'ucrt64', 'clang64'))
                {
                    throw "MSYS2 tool '$($toolInfo.id)' must declare its managed environment."
                }

                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "MSYS2 tool '$($toolInfo.id)' must declare its pacman package."
                }

                foreach ($dependency in $providerDependencies)
                {
                    if (-not $dependency.Contains('environment'))
                    {
                        throw "MSYS2 dependency '$($dependency.package)' for '$($toolInfo.id)' must declare its environment."
                    }
                }
            }
            'npm'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "npm tool '$($toolInfo.id)' must declare its package."
                }

                foreach ($dependency in $providerDependencies)
                {
                    if (-not $dependency.Contains('version'))
                    {
                        throw "npm dependency '$($dependency.package)' for '$($toolInfo.id)' must be versioned."
                    }
                }
            }
            'python'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "Python tool '$($toolInfo.id)' must declare its package."
                }

                if ($toolInfo.provider.Contains('windowsMsys2'))
                {
                    $nativeProvider = $toolInfo.provider.windowsMsys2

                    if ([string]$nativeProvider.environment -notin @('common', 'ucrt64', 'clang64') -or
                        [string]::IsNullOrWhiteSpace([string]$nativeProvider.package))
                    {
                        throw "Python tool '$($toolInfo.id)' has an invalid Windows MSYS2 package declaration."
                    }
                }
            }
            'powershellGallery'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "PowerShell Gallery tool '$($toolInfo.id)' must declare its package."
                }
            }
            'githubRelease'
            {
                foreach ($field in @('repository', 'releaseTag', 'assets'))
                {
                    if (-not $toolInfo.provider.Contains($field))
                    {
                        throw "GitHub release tool '$($toolInfo.id)' must declare '$field'."
                    }
                }

                if ($toolInfo.provider.Contains('installMode') -and $toolInfo.provider.installMode -eq 'directoryArchive')
                {
                    foreach ($field in @('managedDirectory', 'binaryDirectory', 'validationFiles'))
                    {
                        if (-not $toolInfo.provider.Contains($field))
                        {
                            throw "GitHub archive tool '$($toolInfo.id)' must declare '$field'."
                        }
                    }

                    foreach ($field in @('managedDirectory', 'binaryDirectory'))
                    {
                        $path = [string]$toolInfo.provider[$field]

                        if ([string]::IsNullOrWhiteSpace($path) -or $path -match '(^|[/\\])\.\.([/\\]|$)' -or [IO.Path]::IsPathRooted($path))
                        {
                            throw "GitHub archive tool '$($toolInfo.id)' has an unsafe '$field' path."
                        }
                    }

                    foreach ($relativePath in @($toolInfo.provider.validationFiles))
                    {
                        if ([string]::IsNullOrWhiteSpace([string]$relativePath) -or [IO.Path]::IsPathRooted([string]$relativePath) -or [string]$relativePath -match '(^|[/\\])\.\.([/\\]|$)')
                        {
                            throw "GitHub archive tool '$($toolInfo.id)' has an unsafe validation path '$relativePath'."
                        }
                    }

                    foreach ($asset in @($toolInfo.provider.assets.Values))
                    {
                        if (-not $asset.Contains('validationFiles'))
                        {
                            continue
                        }

                        foreach ($relativePath in @($asset.validationFiles))
                        {
                            if ([string]::IsNullOrWhiteSpace([string]$relativePath) -or [IO.Path]::IsPathRooted([string]$relativePath) -or [string]$relativePath -match '(^|[/\\])\.\.([/\\]|$)')
                            {
                                throw "GitHub archive tool '$($toolInfo.id)' has an unsafe asset validation path '$relativePath'."
                            }
                        }
                    }
                }
            }
            'archive'
            {
                foreach ($field in @('baseUrl', 'managedDirectory', 'binaryDirectory', 'compiler', 'cmakeToolchain', 'assets'))
                {
                    if (-not $toolInfo.provider.Contains($field))
                    {
                        throw "Archive tool '$($toolInfo.id)' must declare '$field'."
                    }
                }

                foreach ($field in @('managedDirectory', 'binaryDirectory'))
                {
                    $path = [string]$toolInfo.provider[$field]

                    if ([string]::IsNullOrWhiteSpace($path) -or $path -match '(^|[/\\])\.\.([/\\]|$)' -or [IO.Path]::IsPathRooted($path))
                    {
                        throw "Archive tool '$($toolInfo.id)' has an unsafe '$field' path."
                    }
                }

                if ([string]$toolInfo.provider.compiler -notmatch '^[A-Za-z0-9._+-]+$')
                {
                    throw "Archive tool '$($toolInfo.id)' has an invalid compiler command."
                }

                if ($toolInfo.versionPolicy -ne 'exact')
                {
                    throw "Archive tool '$($toolInfo.id)' must pin an exact version."
                }

                foreach ($assetKey in @($toolInfo.provider.assets.Keys))
                {
                    $asset = $toolInfo.provider.assets[$assetKey]

                    if (-not $asset.Contains('archive') -or
                        $asset.archive -notmatch '(?i)\.(zip|tar\.gz|tar\.xz)$' -or
                        $asset.sha256 -notmatch '^[0-9a-f]{64}$' -or
                        -not $asset.Contains('validationFiles'))
                    {
                        throw "Archive tool '$($toolInfo.id)' has invalid verified asset metadata for '$assetKey'."
                    }
                }

                if ($toolInfo.provider.Contains('latestIndex'))
                {
                    $latestIndex = $toolInfo.provider.latestIndex
                    $indexUri = $null
                    $baseUri = $null

                    if (-not [uri]::TryCreate([string]$latestIndex.url, [UriKind]::Absolute, [ref]$indexUri) -or
                        -not [uri]::TryCreate([string]$toolInfo.provider.baseUrl, [UriKind]::Absolute, [ref]$baseUri) -or
                        $indexUri.Scheme -ne 'https' -or
                        $indexUri.Host -ne $baseUri.Host)
                    {
                        throw "Archive tool '$($toolInfo.id)' must use an HTTPS version index on its download host."
                    }

                    foreach ($assetKey in @($toolInfo.provider.assets.Keys))
                    {
                        if (-not $latestIndex.assetKeys.ContainsKey([string]$assetKey) -or
                            [string]::IsNullOrWhiteSpace([string]$latestIndex.assetKeys[$assetKey]))
                        {
                            throw "Archive tool '$($toolInfo.id)' has no latest-index key for '$assetKey'."
                        }
                    }
                }
            }
            'winget'
            {
                if (-not $toolInfo.provider.Contains('package'))
                {
                    throw "WinGet tool '$($toolInfo.id)' must declare its package."
                }
            }
            'external'
            {
                if ($toolInfo.provider.Contains('latestVersion') -and $toolInfo.provider.Contains('latestQuery'))
                {
                    throw "External tool '$($toolInfo.id)' must declare one latest-version query method."
                }

                if ($toolInfo.provider.Contains('latestVersion'))
                {
                    $latestVersion = $toolInfo.provider.latestVersion
                    $latestUri = $null

                    if (-not [uri]::TryCreate([string]$latestVersion.url, [UriKind]::Absolute, [ref]$latestUri) -or
                        $latestUri.Scheme -ne 'https')
                    {
                        throw "External tool '$($toolInfo.id)' latest-version URL must use HTTPS."
                    }
                    $pattern = [string]$latestVersion.pattern

                    try
                    {
                        $regex = [regex]::new($pattern)
                    }

                    catch
                    {
                        throw "External tool '$($toolInfo.id)' has an invalid latest-version pattern: $($_.Exception.Message)"
                    }

                    if ($regex.GetGroupNumbers().Count -lt 2)
                    {
                        throw "External tool '$($toolInfo.id)' latest-version pattern must capture the version."
                    }
                }

                if ($toolInfo.provider.Contains('latestQuery') -and
                    ([string]$toolInfo.provider.latestQuery.kind -ne 'msys2PackageUpdates' -or
                        [string]$toolInfo.provider.hostPlatform -ne 'windows'))
                {
                    throw "External tool '$($toolInfo.id)' has an unsupported latest-query declaration."
                }
            }
        }

        # Validate every live reference against the repository before accepting
        # the registry, because update planning later relies on these paths.
        foreach ($reference in @($toolInfo.references))
        {
            if ($reference -isnot [hashtable] -or -not $reference.Contains('path') -or -not $reference.Contains('kind'))
            {
                throw "Tool '$($toolInfo.id)' has malformed reference metadata."
            }

            $referencePath = Resolve-GameWipRepositoryPath -Path ([string]$reference.path)

            if (-not (Test-GameWipPathWithinRoot -Path $referencePath -Root $RepositoryRoot))
            {
                throw "Tool '$($toolInfo.id)' reference path '$($reference.path)' resolves outside the repository."
            }

            if (-not (Test-Path -LiteralPath $referencePath -PathType Leaf))
            {
                throw "Tool '$($toolInfo.id)' references missing path '$($reference.path)'."
            }

            $expectedCount = if ($reference.Contains('expectedCount'))
            {
                [int]$reference.expectedCount
            }
            else
            {
                1
            }
            # Each reference kind has a different comparison contract; reject
            # unsupported fields here instead of making mutation code guess.
            switch ([string]$reference.kind)
            {
                'text'
                {
                    if (-not $reference.Contains('pattern'))
                    {
                        throw "Tool '$($toolInfo.id)' text reference '$($reference.path)' must declare a literal pattern."
                    }

                    $tokenCount = [regex]::Matches([string]$reference.pattern, [regex]::Escape('{version}')).Count

                    if ($tokenCount -ne 1)
                    {
                        throw "Tool '$($toolInfo.id)' text reference '$($reference.path)' pattern must contain exactly one literal {version} token."
                    }
                }
                'releaseAsset'
                {
                    if ($toolInfo.provider.kind -ne 'githubRelease' -or
                        -not $toolInfo.provider.assets.ContainsKey([string]$reference.assetKey) -or
                        [string]$reference.assetField -notin @('archive', 'sha256'))
                    {
                        throw "Tool '$($toolInfo.id)' release-asset reference '$($reference.path)' does not match a declared GitHub asset."
                    }

                    $tokenCount = [regex]::Matches([string]$reference.pattern, [regex]::Escape('{value}')).Count

                    if ($tokenCount -ne 1)
                    {
                        throw "Tool '$($toolInfo.id)' release-asset reference '$($reference.path)' pattern must contain exactly one literal {value} token."
                    }
                }
                'cmakeMinimum'
                {
                    if ($reference.Contains('pattern'))
                    {
                        throw "Tool '$($toolInfo.id)' CMake minimum reference '$($reference.path)' must not declare pattern."
                    }
                }
                'path'
                {
                    if ($reference.Contains('pattern') -or $reference.Contains('expectedCount'))
                    {
                        throw "Tool '$($toolInfo.id)' informational path reference '$($reference.path)' must not declare pattern or expectedCount."
                    }
                    continue
                }
                default
                {
                    throw "Tool '$($toolInfo.id)' uses unknown reference kind '$($reference.kind)'."
                }
            }

            if ($expectedCount -lt 1)
            {
                throw "Tool '$($toolInfo.id)' live reference '$($reference.path)' expectedCount must be at least 1."
            }

            $referenceKey = @(
                [string]$reference.kind,
                $referencePath,
                $(if ($reference.Contains('pattern'))
                    {
                        [string]$reference.pattern
                    }
                    else
                    {
                        ''
                    }),
                [string]$expectedCount
            ) -join "`n"

            if ($liveReferences.ContainsKey($referenceKey))
            {
                throw "Tool '$($toolInfo.id)' duplicates live reference '$($reference.path)'."
            }

            $liveReferences[$referenceKey] = $true
        }
    }
}
