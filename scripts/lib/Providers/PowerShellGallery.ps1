# GameWIP PowerShell Gallery tool provider.

function Get-GameWipPowerShellGalleryToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Install-GameWipPowerShellGalleryTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    if (-not $Version)
    {
        throw "PowerShell Gallery tool '$($Tool.id)' requires a version."
    }
    $root = Join-Path ((Get-GameWipManagedToolRoot)) 'powershell'
    $destination = Join-Path $root "$($Tool.provider.package)\$Version"
    $manifestName = "$($Tool.provider.package).psd1"
    if ((Test-Path -LiteralPath $destination -PathType Container) -and
        (Test-Path -LiteralPath (Join-Path $destination $manifestName) -PathType Leaf))
    {
        return
    }
    $archive = Join-Path $Script:OperationContext.Temp "$($Tool.provider.package).$Version.nupkg"
    $download = Invoke-GameWipDownload -Uri "https://www.powershellgallery.com/api/v2/package/$($Tool.provider.package)/$Version" -OutFile $archive -Label "$($Tool.provider.package) $Version"
    if ($download.State -ne 'resolved')
    {
        throw "PowerShell Gallery download failed: $($download.Reason)"
    }
    $destinationParent = Split-Path -Parent $destination
    $staging = Join-Path $Script:OperationContext.Temp ('.{0}.{1}.{2}.incoming' -f $Tool.provider.package, $Version, [guid]::NewGuid().ToString('N'))
    $incoming = $null
    $backup = $null
    $destinationParentCreated = $false
    try
    {
        New-Item -ItemType Directory -Path $staging | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $staging)
        if (-not (Test-Path -LiteralPath (Join-Path $staging $manifestName) -PathType Leaf))
        {
            throw "PowerShell Gallery package '$($Tool.provider.package) $Version' did not contain the expected module manifest '$manifestName'."
        }
        Initialize-GameWipManagedToolRoot
        if (-not (Test-Path -LiteralPath $destinationParent -PathType Container))
        {
            Set-GameWipMutationStarted
            New-Item -ItemType Directory -Path $destinationParent | Out-Null
            $destinationParentCreated = $true
        }
        $incoming = Join-Path $destinationParent ('.{0}.{1}.incoming' -f $Version, [guid]::NewGuid().ToString('N'))
        Copy-Item -LiteralPath $staging -Destination $incoming -Recurse
        if (-not (Test-Path -LiteralPath (Join-Path $incoming $manifestName) -PathType Leaf))
        {
            throw "Staged PowerShell module '$($Tool.provider.package) $Version' failed same-volume verification."
        }
        if (Test-Path -LiteralPath $destination)
        {
            $backup = Join-Path $destinationParent ('.{0}.{1}.backup' -f $Version, [guid]::NewGuid().ToString('N'))
            Set-GameWipMutationStarted
            Move-Item -LiteralPath $destination -Destination $backup
        }
        else
        {
            Set-GameWipMutationStarted
        }
        try
        {
            Move-Item -LiteralPath $incoming -Destination $destination
            $incoming = $null
        }
        catch
        {
            if ($null -ne $backup -and (Test-Path -LiteralPath $backup) -and -not (Test-Path -LiteralPath $destination))
            {
                Move-Item -LiteralPath $backup -Destination $destination
                $backup = $null
            }
            throw
        }
        if (-not (Test-Path -LiteralPath (Join-Path $destination $manifestName) -PathType Leaf))
        {
            if (Test-Path -LiteralPath $destination)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $destination -OwnedRoot $destinationParent -SuppressMutationTracking
            }
            if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
            {
                Move-Item -LiteralPath $backup -Destination $destination
                $backup = $null
            }
            throw "Installed PowerShell module '$($Tool.provider.package) $Version' failed post-swap verification."
        }
        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $destinationParent -SuppressMutationTracking
            $backup = $null
        }
        Add-GameWipOperationChange -Message "Installed PowerShell module $($Tool.provider.package) $Version."
    }
    finally
    {
        if (-not [string]::IsNullOrWhiteSpace([string]$staging) -and (Test-Path -LiteralPath $staging))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $staging -OwnedRoot $Script:OperationContext.Temp -SuppressMutationTracking
        }
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $incoming -OwnedRoot $destinationParent -SuppressMutationTracking
        }
        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            if (-not (Test-Path -LiteralPath $destination))
            {
                Move-Item -LiteralPath $backup -Destination $destination
                $backup = $null
            }
        }
        if ($destinationParentCreated -and (Test-Path -LiteralPath $destinationParent) -and @(Get-ChildItem -LiteralPath $destinationParent -Force).Count -eq 0)
        {
            Remove-Item -LiteralPath $destinationParent -Force -ErrorAction SilentlyContinue
        }
    }
}
