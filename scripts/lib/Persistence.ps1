# Atomic text persistence and ownership-marker contracts.

# ------------------------------------------------------------
# Text, JSON, and ownership persistence
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Read-GameWipUtf8Text
{
    param([Parameter(Mandatory = $true)][string]$Path)

    $fullPath = [IO.Path]::GetFullPath($Path)
    $encoding = [Text.UTF8Encoding]::new($false, $true)
    return [IO.File]::ReadAllText($fullPath, $encoding)
}

function Write-GameWipTextAtomic
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content,
        [switch]$SuppressMutationTracking
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $directory = Split-Path -Parent $fullPath

    if (-not (Test-Path -LiteralPath $directory -PathType Container))
    {
        if (Test-Path -LiteralPath $directory)
        {
            throw "Atomic write parent path is not a directory: '$directory'."
        }

        if (-not $SuppressMutationTracking)
        {
            Set-GameWipMutationStarted
        }

        New-Item -ItemType Directory -Path $directory | Out-Null
    }

    # Keep the temporary file beside the destination for a same-volume replace.
    $temporaryPath = Join-Path $directory (
        '.{0}.{1}.tmp' -f
        [IO.Path]::GetFileName($fullPath),
        [guid]::NewGuid().ToString('N')
    )

    try
    {
        [IO.File]::WriteAllText($temporaryPath, $Content, [Text.UTF8Encoding]::new($false))

        # Replace an existing file atomically when the filesystem supports it.
        if (Test-Path -LiteralPath $fullPath -PathType Leaf)
        {
            try
            {
                if (-not $SuppressMutationTracking)
                {
                    Set-GameWipMutationStarted
                }
                [IO.File]::Replace($temporaryPath, $fullPath, $null, $true)
                $temporaryPath = $null
                return
            }

            catch [PlatformNotSupportedException]
            {
                $null = $_.Exception
            }

            catch [System.IO.IOException]
            {
                $null = $_.Exception
            }

            catch [System.ArgumentException]
            {
                $null = $_.Exception
            }
        }

        if (-not $SuppressMutationTracking)
        {
            Set-GameWipMutationStarted
        }
        Move-Item -LiteralPath $temporaryPath -Destination $fullPath -Force
        $temporaryPath = $null
    }

    finally
    {
        if (-not [string]::IsNullOrWhiteSpace([string]$temporaryPath))
        {
            Remove-Item -LiteralPath $temporaryPath -Force -ErrorAction SilentlyContinue
        }
    }
}

function Write-GameWipJsonAtomic
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)]$Value,
        [ValidateRange(2, 100)][int]$Depth = 20
    )

    $content = ($Value | ConvertTo-Json -Depth $Depth) + "`n"
    Write-GameWipTextAtomic -Path $Path -Content $content
}

# ------------------------------------------------------------
# Ownership markers
# ------------------------------------------------------------

function New-GameWipOwnershipMarker
{
    param(
        [Parameter(Mandatory = $true)][string]$Resource,
        [ValidateSet('created', 'claimedEmpty', 'claimedProjectOwned', 'adopted')][string]$Origin = 'created',
        [AllowNull()]$Payload = $null,
        [switch]$IncludeProcess
    )

    $marker = [ordered]@{
        schemaVersion = 1
        owner = 'GameWIP'
        resource = $Resource

        origin = $Origin
        recordedAt = (Get-Date).ToUniversalTime().ToString('o')
    }

    if ($IncludeProcess)
    {
        $marker.process = [ordered]@{
            id = $PID
            startedAt = (Get-Process -Id $PID).StartTime.ToUniversalTime().ToString('o')
        }
    }

    if ($null -ne $Payload)
    {
        $marker.payload = $Payload
    }

    return $marker
}

function Read-GameWipOwnershipMarker
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][string]$Resource
    )

    try
    {
        $attributes = Get-GameWipFileSystemAttributes -Path $Path
    }

    catch
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $null; Reason = 'marker could not be inspected' }
    }

    if ($null -eq $attributes)
    {
        return [pscustomobject]@{ Status = 'missing'; Marker = $null; Reason = 'marker does not exist' }
    }

    if (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -or
        ($attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0)
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $null; Reason = 'marker is not a plain file' }
    }

    try
    {
        $marker = Get-Content -Raw -LiteralPath $Path | ConvertFrom-Json
    }

    catch
    {
        return [pscustomobject]@{ Status = 'malformed'; Marker = $null; Reason = $_.Exception.Message }
    }

    $properties = @($marker.PSObject.Properties.Name)

    if (
        'schemaVersion' -notin $properties -or
        'owner' -notin $properties -or
        'resource' -notin $properties
    )
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'marker identity fields are incomplete' }
    }

    if ($marker.schemaVersion -ne 1 -or $marker.owner -ne 'GameWIP' -or $marker.resource -ne $Resource)
    {
        return [pscustomobject]@{
            Status = 'unknown'
            Marker = $marker
            Reason = 'marker identity does not match the requested GameWIP resource'
        }
    }

    if ('origin' -notin $properties)
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'ownership origin is missing or unsupported' }
    }

    if ($marker.origin -notin @('created', 'claimedEmpty', 'claimedProjectOwned', 'adopted'))
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'ownership origin is missing or unsupported' }
    }

    return [pscustomobject]@{ Status = 'valid'; Marker = $marker; Reason = '' }
}

# ------------------------------------------------------------
# Operation temporary directories
# ------------------------------------------------------------

function Test-GameWipOperationOwnerActive
{
    param([Parameter(Mandatory = $true)]$Marker)

    if (
        $null -eq $Marker.process -or
        $null -eq $Marker.process.id -or
        [string]::IsNullOrWhiteSpace([string]$Marker.process.startedAt)
    )
    {
        return $null
    }

    try
    {
        $process = Get-Process -Id ([int]$Marker.process.id) -ErrorAction Stop
        $expected = [DateTimeOffset]::Parse([string]$Marker.process.startedAt).UtcDateTime
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
    $tempRoot = Get-GameWipStorageAreaPath -Area temp
    $cutoff = (Get-Date).ToUniversalTime().AddDays(-1)

    # Remove only old directories with a valid marker and inactive owner.
    foreach ($directory in @(Get-ChildItem -LiteralPath $tempRoot -Directory -ErrorAction SilentlyContinue))
    {
        if ($directory.LastWriteTimeUtc -ge $cutoff)
        {
            continue
        }

        $markerPath = Join-Path $directory.FullName '.gamewip-owned.json'
        $state = Read-GameWipOwnershipMarker -Path $markerPath -Resource 'operation-temp'

        if ($state.Status -ne 'valid')
        {
            continue
        }

        $ownerActive = Test-GameWipOperationOwnerActive -Marker $state.Marker

        if ($ownerActive -ne $false)
        {
            continue
        }
        Invoke-GameWipOwnedTreeRemoval `
            -Path $directory.FullName `
            -OwnedRoot $tempRoot `
            -RequireMarker `
            -SuppressMutationTracking
    }
}

function Initialize-GameWipOperationTemp
{
    # Prepare storage and discard only verified stale operation directories.
    Initialize-GameWipStorage
    Invoke-GameWipStaleOperationTempCleanup

    # Create an owned directory and persist its process identity for later cleanup.
    $tempRoot = Get-GameWipStorageAreaPath -Area temp
    $operationId = '{0}-{1}' -f $PID, ([guid]::NewGuid().ToString('N').Substring(0, 12))
    $operationPath = Join-Path $tempRoot $operationId
    New-Item -ItemType Directory -Path $operationPath | Out-Null
    $marker = New-GameWipOwnershipMarker -Resource 'operation-temp' -Origin created -IncludeProcess
    Write-GameWipJsonAtomic -Path (Join-Path $operationPath '.gamewip-owned.json') -Value $marker
    return $operationPath
}
