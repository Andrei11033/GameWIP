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

function Assert-GameWipSafeChildPath
{
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$OwnedRoot)
    $canonicalPath = [IO.Path]::GetFullPath($Path).TrimEnd('\', '/')
    $canonicalRoot = [IO.Path]::GetFullPath($OwnedRoot).TrimEnd('\', '/')
    $driveRoot = [IO.Path]::GetPathRoot($canonicalPath).TrimEnd('\', '/')
    if ($canonicalPath -eq $canonicalRoot -or $canonicalPath -eq $driveRoot -or
        $canonicalPath -eq [IO.Path]::GetFullPath($RepositoryRoot).TrimEnd('\', '/') -or
        -not $canonicalPath.StartsWith($canonicalRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Refusing unsafe recursive deletion target '$canonicalPath' outside owned root '$canonicalRoot'."
    }
    return $canonicalPath
}

function Invoke-GameWipOwnedTreeRemoval
{
    param([Parameter(Mandatory = $true)][string]$Path, [Parameter(Mandatory = $true)][string]$OwnedRoot, [switch]$RequireMarker)
    if (-not (Test-Path -LiteralPath $Path))
    {
        return
    }
    $canonicalPath = Assert-GameWipSafeChildPath -Path $Path -OwnedRoot $OwnedRoot
    if ($RequireMarker -and -not (Test-Path -LiteralPath (Join-Path $canonicalPath '.gamewip-owned.json') -PathType Leaf))
    {
        throw "Refusing to delete unmarked GameWIP tree '$canonicalPath'."
    }
    Remove-Item -LiteralPath $canonicalPath -Recurse -Force
}

function Invoke-GameWipStaleOperationTempCleanup
{
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    $cutoff = (Get-Date).ToUniversalTime().AddDays(-1)
    foreach ($directory in @(Get-ChildItem -LiteralPath $tempRoot -Directory -ErrorAction SilentlyContinue))
    {
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
        if ($marker.owner -eq 'GameWIP' -and $marker.resource -eq 'operation-temp' -and $directory.LastWriteTimeUtc -lt $cutoff)
        {
            Invoke-GameWipOwnedTreeRemoval -Path $directory.FullName -OwnedRoot $tempRoot -RequireMarker
        }
    }
}

function Initialize-GameWipOperationTemp
{
    Initialize-GameWipStorage
    Invoke-GameWipStaleOperationTempCleanup
    $tempRoot = Resolve-GameWipStoragePath -RelativePath $ProjectConfig.storage.temp
    # Keep this unique segment short enough to leave Win32 path budget for
    # deep validation fixtures while retaining process and random identity.
    $operationId = '{0}-{1}' -f $PID, ([guid]::NewGuid().ToString('N').Substring(0, 12))
    $operationPath = Join-Path $tempRoot $operationId
    New-Item -ItemType Directory -Path $operationPath | Out-Null
    [ordered]@{ schemaVersion = 1; owner = 'GameWIP'; resource = 'operation-temp'; processId = $PID; createdAt = (Get-Date).ToUniversalTime().ToString('o') } |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $operationPath '.gamewip-owned.json') -Encoding UTF8
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
