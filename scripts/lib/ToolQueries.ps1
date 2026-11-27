# Structured upstream tool-version queries.

# ------------------------------------------------------------
# Query results and provider requests
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function New-GameWipToolQueryResult
{
    param(
        [ValidateSet('resolved', 'unsupported', 'unavailable', 'invalidResponse', 'integrityUnavailable')][string]$State,
        [string]$Provider,
        [AllowNull()][string]$Version,
        [string]$Reason = '',
        [int]$Attempts = 0,
        [AllowNull()]$Metadata = $null
    )
    [pscustomobject]@{
        State = $State
        Provider = $Provider
        Version = $Version
        Reason = $Reason
        Attempts = $Attempts
        Metadata = $Metadata
    }
}

function Invoke-GameWipToolQueryProcess
{
    param([string]$FilePath, [string[]]$Arguments, [string]$Provider)
    $timeoutSeconds = if ($Provider -eq 'npm')
    {
        60
    }
    else
    {
        30
    }
    $result = Invoke-GameWipProcess -FilePath $FilePath -Arguments $Arguments -OutputMode LogOnly -TimeoutSeconds $timeoutSeconds

    if ($result.Cancelled)
    {
        throw [System.OperationCanceledException]::new('Tool query cancelled.')
    }

    if ($result.TimedOut)
    {
        return New-GameWipToolQueryResult -State unavailable -Provider $Provider -Version $null -Reason "query timed out after ${timeoutSeconds}s"
    }

    if ($result.ExitCode -ne 0)
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider $Provider `
            -Version $null `
            -Reason (($result.Stderr + $result.Stdout) -join ' ').Trim()
    }

    return [pscustomobject]@{ State = 'process'; Output = (($result.Stdout + $result.Stderr) -join "`n").Trim() }
}

function Get-GameWipGitHubReleaseQuery
{
    param([hashtable]$Tool)
    $uri = "https://api.github.com/repos/$($Tool.provider.repository)/releases/latest"
    $http = Invoke-GameWipHttpJson -Uri $uri -Headers @{ Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2026-03-10' }

    if ($http.State -ne 'resolved')
    {
        return New-GameWipToolQueryResult -State unavailable -Provider githubRelease -Version $null -Reason $http.Reason -Attempts $http.Attempts
    }

    $release = $http.Value
    $tag = [string]$release.tag_name

    if ([string]::IsNullOrWhiteSpace($tag))
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider githubRelease `
            -Version $null `
            -Reason 'latest release has no tag_name' `
            -Attempts $http.Attempts
    }

    $version = ConvertFrom-GameWipGitHubReleaseTag -Tag $tag
    $assets = @{}

    foreach ($key in @($Tool.provider.assets.Keys))
    {
        $expectedName = ([string]$Tool.provider.assets[$key].archive).Replace([string]$Tool.requiredVersion, $version)
        $asset = $release.assets | Where-Object { $_.name -eq $expectedName } | Select-Object -First 1

        if ($null -eq $asset)
        {
            continue
        }
        $digest = [regex]::Match([string]$asset.digest, '^sha256:([0-9a-f]{64})$')

        if ($digest.Success)
        {
            $assets[$key] = @{ archive = [string]$asset.name; sha256 = $digest.Groups[1].Value }
        }
    }

    $metadata = [pscustomobject]@{ Version = $version; Tag = $tag; Assets = $assets }

    if ($assets.Count -lt @($Tool.provider.assets.Keys).Count)
    {
        return New-GameWipToolQueryResult `
            -State integrityUnavailable `
            -Provider githubRelease `
            -Version $version `
            -Reason 'one or more required release assets lack a matching SHA-256 digest' `
            -Attempts $http.Attempts `
            -Metadata $metadata
    }

    return New-GameWipToolQueryResult -State resolved -Provider githubRelease -Version $version -Attempts $http.Attempts -Metadata $metadata
}

function Get-GameWipArchiveIndexLatestQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if (-not $Tool.provider.Contains('latestIndex'))
    {
        return New-GameWipToolQueryResult `
            -State unsupported `
            -Provider archive `
            -Version $null `
            -Reason 'archive provider has no latest-version index'
    }

    $indexInfo = $Tool.provider.latestIndex
    $http = Invoke-GameWipHttpJson -Uri ([string]$indexInfo.url)

    if ($http.State -ne 'resolved')
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider archiveIndex `
            -Version $null `
            -Reason $http.Reason `
            -Attempts $http.Attempts
    }

    if ($null -eq $http.Value)
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider archiveIndex `
            -Version $null `
            -Reason 'version index was empty' `
            -Attempts $http.Attempts
    }

    # Select the newest stable semantic version; prerelease index entries are ignored.
    $releases = [System.Collections.Generic.List[object]]::new()

    foreach ($property in $http.Value.PSObject.Properties)
    {
        if ($property.Name -notmatch '^\d+\.\d+\.\d+$')
        {
            continue
        }

        $releases.Add([pscustomobject]@{
                Version = [version]$property.Name
                Entry = $property.Value
            }) | Out-Null
    }

    if ($releases.Count -eq 0)
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider archiveIndex `
            -Version $null `
            -Reason 'version index contains no stable semantic versions' `
            -Attempts $http.Attempts
    }

    $release = $releases | Sort-Object -Property Version -Descending | Select-Object -First 1
    $version = [string]$release.Entry.version

    if ($version -ne [string]$release.Version)
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider archiveIndex `
            -Version $null `
            -Reason 'latest stable entry has a mismatched version field' `
            -Attempts $http.Attempts
    }

    # Accept only verified archives from the declared download origin.
    $indexUri = [uri][string]$indexInfo.url
    $baseUri = [uri][string]$Tool.provider.baseUrl
    $downloadPrefix = $baseUri.AbsolutePath.TrimEnd('/') + "/$version/"
    $assets = @{}

    foreach ($assetKey in @($Tool.provider.assets.Keys))
    {
        $indexKey = [string]$indexInfo.assetKeys[$assetKey]
        $releaseAssetProperty = $release.Entry.PSObject.Properties[$indexKey]
        if ($null -eq $releaseAssetProperty)
        {
            return New-GameWipToolQueryResult `
                -State integrityUnavailable `
                -Provider archiveIndex `
                -Version $version `
                -Reason "latest index has no archive entry for '$assetKey'" `
                -Attempts $http.Attempts
        }

        $releaseAsset = $releaseAssetProperty.Value
        $tarball = [string]$releaseAsset.tarball
        $sha256 = [string]$releaseAsset.shasum
        $assetUri = $null
        $validUri = [uri]::TryCreate($tarball, [UriKind]::Absolute, [ref]$assetUri)

        if (-not $validUri -or
            $assetUri.Scheme -ne 'https' -or
            $assetUri.Host -ne $indexUri.Host -or
            -not $assetUri.AbsolutePath.StartsWith($downloadPrefix, [StringComparison]::Ordinal) -or
            $sha256 -notmatch '^[0-9a-f]{64}$')
        {
            return New-GameWipToolQueryResult `
                -State integrityUnavailable `
                -Provider archiveIndex `
                -Version $version `
                -Reason "latest index lacks a trusted archive and SHA-256 for '$assetKey'" `
                -Attempts $http.Attempts
        }

        $archive = [uri]::UnescapeDataString([IO.Path]::GetFileName($assetUri.AbsolutePath))

        if ([string]::IsNullOrWhiteSpace($archive) -or $archive -notmatch '(?i)\.(zip|tar\.gz|tar\.xz)$')
        {
            return New-GameWipToolQueryResult `
                -State invalidResponse `
                -Provider archiveIndex `
                -Version $version `
                -Reason "latest index has an invalid archive name for '$assetKey'" `
                -Attempts $http.Attempts
        }

        $assets[$assetKey] = @{ archive = $archive; sha256 = $sha256 }
    }

    $metadata = [pscustomobject]@{ Version = $version; Assets = $assets }
    return New-GameWipToolQueryResult `
        -State resolved `
        -Provider archiveIndex `
        -Version $version `
        -Attempts $http.Attempts `
        -Metadata $metadata
}

function Get-GameWipExternalLatestVersionQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $latestVersion = $Tool.provider.latestVersion
    $http = Invoke-GameWipHttpRead -Uri ([string]$latestVersion.url)

    if ($http.State -ne 'resolved')
    {
        return New-GameWipToolQueryResult `
            -State unavailable `
            -Provider external `
            -Version $null `
            -Reason $http.Reason `
            -Attempts $http.Attempts
    }

    $match = [regex]::Match([string]$http.Value.Content, [string]$latestVersion.pattern)

    if (-not $match.Success -or $match.Groups.Count -lt 2)
    {
        return New-GameWipToolQueryResult `
            -State invalidResponse `
            -Provider external `
            -Version $null `
            -Reason 'upstream version page did not match its declared pattern' `
            -Attempts $http.Attempts
    }

    return New-GameWipToolQueryResult `
        -State resolved `
        -Provider external `
        -Version $match.Groups[1].Value `
        -Attempts $http.Attempts
}

function Get-GameWipToolLatestQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    if (-not $Tool.capabilities.checkLatest)
    {
        return New-GameWipToolQueryResult `
            -State unsupported `
            -Provider ([string]$Tool.provider.kind) `
            -Version $null `
            -Reason 'registry marks latest-version queries unsupported'
    }

    switch ([string]$Tool.provider.kind)
    {
        'githubRelease'
        {
            return Get-GameWipGitHubReleaseQuery -Tool $Tool
        }

        'archive'
        {
            return Get-GameWipArchiveIndexLatestQuery -Tool $Tool
        }

        'python'
        {
            $http = Invoke-GameWipHttpJson -Uri "https://pypi.org/pypi/$($Tool.provider.package)/json"

            if ($http.State -ne 'resolved')
            {
                return New-GameWipToolQueryResult -State unavailable -Provider python -Version $null -Reason $http.Reason -Attempts $http.Attempts
            }
            $version = [string]$http.Value.info.version

            if ([string]::IsNullOrWhiteSpace($version))
            {
                return New-GameWipToolQueryResult `
                    -State invalidResponse `
                    -Provider python `
                    -Version $null `
                    -Reason 'PyPI response did not contain info.version' `
                    -Attempts $http.Attempts
            }
            return New-GameWipToolQueryResult -State resolved -Provider python -Version $version -Attempts $http.Attempts
        }

        'npm'
        {
            $npm = Resolve-GameWipToolCommand -Command 'npm'

            if ($null -eq $npm)
            {
                return New-GameWipToolQueryResult -State unavailable -Provider npm -Version $null -Reason 'npm command is unavailable'
            }
            $query = Invoke-GameWipToolQueryProcess -FilePath $npm -Arguments @('view', [string]$Tool.provider.package, 'version') -Provider npm

            if ($query.State -ne 'process')
            {
                return $query
            }

            if ([string]::IsNullOrWhiteSpace($query.Output))
            {
                return New-GameWipToolQueryResult -State invalidResponse -Provider npm -Version $null -Reason 'npm returned an empty version'
            }
            return New-GameWipToolQueryResult -State resolved -Provider npm -Version $query.Output
        }

        # Package-manager queries are implemented by the active host platform.
        'msys2'
        {
            if (-not (Test-GameWipWindowsHost))
            {
                return New-GameWipToolQueryResult `
                    -State unsupported `
                    -Provider msys2 `
                    -Version $null `
                    -Reason 'MSYS2 package queries are available only on Windows'
            }

            return Get-GameWipMsys2ToolLatestQuery -Tool $Tool
        }

        'winget'
        {
            if (-not (Test-GameWipWindowsHost))
            {
                return New-GameWipToolQueryResult `
                    -State unsupported `
                    -Provider winget `
                    -Version $null `
                    -Reason 'WinGet queries are available only on Windows'
            }

            return Get-GameWipWingetToolLatestQuery -Tool $Tool
        }

        'powershellGallery'
        {
            try
            {
                $version = Get-GameWipPowerShellGalleryToolLatestVersion -Tool $Tool

                if ([string]::IsNullOrWhiteSpace($version))
                {
                    throw "PowerShell Gallery returned no versions for '$($Tool.provider.package)'."
                }
                return New-GameWipToolQueryResult -State resolved -Provider powershellGallery -Version $version -Attempts 1
            }

            catch
            {
                return New-GameWipToolQueryResult `
                    -State unavailable `
                    -Provider powershellGallery `
                    -Version $null `
                    -Reason $_.Exception.Message `
                    -Attempts 1
            }
        }
        'external'
        {
            if ($Tool.provider.Contains('latestVersion'))
            {
                return Get-GameWipExternalLatestVersionQuery -Tool $Tool
            }

            if ($Tool.provider.Contains('latestQuery'))
            {
                switch ([string]$Tool.provider.latestQuery.kind)
                {
                    'msys2PackageUpdates'
                    {
                        if (-not (Test-GameWipWindowsHost))
                        {
                            return New-GameWipToolQueryResult `
                                -State unsupported `
                                -Provider msys2 `
                                -Version $null `
                                -Reason 'MSYS2 updates are available only on Windows'
                        }

                        return Get-GameWipMsys2SystemUpdatesQuery -Tool $Tool
                    }
                }
            }

            return New-GameWipToolQueryResult -State unsupported -Provider external -Version $null -Reason 'external provider has no updater'
        }
        default
        {
            return New-GameWipToolQueryResult `
                -State unsupported `
                -Provider ([string]$Tool.provider.kind) `
                -Version $null `
                -Reason 'provider query is not implemented'
        }
    }
}

function Get-GameWipNpmPackageLatestQuery
{
    param([Parameter(Mandatory = $true)][string]$Package)
    $tool = @{ provider = @{ kind = 'npm'; package = $Package }; capabilities = @{ checkLatest = $true } }
    return Get-GameWipToolLatestQuery -Tool $tool
}
