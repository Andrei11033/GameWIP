# MSYS2 tool provider support.

# ------------------------------------------------------------
# MSYS2 package operations
# ------------------------------------------------------------

function Get-GameWipMsys2BaseArchive
{
    # GitHub's /releases/latest endpoint points to the nightly channel.
    $releaseQuery = Invoke-GameWipHttpJson `
        -Uri 'https://api.github.com/repos/msys2/msys2-installer/releases?per_page=100' `
        -Headers @{ Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2026-03-10' }

    if ($releaseQuery.State -ne 'resolved')
    {
        throw "Could not retrieve MSYS2 release metadata: $($releaseQuery.Reason)"
    }

    # Select the newest dated stable release, excluding the nightly channel.
    $release = @(
        $releaseQuery.Value |
            Where-Object {
                -not $_.draft -and
                -not $_.prerelease -and
                [string]$_.tag_name -match '^\d{4}-\d{2}-\d{2}$'
            } |
            Sort-Object { [string]$_.tag_name } -Descending |
            Select-Object -First 1
    ) | Select-Object -First 1

    if ($null -eq $release)
    {
        throw 'The MSYS2 release list did not contain a dated stable release.'
    }

    # Locate the archive and checksum assets for the selected release.
    $releaseDate = ([string]$release.tag_name).Replace('-', '')
    $archiveName = "msys2-base-x86_64-$releaseDate.sfx.exe"
    $archiveAsset = $release.assets | Where-Object { $_.name -eq $archiveName } | Select-Object -First 1
    $checksumAsset = $release.assets | Where-Object { $_.name -eq "$archiveName.sha256" } | Select-Object -First 1

    if ($null -eq $archiveAsset -or $null -eq $checksumAsset)
    {
        throw "MSYS2 release '$($release.tag_name)' is missing its base archive or SHA-256 asset."
    }

    # Read the checksum published alongside the archive.
    $checksum = Invoke-GameWipHttpRead -Uri ([string]$checksumAsset.browser_download_url) -MaxAttempts 3 -TimeoutSeconds 30

    if ($checksum.State -ne 'resolved')
    {
        throw "Could not retrieve the MSYS2 archive checksum: $($checksum.Reason)"
    }

    # Decode text assets returned as bytes by Windows PowerShell 5.1.
    $checksumContent = $checksum.Value.Content

    if ($checksumContent -is [byte[]])
    {
        $checksumContent = [Text.Encoding]::UTF8.GetString($checksumContent)
    }

    $expectedMatch = [regex]::Match([string]$checksumContent, '(?i)\b[0-9a-f]{64}\b')

    if (-not $expectedMatch.Success)
    {
        throw 'The MSYS2 archive checksum response did not contain a SHA-256 digest.'
    }

    return [pscustomobject]@{
        Name = $archiveName
        Uri = [string]$archiveAsset.browser_download_url
        Sha256 = $expectedMatch.Value.ToLowerInvariant()
    }
}

function Install-GameWipMsys2BaseArchive
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot)

    if (-not (Test-GameWipWindowsHost))
    {
        throw 'The MSYS2 base archive is available only on Windows.'
    }

    $bash = Join-Path $MsysRoot 'usr/bin/bash.exe'

    if (Test-Path -LiteralPath $bash -PathType Leaf)
    {
        return
    }

    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))

    if (-not (Test-GameWipPathWithinRoot -Path $MsysRoot -Root $buildRoot))
    {
        throw "Refusing to install a new MSYS2 root outside build/: '$MsysRoot'. Select an existing installation or use a root beneath '$buildRoot'."
    }

    # Resolve the current stable archive before creating managed files.
    $archive = Get-GameWipMsys2BaseArchive
    $cacheRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'msys2'
    $archivePath = Join-Path $cacheRoot $archive.Name
    $expectedHash = $archive.Sha256
    $archiveMatches = Test-Path -LiteralPath $archivePath -PathType Leaf

    if ($archiveMatches)
    {
        $archiveMatches = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedHash
    }

    if (-not $archiveMatches)
    {
        $download = Invoke-GameWipDownload `
            -Uri $archive.Uri `
            -OutFile $archivePath `
            -TimeoutSeconds 300 `
            -Label 'MSYS2 base archive'

        if ($download.State -ne 'resolved')
        {
            throw "Could not download the MSYS2 base archive: $($download.Reason)"
        }
    }

    $actualHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()

    if ($actualHash -ne $expectedHash)
    {
        throw 'The downloaded MSYS2 base archive does not match its published SHA-256 checksum.'
    }

    $rootCreated = -not (Test-Path -LiteralPath $MsysRoot -PathType Container)

    if ($rootCreated)
    {
        Set-GameWipMutationStarted
        New-Item -ItemType Directory -Force -Path $MsysRoot | Out-Null
    }
    $existingEntries = @(Get-ChildItem -LiteralPath $MsysRoot -Force -ErrorAction SilentlyContinue)
    $ownershipPath = Join-Path $MsysRoot '.gamewip-managed.json'
    $ownership = Read-GameWipOwnershipMarker -Path $ownershipPath -Resource 'msys2'

    if ($existingEntries.Count -ne 0 -and $ownership.Status -ne 'valid')
    {
        throw "Refusing to install MSYS2 into a non-empty unowned directory: '$MsysRoot'."
    }

    if ($ownership.Status -ne 'valid')
    {
        $origin = if ($rootCreated) { 'created' } else { 'claimedEmpty' }
        $marker = New-GameWipOwnershipMarker -Resource 'msys2' -Origin $origin -Payload ([ordered]@{ installedBySetup = $rootCreated })
        Write-GameWipJsonAtomic -Path $ownershipPath -Value $marker
    }

    try
    {
        Invoke-GameWipProviderNative `
            -Name msys2-bootstrap `
            -FilePath $archivePath `
            -Arguments @('-y', "-o$MsysRoot") | Out-Null

        if (-not (Test-Path -LiteralPath $bash -PathType Leaf))
        {
            throw "MSYS2 extraction completed without creating '$bash'."
        }
        Invoke-GameWipProviderNative -Name msys2-initialize -FilePath $bash -Arguments @('-lc', 'true') | Out-Null
        Add-GameWipOperationChange -Message "Installed the MSYS2 base environment under $MsysRoot"
    }

    catch
    {
        if ($rootCreated -and (Test-Path -LiteralPath $MsysRoot -PathType Container))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $MsysRoot -OwnedRoot $buildRoot -RequireMarker -MarkerName '.gamewip-managed.json' -SuppressMutationTracking
        }
        throw
    }
}

function Get-GameWipMsys2ToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool

    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }

    return $null
}

function Get-GameWipMsys2InstalledPackageNames
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot)

    $pacman = Join-Path $MsysRoot 'usr\bin\pacman.exe'

    if (-not (Test-Path -LiteralPath $pacman -PathType Leaf))
    {
        return [pscustomobject]@{ State = 'missing'; Packages = @(); Reason = "MSYS2 pacman was not found at '$pacman'." }
    }

    $result = Invoke-GameWipProcess -FilePath $pacman -Arguments @('-Qq') -OutputMode LogOnly -TimeoutSeconds 60

    if ($result.ExitCode -ne 0)
    {
        return [pscustomobject]@{ State = 'unknown'; Packages = @(); Reason = 'MSYS2 pacman could not enumerate installed packages.' }
    }

    return [pscustomobject]@{
        State = 'ready'
        Packages = @($result.Stdout | ForEach-Object { ([string]$_).Trim() } | Where-Object { $_ })
        Reason = $null
    }
}

function Get-GameWipMsys2ToolDependencyStatus
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool)

    $installed = Get-GameWipMsys2InstalledPackageNames -MsysRoot (Get-GameWipMsys2Root)

    foreach ($dependency in @($Tool.provider.dependencies))
    {
        $package = [string]$dependency.package
        $state = if ($installed.State -ne 'ready')
        {
            $installed.State
        }
        elseif ($installed.Packages -contains $package)
        {
            'compatible'
        }
        else
        {
            'missing'
        }

        $reason = if ($state -eq 'missing')
        {
            "MSYS2 package '$package' is not installed."
        }
        elseif ($installed.Reason)
        {
            [string]$installed.Reason
        }
        else
        {
            $null
        }

        [pscustomobject]@{
            Package = $package
            RequiredVersion = $null
            InstalledVersion = $null
            State = $state
            Location = Get-GameWipMsys2Root
            Reason = $reason
        }
    }
}

function Install-GameWipMsys2Tool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    $null = $Version
    $bash = Join-Path (Get-GameWipMsys2Root) 'usr\bin\bash.exe'

    if (-not (Test-Path -LiteralPath $bash))
    {
        throw 'MSYS2 is not installed; run setup.bat repair.'
    }

    $dependencies = if ($Tool.provider.Contains('dependencies'))
    {
        @($Tool.provider.dependencies)
    }
    else
    {
        @()
    }
    $packages = @([string]$Tool.provider.package) + @($dependencies | ForEach-Object { [string]$_.package })
    $packageText = @($packages | Sort-Object -Unique | ForEach-Object { "'$_'" }) -join ' '
    Invoke-GameWipProviderNative -Name "pacman-install-$($Tool.id)" -FilePath $bash -Arguments @('-lc', "pacman --noconfirm --needed -S $packageText") | Out-Null
}
