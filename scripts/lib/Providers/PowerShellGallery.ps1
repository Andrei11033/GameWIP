# GameWIP PowerShellGallery tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipPowerShellGalleryToolLatestVersion
{
    param([hashtable]$Tool)
    try
    {
        return (Find-Module -Name $Tool.provider.package -Repository PSGallery -ErrorAction Stop).Version.ToString()
    }
    catch
    {
        return $null
    }
}

function Install-GameWipPowerShellGalleryTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    if (-not $Version)
    {
        throw "PowerShell Gallery tool '$($Tool.id)' requires a version."
    }
    $root = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) 'powershell'
    $destination = Join-Path $root "$($Tool.provider.package)\$Version"
    if (Test-Path -LiteralPath $destination)
    {
        return
    }
    $archive = Join-Path $Script:OperationTemp "$($Tool.provider.package).$Version.nupkg"
    Invoke-WebRequest -Uri "https://www.powershellgallery.com/api/v2/package/$($Tool.provider.package)/$Version" -OutFile $archive -UseBasicParsing
    New-Item -ItemType Directory -Force -Path $destination | Out-Null
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [IO.Compression.ZipFile]::ExtractToDirectory($archive, $destination)
}
