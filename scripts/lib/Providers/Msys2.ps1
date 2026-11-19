# MSYS2 tool provider support.

# ------------------------------------------------------------
# MSYS2 package operations
# ------------------------------------------------------------

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

    $archiveName = 'msys2-base-x86_64-latest.sfx.exe'
    $archiveUri = "https://repo.msys2.org/distrib/x86_64/$archiveName"
    $cacheRoot = Join-Path (Get-GameWipStorageAreaPath -Area cache) 'msys2'
    $archivePath = Join-Path $cacheRoot $archiveName
    $checksumUri = "$archiveUri.sha256"
    $checksum = Invoke-GameWipHttpRead -Uri $checksumUri -MaxAttempts 3 -TimeoutSeconds 30

    if ($checksum.State -ne 'resolved')
    {
        throw "Could not retrieve the MSYS2 archive checksum: $($checksum.Reason)"
    }
    $expectedMatch = [regex]::Match([string]$checksum.Value.Content, '(?i)\b[0-9a-f]{64}\b')

    if (-not $expectedMatch.Success)
    {
        throw 'The MSYS2 archive checksum response did not contain a SHA-256 digest.'
    }
    $expectedHash = $expectedMatch.Value.ToLowerInvariant()
    $archiveMatches = Test-Path -LiteralPath $archivePath -PathType Leaf

    if ($archiveMatches)
    {
        $archiveMatches = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant() -eq $expectedHash
    }

    if (-not $archiveMatches)
    {
        $download = Invoke-GameWipDownload `
            -Uri $archiveUri `
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
