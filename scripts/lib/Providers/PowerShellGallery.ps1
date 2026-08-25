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
    if (Test-Path -LiteralPath $destination)
    {
        return
    }
    $archive = Join-Path $Script:OperationContext.Temp "$($Tool.provider.package).$Version.nupkg"
    $download = Invoke-GameWipDownload -Uri "https://www.powershellgallery.com/api/v2/package/$($Tool.provider.package)/$Version" -OutFile $archive -Label "$($Tool.provider.package) $Version"
    if ($download.State -ne 'resolved')
    {
        throw "PowerShell Gallery download failed: $($download.Reason)"
    }
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination)
}
