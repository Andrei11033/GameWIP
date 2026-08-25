# GameWIP atomic persistence and common ownership-marker contracts.

Set-StrictMode -Version Latest

function Write-GameWipTextAtomic
{
    param(
        [Parameter(Mandatory = $true)][string]$Path,
        [Parameter(Mandatory = $true)][AllowEmptyString()][string]$Content
    )

    $fullPath = [IO.Path]::GetFullPath($Path)
    $directory = Split-Path -Parent $fullPath
    New-Item -ItemType Directory -Force -Path $directory | Out-Null
    $temporaryPath = Join-Path $directory ('.{0}.{1}.tmp' -f ([IO.Path]::GetFileName($fullPath)), [guid]::NewGuid().ToString('N'))
    try
    {
        [IO.File]::WriteAllText($temporaryPath, $Content, [Text.UTF8Encoding]::new($false))
        if (Test-Path -LiteralPath $fullPath -PathType Leaf)
        {
            try
            {
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

function New-GameWipOwnershipMarker
{
    param(
        [Parameter(Mandatory = $true)][string]$Resource,
        [ValidateSet('created', 'claimedEmpty', 'adopted')][string]$Origin = 'created',
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

    if (-not (Test-Path -LiteralPath $Path -PathType Leaf))
    {
        return [pscustomobject]@{ Status = 'missing'; Marker = $null; Reason = 'marker does not exist' }
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
    if ('schemaVersion' -notin $properties -or 'owner' -notin $properties -or 'resource' -notin $properties)
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'marker identity fields are incomplete' }
    }
    if ($marker.schemaVersion -ne 1 -or $marker.owner -ne 'GameWIP' -or $marker.resource -ne $Resource)
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'marker identity does not match the requested GameWIP resource' }
    }
    if ('origin' -notin $properties)
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'ownership origin is missing or unsupported' }
    }
    if ($marker.origin -notin @('created', 'claimedEmpty', 'adopted'))
    {
        return [pscustomobject]@{ Status = 'unknown'; Marker = $marker; Reason = 'ownership origin is missing or unsupported' }
    }
    return [pscustomobject]@{ Status = 'valid'; Marker = $marker; Reason = '' }
}

function Test-GameWipOperationOwnerActive
{
    param([Parameter(Mandatory = $true)]$Marker)

    if ($null -eq $Marker.process -or $null -eq $Marker.process.id -or [string]::IsNullOrWhiteSpace([string]$Marker.process.startedAt))
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
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    $cutoff = (Get-Date).ToUniversalTime().AddDays(-1)
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
        Invoke-GameWipOwnedTreeRemoval -Path $directory.FullName -OwnedRoot $tempRoot -RequireMarker
    }
}

function Initialize-GameWipOperationTemp
{
    Initialize-GameWipStorage
    Invoke-GameWipStaleOperationTempCleanup
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    $operationId = '{0}-{1}' -f $PID, ([guid]::NewGuid().ToString('N').Substring(0, 12))
    $operationPath = Join-Path $tempRoot $operationId
    New-Item -ItemType Directory -Path $operationPath | Out-Null
    $marker = New-GameWipOwnershipMarker -Resource 'operation-temp' -Origin created -IncludeProcess
    Write-GameWipJsonAtomic -Path (Join-Path $operationPath '.gamewip-owned.json') -Value $marker
    return $operationPath
}
