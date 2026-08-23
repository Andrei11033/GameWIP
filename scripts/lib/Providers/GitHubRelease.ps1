# GameWIP GitHubRelease tool-provider behavior. Dot-sourced by scripts/lib/Tools.ps1.
function Get-GameWipGitHubReleaseToolLatestVersion
{
    param([hashtable]$Tool)
    try
    {
        return (Get-GameWipGitHubReleaseMetadata -Tool $Tool).Version
    }
    catch
    {
        return $null
    }
}

function Get-GameWipGitHubReleaseMetadata
{
    param([hashtable]$Tool)
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$($Tool.provider.repository)/releases/latest" -Headers @{ Accept = 'application/vnd.github+json'; 'X-GitHub-Api-Version' = '2022-11-28' }
    $version = ([string]$release.tag_name).TrimStart('v')
    $assets = @{}
    foreach ($key in @($Tool.provider.assets.Keys))
    {
        $expectedName = ([string]$Tool.provider.assets[$key].archive).Replace([string]$Tool.requiredVersion, $version)
        $asset = $release.assets | Where-Object { $_.name -eq $expectedName } | Select-Object -First 1
        if ($null -eq $asset)
        {
            continue
        }
        $digestMatch = [regex]::Match([string]$asset.digest, '^sha256:([0-9a-f]{64})$')
        if ($digestMatch.Success)
        {
            $assets[$key] = @{ archive = [string]$asset.name; sha256 = $digestMatch.Groups[1].Value }
        }
    }
    return [pscustomobject]@{ Version = $version; Assets = $assets }
}

function Install-GameWipGitHubReleaseTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    if (-not $Version)
    {
        throw "GitHub release tool '$($Tool.id)' requires a version."
    }
    $platformKey = if ([Runtime.InteropServices.RuntimeInformation]::IsOSPlatform([Runtime.InteropServices.OSPlatform]::Linux))
    {
        'linux-amd64'
    }
    else
    {
        'windows-amd64'
    }
    $asset = $Tool.provider.assets[$platformKey]
    if ($null -eq $asset)
    {
        throw "No verified '$platformKey' asset is declared for '$($Tool.id)'."
    }
    $download = Join-Path $Script:OperationTemp $asset.archive
    $uri = "https://github.com/$($Tool.provider.repository)/releases/download/v$Version/$($asset.archive)"
    Invoke-WebRequest -Uri $uri -OutFile $download
    $actualHash = (Get-FileHash -LiteralPath $download -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actualHash -ne $asset.sha256)
    {
        throw "SHA256 mismatch for '$($asset.archive)'."
    }
    $toolRoot = Join-Path ([string]$ProjectConfig.managedEnvironment.gameWipToolsRoot) "tools\$($Tool.id)\$Version"
    New-Item -ItemType Directory -Force -Path $toolRoot | Out-Null
    if ($asset.Contains('format') -and $asset.format -eq 'executable')
    {
        $destinationName = if ($platformKey -eq 'windows-amd64')
        {
            "$($Tool.id).exe"
        }
        else
        {
            [string]$Tool.id
        }
        Copy-Item -LiteralPath $download -Destination (Join-Path $toolRoot $destinationName) -Force
        Write-GameWipManagedToolShim -ToolId $Tool.id -Version $Version -ExecutableName $destinationName
        return
    }
    $extractRoot = Join-Path $Script:OperationTemp "extract-$($Tool.id)"
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    if ($asset.archive.EndsWith('.zip'))
    {
        Expand-Archive -LiteralPath $download -DestinationPath $extractRoot -Force
    }
    else
    {
        & tar -xzf $download -C $extractRoot; if ($LASTEXITCODE -ne 0)
        {
            throw "Could not extract '$download'."
        }
    }
    $binary = Get-ChildItem -LiteralPath $extractRoot -Recurse -File | Where-Object { $_.Name -in @($Tool.id, "$($Tool.id).exe") } | Select-Object -First 1
    if ($null -eq $binary)
    {
        throw "Release archive did not contain '$($Tool.id)'."
    }
    Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $toolRoot $binary.Name) -Force
    Write-GameWipManagedToolShim -ToolId $Tool.id -Version $Version -ExecutableName $binary.Name
}
