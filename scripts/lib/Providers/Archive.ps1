# Verified direct-archive provider.

# ------------------------------------------------------------
# Shared directory-archive installation
# ------------------------------------------------------------

function Install-GameWipVerifiedDirectoryArchive
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][hashtable]$Asset,
        [Parameter(Mandatory = $true)][string]$AssetKey,
        [Parameter(Mandatory = $true)][string]$Download
    )

    # Validate and extract the archive before touching the managed install.
    $extractRoot = Join-Path $Script:OperationContext.Temp "extract-$($Tool.id)"
    New-Item -ItemType Directory -Path $extractRoot | Out-Null
    $archiveName = [string]$Asset.archive

    if ($archiveName.EndsWith('.zip', [StringComparison]::OrdinalIgnoreCase))
    {
        Expand-Archive -LiteralPath $Download -DestinationPath $extractRoot
    }
    elseif ($archiveName.EndsWith('.tar.gz', [StringComparison]::OrdinalIgnoreCase) -or
        $archiveName.EndsWith('.tgz', [StringComparison]::OrdinalIgnoreCase))
    {
        Invoke-GameWipProviderNative -Name "extract-$($Tool.id)" -FilePath tar -Arguments @('-xzf', $Download, '-C', $extractRoot) | Out-Null
    }
    elseif ($archiveName.EndsWith('.tar.xz', [StringComparison]::OrdinalIgnoreCase))
    {
        Invoke-GameWipProviderNative -Name "extract-$($Tool.id)" -FilePath tar -Arguments @('-xJf', $Download, '-C', $extractRoot) | Out-Null
    }
    else
    {
        throw "Unsupported directory archive format '$archiveName'."
    }

    $payloadDirectories = @(Get-ChildItem -LiteralPath $extractRoot -Directory -Force)

    if ($payloadDirectories.Count -ne 1 -or @(Get-ChildItem -LiteralPath $extractRoot -File -Force).Count -ne 0)
    {
        throw "Directory archive '$($Asset.archive)' must contain one top-level directory."
    }

    $payload = $payloadDirectories[0].FullName

    # Confirm the archive matches the declared file set.
    foreach ($relativePath in @(Get-GameWipToolValidationFiles -Tool $Tool -AssetKey $AssetKey))
    {
        $candidatePath = Join-Path $payload ([string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))

        if (-not (Test-Path -LiteralPath $candidatePath -PathType Leaf))
        {
            throw "Directory archive '$($Asset.archive)' is missing required file '$relativePath'."
        }
    }

    $markerPath = Join-Path $payload '.gamewip-tool.json'
    $marker = [ordered]@{
        schemaVersion = 1
        toolId = [string]$Tool.id
        version = $Version
        archive = [string]$Asset.archive
        sha256 = [string]$Asset.sha256
    }
    Write-GameWipTextAtomic `
        -Path $markerPath `
        -Content (($marker | ConvertTo-Json -Depth 8) + "`n") `
        -SuppressMutationTracking

    # Stage the verified payload beside the managed tool family.
    Initialize-GameWipManagedToolRoot
    $managedRoot = Get-GameWipManagedToolRoot
    $familyRoot = Join-Path $managedRoot ([string]$Tool.provider.managedDirectory)
    $toolRoot = Join-Path $familyRoot $Version
    $incoming = Join-Path $familyRoot ('.{0}.{1}.incoming' -f $Version, [guid]::NewGuid().ToString('N'))
    $backup = $null

    try
    {
        Set-GameWipMutationStarted
        New-Item -ItemType Directory -Force -Path $familyRoot | Out-Null

        if (Test-GameWipWindowsHost)
        {
            Copy-Item -LiteralPath $payload -Destination $incoming -Recurse
        }
        else
        {
            Move-Item -LiteralPath $payload -Destination $incoming
        }

        if (Test-Path -LiteralPath $toolRoot)
        {
            $backup = Join-Path $familyRoot ('.{0}.{1}.backup' -f $Version, [guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $toolRoot -Destination $backup
        }

        # Replace the version only after the staged payload passes validation.
        try
        {
            Move-Item -LiteralPath $incoming -Destination $toolRoot
            $incoming = $null
            $installedFiles = @(Get-GameWipToolValidationFiles -Tool $Tool -AssetKey $AssetKey | Where-Object {
                    $relativePath = [string]$_.Replace('/', [IO.Path]::DirectorySeparatorChar)
                    -not (Test-Path -LiteralPath (Join-Path $toolRoot $relativePath) -PathType Leaf)
                })

            if ($installedFiles.Count -ne 0)
            {
                throw "Installed '$($Tool.id)' is incomplete: $($installedFiles -join ', ')."
            }
        }
        catch
        {
            if (Test-Path -LiteralPath $toolRoot)
            {
                Remove-Item -LiteralPath $toolRoot -Recurse -Force
            }

            if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
            {
                Move-Item -LiteralPath $backup -Destination $toolRoot
                $backup = $null
            }
            throw
        }

        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            Remove-Item -LiteralPath $backup -Recurse -Force
            $backup = $null
        }
    }
    finally
    {
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Remove-Item -LiteralPath $incoming -Recurse -Force -ErrorAction SilentlyContinue
        }
    }
}

function Install-GameWipArchiveTool
{
    param([Parameter(Mandatory = $true)][hashtable]$Tool, [AllowNull()][string]$Version)

    # Resolve the pinned host archive before making persistent changes.
    if ([string]::IsNullOrWhiteSpace($Version) -or $Version -ne [string]$Tool.requiredVersion)
    {
        throw "Archive tool '$($Tool.id)' requires its declared version '$($Tool.requiredVersion)'."
    }

    $assetKey = Get-GameWipHostAssetKey

    if ($null -eq $assetKey -or -not $Tool.provider.assets.ContainsKey($assetKey))
    {
        throw "Archive tool '$($Tool.id)' has no verified asset for host '$assetKey'."
    }

    $asset = $Tool.provider.assets[$assetKey]
    $download = Join-Path $Script:OperationContext.Temp $asset.archive
    $baseUrl = ([string]$Tool.provider.baseUrl).TrimEnd('/')
    $uri = "$baseUrl/$Version/$($asset.archive)"
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

    Install-GameWipVerifiedDirectoryArchive `
        -Tool $Tool `
        -Version $Version `
        -Asset $asset `
        -AssetKey $assetKey `
        -Download $download
}
