# GameWIP verified GitHub-release tool provider.

function ConvertFrom-GameWipGitHubReleaseTag
{
    param([Parameter(Mandatory = $true)][string]$Tag)
    return $Tag -replace '^v(?=[0-9])', ''
}

function Get-GameWipGitHubReleaseToolLatestVersion
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -eq 'resolved')
    {
        return [string]$query.Version
    }
    return $null
}

function Get-GameWipGitHubReleaseMetadata
{
    param([hashtable]$Tool)
    $query = Get-GameWipToolLatestQuery -Tool $Tool
    if ($query.State -ne 'resolved')
    {
        throw "GitHub release metadata for '$($Tool.id)' is $($query.State): $($query.Reason)"
    }
    return $query.Metadata
}

function Install-GameWipGitHubReleaseTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)
    if (-not $Version)
    {
        throw "GitHub release tool '$($Tool.id)' requires a version."
    }
    if (-not $Tool.provider.Contains('releaseTag'))
    {
        throw "GitHub release tool '$($Tool.id)' lacks an upstream release tag."
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
    $download = Join-Path $Script:OperationContext.Temp $asset.archive
    $uri = "https://github.com/$($Tool.provider.repository)/releases/download/$($Tool.provider.releaseTag)/$($asset.archive)"
    $downloadResult = Invoke-GameWipDownload -Uri $uri -OutFile $download -Label "$($Tool.id) $Version"
    if ($downloadResult.State -ne 'resolved')
    {
        throw "Download failed for '$($Tool.id)': $($downloadResult.Reason)"
    }
    $actualHash = Get-GameWipFileSha256 -Path $download
    if ($actualHash -ne $asset.sha256)
    {
        throw "SHA256 mismatch for '$($asset.archive)'."
    }

    Initialize-GameWipManagedToolRoot
    $toolRoot = Join-Path ((Get-GameWipManagedToolRoot)) "tools\$($Tool.id)\$Version"
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
    $extractRoot = Join-Path $Script:OperationContext.Temp "extract-$($Tool.id)"
    New-Item -ItemType Directory -Force -Path $extractRoot | Out-Null
    if ($asset.archive.EndsWith('.zip'))
    {
        Expand-Archive -LiteralPath $download -DestinationPath $extractRoot -Force
    }
    else
    {
        Invoke-GameWipProviderNative -Name "extract-$($Tool.id)" -FilePath tar -Arguments @('-xzf', $download, '-C', $extractRoot) | Out-Null
    }
    $binary = Get-ChildItem -LiteralPath $extractRoot -Recurse -File | Where-Object { $_.Name -in @($Tool.id, "$($Tool.id).exe") } | Select-Object -First 1
    if ($null -eq $binary)
    {
        throw "Release archive did not contain '$($Tool.id)'."
    }
    Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $toolRoot $binary.Name) -Force
    Write-GameWipManagedToolShim -ToolId $Tool.id -Version $Version -ExecutableName $binary.Name
}
