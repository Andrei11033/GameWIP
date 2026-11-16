# Verified GitHub release provider.

# ------------------------------------------------------------
# Release metadata and verified installation
# ------------------------------------------------------------

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

function Install-GameWipGitHubReleaseDirectoryArchive
{
    param(
        [Parameter(Mandatory = $true)][hashtable]$Tool,
        [Parameter(Mandatory = $true)][string]$Version,
        [Parameter(Mandatory = $true)][hashtable]$Asset,
        [Parameter(Mandatory = $true)][string]$Download
    )

    # Validate the provider and archive before touching the managed install.
    if (-not (Test-GameWipWindowsHost))
    {
        throw "Tool '$($Tool.id)' is only installable on Windows."
    }

    $extractRoot = Join-Path $Script:OperationContext.Temp "extract-$($Tool.id)"
    New-Item -ItemType Directory -Path $extractRoot | Out-Null

    if (-not $Asset.archive.EndsWith('.zip', [StringComparison]::OrdinalIgnoreCase))
    {
        throw "Directory archive '$($Asset.archive)' must be a ZIP file."
    }

    Expand-Archive -LiteralPath $Download -DestinationPath $extractRoot
    $payloadDirectories = @(Get-ChildItem -LiteralPath $extractRoot -Directory -Force)

    if ($payloadDirectories.Count -ne 1 -or @(Get-ChildItem -LiteralPath $extractRoot -File -Force).Count -ne 0)
    {
        throw "Directory archive '$($Asset.archive)' must contain one top-level directory."
    }

    $payload = $payloadDirectories[0].FullName

    # Confirm the extracted tool contains every declared file.
    foreach ($relativePath in @($Tool.provider.validationFiles))
    {
        $candidatePath = Join-Path $payload ([string]$relativePath -replace '/', '\')

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
        Copy-Item -LiteralPath $payload -Destination $incoming -Recurse

        if (Test-Path -LiteralPath $toolRoot)
        {
            $backup = Join-Path $familyRoot ('.{0}.{1}.backup' -f $Version, [guid]::NewGuid().ToString('N'))
            Move-Item -LiteralPath $toolRoot -Destination $backup
        }

        # Swap the complete candidate into place and restore the previous version on failure.
        try
        {
            Move-Item -LiteralPath $incoming -Destination $toolRoot
            $incoming = $null
            $installedFiles = @($Tool.provider.validationFiles | Where-Object {
                    -not (Test-Path -LiteralPath (Join-Path $toolRoot ([string]$_ -replace '/', '\')) -PathType Leaf)
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

function Install-GameWipGitHubReleaseTool
{
    param([hashtable]$Tool, [AllowNull()][string]$Version)

    # Resolve and verify the immutable release input before creating any
    # persistent installation state.
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

    if ($Tool.provider.Contains('installMode') -and $Tool.provider.installMode -eq 'directoryArchive')
    {
        Install-GameWipGitHubReleaseDirectoryArchive -Tool $Tool -Version $Version -Asset $asset -Download $download
        return
    }

    # Stage the executable in operation-owned temporary storage so downloads,
    # extraction, and version checks cannot leave a partial managed install.
    $candidateRoot = Join-Path $Script:OperationContext.Temp "candidate-$($Tool.id)"
    New-Item -ItemType Directory -Path $candidateRoot | Out-Null
    $executableName = $null

    if ($asset.Contains('format') -and $asset.format -eq 'executable')
    {
        $executableName = if ($platformKey -eq 'windows-amd64')
        {
            "$($Tool.id).exe"
        }
        else
        {
            [string]$Tool.id
        }
        Copy-Item -LiteralPath $download -Destination (Join-Path $candidateRoot $executableName)
    }
    else
    {
        $extractRoot = Join-Path $Script:OperationContext.Temp "extract-$($Tool.id)"
        New-Item -ItemType Directory -Path $extractRoot | Out-Null

        if ($asset.archive.EndsWith('.zip'))
        {
            Expand-Archive -LiteralPath $download -DestinationPath $extractRoot
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

        $executableName = $binary.Name
        Copy-Item -LiteralPath $binary.FullName -Destination (Join-Path $candidateRoot $executableName)
    }

    $candidate = Join-Path $candidateRoot $executableName

    if (-not (Test-GameWipWindowsHost))
    {
        Invoke-GameWipProcess -FilePath chmod -Arguments @('+x', $candidate) -OutputMode LogOnly -TimeoutSeconds 10 | Out-Null
    }
    $candidateVersion = Get-GameWipToolCandidateVersion -Tool $Tool -Path $candidate

    if ([string]$candidateVersion -ne $Version)
    {
        throw "Staged '$($Tool.id)' reported version '$candidateVersion'; expected '$Version'."
    }

    # Replace the versioned tool directory and shim as one guarded mutation.
    Initialize-GameWipManagedToolRoot
    $managedRoot = Get-GameWipManagedToolRoot
    $toolsRoot = Join-Path $managedRoot 'tools'
    $familyRoot = Join-Path $toolsRoot $Tool.id
    $toolRoot = Join-Path $familyRoot $Version
    $incoming = Join-Path $toolsRoot ('.{0}.{1}.{2}.incoming' -f $Tool.id, $Version, [guid]::NewGuid().ToString('N'))
    $shimPath = Join-Path (Join-Path $managedRoot 'bin') $(if (Test-GameWipWindowsHost)
        {
            "$($Tool.id).cmd"
        }
        else
        {
            [string]$Tool.id
        })
    $shimBackup = $null
    $backup = $null
    $familyCreated = $false
    $shimReplacementAttempted = $false

    try
    {
        New-Item -ItemType Directory -Path $incoming | Out-Null
        Copy-Item -LiteralPath $candidate -Destination (Join-Path $incoming $executableName)

        if (-not (Test-Path -LiteralPath $familyRoot -PathType Container))
        {
            Set-GameWipMutationStarted
            New-Item -ItemType Directory -Path $familyRoot | Out-Null
            $familyCreated = $true
        }

        if (Test-Path -LiteralPath $toolRoot)
        {
            $backup = Join-Path $familyRoot ('.{0}.{1}.backup' -f $Version, [guid]::NewGuid().ToString('N'))
            Set-GameWipMutationStarted
            Move-Item -LiteralPath $toolRoot -Destination $backup
        }
        else
        {
            Set-GameWipMutationStarted
        }

        try
        {
            # Move the verified payload into place before changing the shim;
            # this keeps the previous shim usable until the new binary exists.
            Move-Item -LiteralPath $incoming -Destination $toolRoot
            $incoming = $null

            if (Test-Path -LiteralPath $shimPath -PathType Leaf)
            {
                $shimBackup = Join-Path (Split-Path -Parent $shimPath) ('.{0}.{1}.backup' -f $Tool.id, [guid]::NewGuid().ToString('N'))
                Copy-Item -LiteralPath $shimPath -Destination $shimBackup
            }
            $shimReplacementAttempted = $true
            Write-GameWipManagedToolShim -ToolId $Tool.id -Version $Version -ExecutableName $executableName
            $installedVersion = Get-GameWipToolCandidateVersion -Tool $Tool -Path $shimPath

            if ([string]$installedVersion -ne $Version)
            {
                throw "Installed '$($Tool.id)' reported version '$installedVersion'; expected '$Version'."
            }
        }

        catch
        {
            if ($shimReplacementAttempted -and $null -ne $shimBackup -and (Test-Path -LiteralPath $shimBackup))
            {
                Copy-Item -LiteralPath $shimBackup -Destination $shimPath -Force
                Remove-Item -LiteralPath $shimBackup -Force
                $shimBackup = $null
            }
            elseif ($shimReplacementAttempted -and (Test-Path -LiteralPath $shimPath))
            {
                Remove-Item -LiteralPath $shimPath -Force
            }

            if (Test-Path -LiteralPath $toolRoot)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $toolRoot -OwnedRoot $familyRoot -SuppressMutationTracking
            }

            if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
            {
                Move-Item -LiteralPath $backup -Destination $toolRoot
                $backup = $null
            }

            if ($familyCreated -and (Test-Path -LiteralPath $familyRoot) -and @(Get-ChildItem -LiteralPath $familyRoot -Force).Count -eq 0)
            {
                Remove-Item -LiteralPath $familyRoot -Force
            }
            throw
        }

        if ($null -ne $shimBackup -and (Test-Path -LiteralPath $shimBackup))
        {
            Remove-Item -LiteralPath $shimBackup -Force
            $shimBackup = $null
        }

        if ($null -ne $backup -and (Test-Path -LiteralPath $backup))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $familyRoot -SuppressMutationTracking
            $backup = $null
        }
    }

    finally
    {
        # Remove only operation-owned staging and temporary shim state. A failed
        # replacement keeps the verified previous installation available.
        if ($null -ne $incoming -and (Test-Path -LiteralPath $incoming))
        {
            Invoke-GameWipOwnedTreeRemoval -Path $incoming -OwnedRoot $toolsRoot -SuppressMutationTracking
        }

        if (-not $shimReplacementAttempted -and $null -ne $shimBackup -and (Test-Path -LiteralPath $shimBackup))
        {
            Remove-Item -LiteralPath $shimBackup -Force -ErrorAction SilentlyContinue
        }
    }
}
