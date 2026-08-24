# GameWIP Storage helper behavior. Dot-sourced by scripts/GameWIP.ps1.

function Resolve-GameWipStoragePath
{
    param([Parameter(Mandatory = $true)][string]$RelativePath)
    return [IO.Path]::GetFullPath((Join-Path $RepositoryRoot $RelativePath))
}

function Initialize-GameWipStorage
{
    foreach ($relativePath in @($ProjectConfig.storage.cache, $ProjectConfig.storage.state, $ProjectConfig.storage.temp, $ProjectConfig.storage.runs))
    {
        New-Item -ItemType Directory -Force -Path (Resolve-GameWipStoragePath -RelativePath $relativePath) | Out-Null
    }
}

function Get-GameWipCanonicalPath
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $canonical = [IO.Path]::GetFullPath($Path)
    $root = [IO.Path]::GetPathRoot($canonical)
    if ($canonical -eq $root)
    {
        return $canonical
    }

    [char[]]$trimCharacters = @(
        [IO.Path]::DirectorySeparatorChar
        [IO.Path]::AltDirectorySeparatorChar
    ) | Select-Object -Unique
    return $canonical.TrimEnd($trimCharacters)
}

function Assert-GameWipSafeChildPath
{
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$OwnedRoot)

    $comparison = if (Test-GameWipWindowsHost)
    {
        [StringComparison]::OrdinalIgnoreCase
    }
    else
    {
        [StringComparison]::Ordinal
    }

    $canonicalPath = Get-GameWipCanonicalPath -Path $Path
    $canonicalRoot = Get-GameWipCanonicalPath -Path $OwnedRoot
    $driveRoot = Get-GameWipCanonicalPath -Path ([IO.Path]::GetPathRoot($canonicalPath))
    $canonicalRepositoryRoot = Get-GameWipCanonicalPath -Path $RepositoryRoot
    $profileRoot = if (-not [string]::IsNullOrWhiteSpace([string]$env:USERPROFILE))
    {
        Get-GameWipCanonicalPath -Path $env:USERPROFILE
    }
    else
    {
        $null
    }

    $isOwnedRoot = $canonicalPath.Equals($canonicalRoot, $comparison)
    $isDriveRoot = $canonicalPath.Equals($driveRoot, $comparison)
    $isRepositoryRoot = $canonicalPath.Equals($canonicalRepositoryRoot, $comparison)
    $isProfileRoot = $profileRoot -and $canonicalPath.Equals($profileRoot, $comparison)
    $ownedPrefix = $canonicalRoot + [IO.Path]::DirectorySeparatorChar

    if ($isOwnedRoot -or
        $isDriveRoot -or
        $isRepositoryRoot -or
        $isProfileRoot -or
        -not $canonicalPath.StartsWith($ownedPrefix, $comparison))
    {
        throw "Refusing unsafe recursive deletion target '$canonicalPath' outside owned root '$canonicalRoot'."
    }
    return $canonicalPath
}

function Invoke-GameWipOwnedTreeRemoval
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$OwnedRoot,
        [switch]$RequireMarker,
        [string]$MarkerName = '.gamewip-owned.json'
    )

    if (-not (Test-Path -LiteralPath $Path))
    {
        return
    }
    $canonicalPath = Assert-GameWipSafeChildPath -Path $Path -OwnedRoot $OwnedRoot
    if ($RequireMarker -and -not (Test-Path -LiteralPath (Join-Path $canonicalPath $MarkerName) -PathType Leaf))
    {
        throw "Refusing to delete unmarked GameWIP tree '$canonicalPath'."
    }
    Remove-Item -LiteralPath $canonicalPath -Recurse -Force
}

function Test-GameWipOperationOwnerActive
{
    param([Parameter(Mandatory = $true)]$Marker)

    if ($null -eq $Marker.processId -or [string]::IsNullOrWhiteSpace([string]$Marker.processStartTime))
    {
        return $null
    }

    try
    {
        $process = Get-Process -Id ([int]$Marker.processId) -ErrorAction Stop
        $expected = [DateTimeOffset]::Parse([string]$Marker.processStartTime).UtcDateTime
        $actual = $process.StartTime.ToUniversalTime()
        return [Math]::Abs(($actual - $expected).TotalSeconds) -lt 1.0
    }
    catch [Microsoft.PowerShell.Commands.ProcessCommandException]
    {
        return $false
    }
    catch
    {
        return $null
    }
}

function Invoke-GameWipStaleOperationTempCleanup
{
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    $cutoff = (Get-Date).ToUniversalTime().AddDays(-1)
    foreach ($directory in @(Get-ChildItem -LiteralPath $tempRoot -Directory -ErrorAction SilentlyContinue))
    {
        if ($directory.LastWriteTimeUtc -ge $cutoff)
        {
            continue
        }

        $markerPath = Join-Path $directory.FullName '.gamewip-owned.json'
        if (-not (Test-Path -LiteralPath $markerPath -PathType Leaf))
        {
            continue
        }
        try
        {
            $marker = Get-Content -Raw -LiteralPath $markerPath | ConvertFrom-Json
        }
        catch
        {
            continue
        }
        if ($marker.schemaVersion -ne 1 -or $marker.owner -ne 'GameWIP' -or $marker.resource -ne 'operation-temp')
        {
            continue
        }

        $ownerActive = Test-GameWipOperationOwnerActive -Marker $marker
        if ($ownerActive -ne $false)
        {
            # Active, malformed, and ambiguous ownership are all preserved.
            continue
        }
        Invoke-GameWipOwnedTreeRemoval -Path $directory.FullName -OwnedRoot $tempRoot -RequireMarker
    }
}

function Initialize-GameWipOperationTemp
{
    Initialize-GameWipStorage
    Invoke-GameWipStaleOperationTempCleanup
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp

    # Keep the unique segment short enough to leave Win32 path budget for deep
    # validation fixtures while retaining process and random identity.
    $operationId = '{0}-{1}' -f $PID, ([guid]::NewGuid().ToString('N').Substring(0, 12))
    $operationPath = Join-Path $tempRoot $operationId
    New-Item -ItemType Directory -Path $operationPath | Out-Null
    $processStartTime = (Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
    [ordered]@{
        schemaVersion = 1
        owner = 'GameWIP'
        resource = 'operation-temp'
        processId = $PID
        processStartTime = $processStartTime
        createdAt = (Get-Date).ToUniversalTime().ToString('o')
    } | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operationPath '.gamewip-owned.json') -Encoding UTF8
    return $operationPath
}

function Complete-GameWipOperationTemp
{
    if ([string]::IsNullOrWhiteSpace([string]$Script:OperationTemp))
    {
        return
    }
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    Invoke-GameWipOwnedTreeRemoval -Path $Script:OperationTemp -OwnedRoot $tempRoot -RequireMarker
    $Script:OperationTemp = $null
}
