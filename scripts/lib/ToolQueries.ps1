# GameWIP structured upstream tool-version query behavior.

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
    [pscustomobject]@{ State = $State; Provider = $Provider; Version = $Version; Reason = $Reason; Attempts = $Attempts; Metadata = $Metadata }
}

function Invoke-GameWipToolQueryProcess
{
    param([string]$FilePath, [string[]]$Arguments, [string]$Provider)
    $result = Invoke-GameWipProcess -FilePath $FilePath -Arguments $Arguments -OutputMode LogOnly -TimeoutSeconds 30
    if ($result.Cancelled)
    {
        throw [System.OperationCanceledException]::new('Tool query cancelled.')
    }
    if ($result.TimedOut)
    {
        return New-GameWipToolQueryResult -State unavailable -Provider $Provider -Version $null -Reason 'query timed out'
    }
    if ($result.ExitCode -ne 0)
    {
        return New-GameWipToolQueryResult -State unavailable -Provider $Provider -Version $null -Reason (($result.Stderr + $result.Stdout) -join ' ').Trim()
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
        return New-GameWipToolQueryResult -State invalidResponse -Provider githubRelease -Version $null -Reason 'latest release has no tag_name' -Attempts $http.Attempts
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
        return New-GameWipToolQueryResult -State integrityUnavailable -Provider githubRelease -Version $version -Reason 'one or more required release assets lack a matching SHA-256 digest' -Attempts $http.Attempts -Metadata $metadata
    }
    return New-GameWipToolQueryResult -State resolved -Provider githubRelease -Version $version -Attempts $http.Attempts -Metadata $metadata
}

function Get-GameWipToolLatestQuery
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)
    if (-not $Tool.capabilities.checkLatest)
    {
        return New-GameWipToolQueryResult -State unsupported -Provider ([string]$Tool.provider.kind) -Version $null -Reason 'registry marks latest-version queries unsupported'
    }

    switch ([string]$Tool.provider.kind)
    {
        'githubRelease'
        {
            return Get-GameWipGitHubReleaseQuery -Tool $Tool
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
                return New-GameWipToolQueryResult -State invalidResponse -Provider python -Version $null -Reason 'PyPI response did not contain info.version' -Attempts $http.Attempts
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
        'msys2'
        {
            $bash = Join-Path ([string]$ProjectConfig.managedEnvironment.msys2Root) 'usr\bin\bash.exe'
            if (-not (Test-Path -LiteralPath $bash))
            {
                return New-GameWipToolQueryResult -State unavailable -Provider msys2 -Version $null -Reason 'MSYS2 bash is unavailable'
            }
            $query = Invoke-GameWipToolQueryProcess -FilePath $bash -Arguments @('-lc', "pacman -Si '$($Tool.provider.package)' 2>/dev/null") -Provider msys2
            if ($query.State -ne 'process')
            {
                return $query
            }
            $match = [regex]::Match($query.Output, '(?m)^Version\s*:\s*([^\s-]+)')
            if (-not $match.Success)
            {
                return New-GameWipToolQueryResult -State invalidResponse -Provider msys2 -Version $null -Reason 'pacman output did not contain a version'
            }
            return New-GameWipToolQueryResult -State resolved -Provider msys2 -Version $match.Groups[1].Value
        }
        'winget'
        {
            $winget = Resolve-GameWipToolCommand -Command 'winget'
            if ($null -eq $winget)
            {
                return New-GameWipToolQueryResult -State unavailable -Provider winget -Version $null -Reason 'winget is unavailable'
            }
            $query = Invoke-GameWipToolQueryProcess -FilePath $winget -Arguments @('show', '--id', [string]$Tool.provider.package, '--exact', '--accept-source-agreements') -Provider winget
            if ($query.State -ne 'process')
            {
                return $query
            }
            $match = [regex]::Match($query.Output, '(?m)^Version:\s*(\S+)')
            if (-not $match.Success)
            {
                return New-GameWipToolQueryResult -State invalidResponse -Provider winget -Version $null -Reason 'winget output did not contain Version'
            }
            return New-GameWipToolQueryResult -State resolved -Provider winget -Version $match.Groups[1].Value
        }
        'powershellGallery'
        {
            try
            {
                $module = Find-Module -Name $Tool.provider.package -Repository PSGallery -ErrorAction Stop
                return New-GameWipToolQueryResult -State resolved -Provider powershellGallery -Version $module.Version.ToString() -Attempts 1
            }
            catch
            {
                return New-GameWipToolQueryResult -State unavailable -Provider powershellGallery -Version $null -Reason $_.Exception.Message -Attempts 1
            }
        }
        'gitSubmodule'
        {
            return New-GameWipToolQueryResult -State unsupported -Provider gitSubmodule -Version $null -Reason 'updated through repository dependency state'
        }
        'external'
        {
            return New-GameWipToolQueryResult -State unsupported -Provider external -Version $null -Reason 'external provider has no updater'
        }
        default
        {
            return New-GameWipToolQueryResult -State unsupported -Provider ([string]$Tool.provider.kind) -Version $null -Reason 'provider query is not implemented'
        }
    }
}

function Get-GameWipNpmPackageLatestQuery
{
    param([Parameter(Mandatory = $true)][string]$Package)
    $tool = @{ provider = @{ kind = 'npm'; package = $Package }; capabilities = @{ checkLatest = $true } }
    return Get-GameWipToolLatestQuery -Tool $tool
}
