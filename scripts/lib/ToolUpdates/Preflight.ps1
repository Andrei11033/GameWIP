# ------------------------------------------------------------
# Pinned tool preflight
# ------------------------------------------------------------

function Assert-GameWipPinnedToolAvailable
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if ($Tool.versionPolicy -ne 'exact' -or -not $Tool.Contains('requiredVersion'))
    {
        return
    }

    $version = [string]$Tool.requiredVersion

    # Verify that each provider can supply the exact pinned version.
    switch ([string]$Tool.provider.kind)
    {
        'python'
        {
            if ((Test-GameWipWindowsHost) -and $Tool.provider.Contains('windowsMsys2'))
            {
                $nativeProvider = $Tool.provider.windowsMsys2
                $queryTool = @{
                    provider = @{
                        kind = 'msys2'
                        environment = [string]$nativeProvider.environment
                        package = [string]$nativeProvider.package
                    }
                    capabilities = @{ checkLatest = $true }
                }
                $query = Get-GameWipToolLatestQuery -Tool $queryTool

                if ($query.State -ne 'resolved' -or [string]$query.Version -ne $version)
                {
                    throw "Pinned MSYS2 package '$($nativeProvider.package) $version' is unavailable."
                }
                break
            }

            $result = Invoke-GameWipHttpJson `
                -Uri "https://pypi.org/pypi/$($Tool.provider.package)/$version/json" `
                -MaxAttempts 3 `
                -TimeoutSeconds 20

            if ($result.State -ne 'resolved')
            {
                throw "Pinned Python package '$($Tool.provider.package)==$version' is unavailable: $($result.Reason)"
            }
        }
        'npm'
        {
            $npm = Resolve-GameWipToolCommand -Command npm

            if ($null -eq $npm)
            {
                throw "npm is unavailable while preflighting '$($Tool.id)'."
            }
            $query = Invoke-GameWipToolQueryProcess `
                -FilePath $npm `
                -Arguments @('view', "$($Tool.provider.package)@$version", 'version') `
                -Provider npm

            if ($query.State -ne 'process')
            {
                throw "Pinned npm package '$($Tool.provider.package)@$version' could not be verified: $($query.State) ($($query.Reason))."
            }

            if ($query.Output.Trim() -ne $version)
            {
                throw "Pinned npm package '$($Tool.provider.package)@$version' returned version '$($query.Output.Trim())'."
            }
            $dependencies = if ($Tool.provider.Contains('dependencies'))
            {
                @($Tool.provider.dependencies)
            }
            else
            {
                @()
            }

            foreach ($dependency in $dependencies)
            {
                $depQuery = Invoke-GameWipToolQueryProcess `
                    -FilePath $npm `
                    -Arguments @('view', "$($dependency.package)@$($dependency.version)", 'version') `
                    -Provider npm

                if ($depQuery.State -ne 'process')
                {
                    throw (
                        "Pinned npm dependency '$($dependency.package)@$($dependency.version)' " +
                        "could not be verified: $($depQuery.State) ($($depQuery.Reason))."
                    )
                }
            }
        }
        'powershellGallery'
        {
            try
            {
                $versions = @(Get-GameWipPowerShellGalleryVersions -Package ([string]$Tool.provider.package))

                if ($versions -notcontains $version)
                {
                    throw "Gallery metadata does not list version $version."
                }
            }

            catch
            {
                throw "Pinned PowerShell Gallery module '$($Tool.provider.package) $version' is unavailable: $($_.Exception.Message)"
            }
        }
        'githubRelease'
        {
            if (-not $Tool.provider.Contains('releaseTag') -or -not $Tool.provider.Contains('assets'))
            {
                throw "Pinned GitHub-release tool '$($Tool.id)' lacks verified release metadata."
            }
        }
        'archive'
        {
            $assetKey = Get-GameWipHostAssetKey

            if ($null -eq $assetKey -or -not $Tool.provider.assets.ContainsKey($assetKey))
            {
                throw "Pinned archive tool '$($Tool.id)' has no asset for host '$assetKey'."
            }

            $asset = $Tool.provider.assets[$assetKey]

            if ($asset.sha256 -notmatch '^[0-9a-f]{64}$')
            {
                throw "Pinned archive tool '$($Tool.id)' has no valid SHA-256 for '$assetKey'."
            }
        }
    }
}
