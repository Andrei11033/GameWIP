# VS Code workflow extension packaging.

# ------------------------------------------------------------
# Build a verified VSIX package
# ------------------------------------------------------------

Set-StrictMode -Version Latest

function Invoke-GameWipVsCodeExtensionPackaging
{
    param(
        [Parameter(Mandatory = $true)][string]$ExtensionSource,
        [Parameter(Mandatory = $true)][string]$Destination
    )

    $package = Get-Content -LiteralPath (Join-Path $ExtensionSource 'package.json') -Raw | ConvertFrom-Json
    $stagingRoot = Join-Path $Script:OperationContext.Temp "gamewip-vsix-$([Guid]::NewGuid().ToString('N'))"
    $extensionRoot = Join-Path $stagingRoot 'extension'

    try
    {
        # Stage the extension files and VSIX manifests.
        New-Item -ItemType Directory -Path $extensionRoot -Force | Out-Null
        Copy-Item -Path (Join-Path $ExtensionSource '*') -Destination $extensionRoot -Recurse -Force

        $identity = "$($package.publisher).$($package.name)"
        $manifest = @"
<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011">
  <Metadata>
    <Identity Language="en-US" Id="$identity" Version="$($package.version)" Publisher="$($package.publisher)" />
    <DisplayName>$($package.displayName)</DisplayName>
    <Description xml:space="preserve">$($package.description)</Description>
    <Tags>vscode</Tags>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="$($package.engines.vscode)" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Services.Links.Source" Value="https://github.com/Andrei11033/GameWIP" />
    </Properties>
  </Metadata>
  <Installation>
    <InstallationTarget Id="Microsoft.VisualStudio.Code" />
  </Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
  </Assets>
</PackageManifest>
"@
        $contentTypes = @"
<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
  <Default Extension="json" ContentType="application/json" />
  <Default Extension="js" ContentType="application/javascript" />
  <Default Extension="md" ContentType="text/markdown" />
  <Default Extension="vsixmanifest" ContentType="text/xml" />
</Types>
"@
        [IO.File]::WriteAllText((Join-Path $stagingRoot 'extension.vsixmanifest'), $manifest, [Text.UTF8Encoding]::new($false))
        [IO.File]::WriteAllText((Join-Path $stagingRoot '[Content_Types].xml'), $contentTypes, [Text.UTF8Encoding]::new($false))

        Add-Type -AssemblyName System.IO.Compression
        Add-Type -AssemblyName System.IO.Compression.FileSystem
        New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null

        # Create the package from the staged files.
        if (Test-Path -LiteralPath $Destination)
        {
            Remove-Item -LiteralPath $Destination -Force
        }

        $archive = [IO.Compression.ZipFile]::Open($Destination, [IO.Compression.ZipArchiveMode]::Create)

        try
        {
            foreach ($file in Get-ChildItem -LiteralPath $stagingRoot -File -Recurse)
            {
                $entryName = $file.FullName.Substring($stagingRoot.Length + 1).Replace('\', '/')
                [IO.Compression.ZipFileExtensions]::CreateEntryFromFile(
                    $archive,
                    $file.FullName,
                    $entryName,
                    [IO.Compression.CompressionLevel]::Optimal
                ) | Out-Null
            }
        }

        finally
        {
            $archive.Dispose()
        }
    }

    finally
    {
        # Remove temporary package files after success or failure.
        if (Test-Path -LiteralPath $stagingRoot)
        {
            Invoke-GameWipOwnedTreeRemoval -Path $stagingRoot -OwnedRoot $Script:OperationContext.Temp -SuppressMutationTracking
        }
    }
}
