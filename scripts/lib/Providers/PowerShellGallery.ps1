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
    Initialize-GameWipManagedToolRoot
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
    New-Item -ItemType Directory -Force -Path $destinationParent | Out-Null
    $staging = Join-Path $destinationParent ('.{0}.{1}.incoming' -f $Version, [guid]::NewGuid().ToString('N'))
    try
    {
        New-Item -ItemType Directory -Path $staging | Out-Null
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $staging)
        if (-not (Test-Path -LiteralPath (Join-Path $staging $manifestName) -PathType Leaf))
        {
            throw "PowerShell Gallery package '$($Tool.provider.package) $Version' did not contain the expected module manifest '$manifestName'."
        }
        if (Test-Path -LiteralPath $destination)
        {
            Set-GameWipMutationStarted
            Invoke-GameWipOwnedTreeRemoval -Path $destination -OwnedRoot $destinationParent
        }
        Set-GameWipMutationStarted
        Move-Item -LiteralPath $staging -Destination $destination
        $staging = $null
        Add-GameWipOperationChange -Message "Installed PowerShell module $($Tool.provider.package) $Version."
    }
    finally
    {
        if (-not [string]::IsNullOrWhiteSpace([string]$staging) -and (Test-Path -LiteralPath $staging))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $staging -OwnedRoot $destinationParent
        }
    }
}
