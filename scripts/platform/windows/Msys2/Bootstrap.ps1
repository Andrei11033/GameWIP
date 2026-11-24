# MSYS2 archive discovery and installation.

# ------------------------------------------------------------
# Resolve and install the base archive
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
    $checksum = Invoke-GameWipHttpRead `
        -Uri ([string]$checksumAsset.browser_download_url) `
        -MaxAttempts 3 `
        -TimeoutSeconds 30

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

function Complete-GameWipMsys2BaseArchiveExtraction
{
    param([Parameter(Mandatory = $true)][string]$MsysRoot)

    $rootBash = Join-Path $MsysRoot 'usr/bin/bash.exe'
    $archiveRoot = Join-Path $MsysRoot 'msys64'
    $archiveBash = Join-Path $archiveRoot 'usr/bin/bash.exe'

    if ((Test-Path -LiteralPath $rootBash -PathType Leaf) -or
        -not (Test-Path -LiteralPath $archiveBash -PathType Leaf))
    {
        return
    }

    # The self-extracting archive includes an `msys64` directory.
    $entries = @(Get-ChildItem -LiteralPath $archiveRoot -Force)

    foreach ($entry in $entries)
    {
        $destination = Join-Path $MsysRoot $entry.Name

        if (Test-Path -LiteralPath $destination)
        {
            throw "Cannot place the extracted MSYS2 root because '$destination' already exists."
        }
    }

    foreach ($entry in $entries)
    {
        Move-Item -LiteralPath $entry.FullName -Destination $MsysRoot
    }

    [IO.Directory]::Delete($archiveRoot)
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

    # Require a build-owned root for a new installation.
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $RepositoryRoot 'build'))

    if (-not (Test-GameWipPathWithinRoot -Path $MsysRoot -Root $buildRoot))
    {
        throw (
            "Refusing to install a new MSYS2 root outside build/: '{0}'. Select an existing installation or use a root beneath '{1}'." -f
            $MsysRoot,
            $buildRoot
        )
    }

    # Resolve and verify the current stable archive before installation.
    $archive = Get-GameWipMsys2BaseArchive
    $cacheRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'msys2'
    $archivePath = Join-Path $cacheRoot $archive.Name
    $expectedHash = $archive.Sha256
    $archiveMatches = Test-Path -LiteralPath $archivePath -PathType Leaf

    if ($archiveMatches)
    {
        $archiveMatches = (Get-GameWipFileSha256 -Path $archivePath) -eq $expectedHash
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

    $actualHash = Get-GameWipFileSha256 -Path $archivePath

    if ($actualHash -ne $expectedHash)
    {
        throw 'The downloaded MSYS2 base archive does not match its published SHA-256 checksum.'
    }

    # Establish ownership before extraction.
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
        $origin = if ($rootCreated)
        {
            'created'
        }
        else
        {
            'claimedEmpty'
        }
        $marker = New-GameWipOwnershipMarker -Resource 'msys2' -Origin $origin -Payload ([ordered]@{ installedBySetup = $rootCreated })
        Write-GameWipJsonAtomic -Path $ownershipPath -Value $marker
    }

    # Extract, initialize, and verify the managed environment.
    try
    {
        Set-GameWipMutationStarted
        Invoke-GameWipProviderNative `
            -Name msys2-bootstrap `
            -FilePath $archivePath `
            -Arguments @('-y', "-o$MsysRoot") | Out-Null

        Complete-GameWipMsys2BaseArchiveExtraction -MsysRoot $MsysRoot

        if (-not (Test-Path -LiteralPath $bash -PathType Leaf))
        {
            throw "MSYS2 extraction completed without creating '$bash'."
        }

        Invoke-GameWipProviderNative `
            -Name msys2-initialize `
            -FilePath $bash `
            -Arguments @('-lc', 'true') | Out-Null
        Add-GameWipOperationChange -Message "Installed the MSYS2 base environment under $MsysRoot"
    }

    catch
    {
        $installationFailure = $_

        # Remove a partial root only when this operation created it.
        if ($rootCreated -and (Test-Path -LiteralPath $MsysRoot -PathType Container))
        {
            try
            {
                Invoke-GameWipOwnedTreeRemoval `
                    -Path $MsysRoot `
                    -OwnedRoot $buildRoot `
                    -RequireMarker `
                    -MarkerName '.gamewip-managed.json' `
                    -SuppressMutationTracking
            }

            catch
            {
                Write-GameWipOperationEvent `
                    -Phase execute `
                    -Step msys2-bootstrap-cleanup `
                    -Severity warning `
                    -Message 'Could not remove the partial MSYS2 installation.' `
                    -Data @{ reason = $_.Exception.Message }
            }
        }

        throw $installationFailure
    }
}
