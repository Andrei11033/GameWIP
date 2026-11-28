# ------------------------------------------------------------
# Linux managed CMake installation
# ------------------------------------------------------------

function Install-GameWipLinuxCMake
{
    # Reuse a compatible installation before selecting an archive.
    $tool = Get-GameWipProjectTool -Id cmake
    $status = Get-GameWipToolStatus -Tool $tool

    if ($status.Ready)
    {
        Write-GameWipStatusLine -Status ready -Text "CMake $($status.Detected.Version)" -Semantic Success -Indent 2
        return
    }

    # Select and download the pinned host archive.
    $assetKey = Get-GameWipHostAssetKey
    $release = $LinuxSetupConfig.cmake.releases[[string]$tool.requiredVersion]

    if ([string]::IsNullOrWhiteSpace($assetKey) -or $null -eq $release -or -not $release.ContainsKey($assetKey))
    {
        throw (
            "No verified Linux CMake archive is configured for version '{0}' and host '{1}'." -f
            $tool.requiredVersion,
            $assetKey
        )
    }

    $asset = $release[$assetKey]
    $download = Join-Path $Script:OperationContext.Temp $asset.archive
    $uri = 'https://github.com/{0}/releases/download/v{1}/{2}' -f `
        $LinuxSetupConfig.cmake.repository, $tool.requiredVersion, $asset.archive
    $downloadResult = Invoke-GameWipDownload -Uri $uri -OutFile $download -Label "CMake $($tool.requiredVersion)"

    if ($downloadResult.State -ne 'resolved')
    {
        throw "CMake download failed: $($downloadResult.Reason)"
    }

    # Verify the archive before extracting its contents.
    if ((Get-GameWipFileSha256 -Path $download) -ne [string]$asset.sha256)
    {
        throw "SHA256 mismatch for '$($asset.archive)'."
    }

    # Extract and validate the archive before replacing the managed version.
    $extractRoot = Join-Path $Script:OperationContext.Temp 'extract-cmake'
    New-Item -ItemType Directory -Path $extractRoot | Out-Null
    Invoke-GameWipProviderNative `
        -Name 'extract-cmake' `
        -FilePath tar `
        -Arguments @('-xzf', $download, '-C', $extractRoot) | Out-Null
    $payloadDirectories = @(Get-ChildItem -LiteralPath $extractRoot -Directory -Force)

    if ($payloadDirectories.Count -ne 1 -or @(Get-ChildItem -LiteralPath $extractRoot -File -Force).Count -ne 0)
    {
        throw "CMake archive '$($asset.archive)' must contain one top-level directory."
    }

    $payload = $payloadDirectories[0].FullName

    foreach ($relativePath in @($asset.validationFiles))
    {
        $candidate = Join-Path $payload ([string]$relativePath.Replace('/', [IO.Path]::DirectorySeparatorChar))

        if (-not (Test-Path -LiteralPath $candidate -PathType Leaf))
        {
            throw "CMake archive is missing required file '$relativePath'."
        }
    }

    $managedRoot = Get-GameWipManagedToolRoot
    $toolRoot = Join-Path $managedRoot 'cmake'
    $incomingName = '.cmake.{0}.{1}.incoming' -f $tool.requiredVersion, [guid]::NewGuid().ToString('N')
    $incoming = Join-Path $managedRoot $incomingName
    $backup = $null
    New-Item -ItemType Directory -Force -Path $managedRoot | Out-Null

    # Stage the new version and retain the current one for rollback.
    try
    {
        Set-GameWipMutationStarted
        Move-Item -LiteralPath $payload -Destination $incoming
        $marker = [ordered]@{
            schemaVersion = 1
            toolId = 'cmake'
            version = [string]$tool.requiredVersion
            archive = [string]$asset.archive
            sha256 = [string]$asset.sha256
        }
        Write-GameWipJsonAtomic -Path (Join-Path $incoming '.gamewip-tool.json') -Value $marker

        if (Test-Path -LiteralPath $toolRoot)
        {
            $markerPath = Join-Path $toolRoot '.gamewip-tool.json'

            if (-not (Test-GameWipPlainFile -Path $markerPath))
            {
                throw "Refusing to replace an unowned CMake directory: '$toolRoot'."
            }

            try
            {
                $existingMarker = Read-GameWipUtf8Text -Path $markerPath | ConvertFrom-Json

                if ($existingMarker.schemaVersion -ne 1 -or $existingMarker.toolId -ne 'cmake')
                {
                    throw 'Invalid managed CMake marker.'
                }
            }

            catch
            {
                throw "Refusing to replace CMake directory with invalid ownership metadata: '$toolRoot'."
            }
            $backupName = '.{0}.{1}.backup' -f $tool.requiredVersion, [guid]::NewGuid().ToString('N')
            $backup = Join-Path $managedRoot $backupName
            Move-Item -LiteralPath $toolRoot -Destination $backup
        }

        # Verify the staged CMake before removing the previous version.
        try
        {
            Move-Item -LiteralPath $incoming -Destination $toolRoot
            $installedFiles = @($asset.validationFiles | Where-Object {
                    $relativePath = [string]$_.Replace('/', [IO.Path]::DirectorySeparatorChar)
                    -not (Test-Path -LiteralPath (Join-Path $toolRoot $relativePath) -PathType Leaf)
                })

            if ($installedFiles.Count -ne 0)
            {
                throw "Installed CMake is incomplete: $($installedFiles -join ', ')."
            }

            $cmakeExecutable = Join-Path $toolRoot 'bin/cmake'
            $versionOutput = Invoke-GameWipProcess `
                -FilePath $cmakeExecutable `
                -Arguments @('--version') `
                -OutputMode LogOnly `
                -TimeoutSeconds 20
            $installedVersion = Get-GameWipToolCandidateVersion -Tool $tool -Path $cmakeExecutable

            if ($versionOutput.ExitCode -ne 0 -or
                $null -eq $installedVersion -or
                [version]$installedVersion -lt [version]$tool.requiredVersion)
            {
                throw (
                    "Installed CMake '{0}' does not satisfy the minimum version '{1}'." -f
                    $installedVersion,
                    $tool.requiredVersion
                )
            }
        }

        catch
        {
            # Restore the previous version if validation fails.
            if (Test-Path -LiteralPath $toolRoot)
            {
                Invoke-GameWipOwnedTreeRemoval -Path $toolRoot -OwnedRoot $managedRoot -SuppressMutationTracking
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
            Invoke-GameWipOwnedTreeRemoval -Path $backup -OwnedRoot $managedRoot -SuppressMutationTracking
        }
        Add-GameWipOperationChange -Message "Installed CMake $($tool.requiredVersion)"
    }

    finally
    {
        if (Test-Path -LiteralPath $incoming)
        {
            try
            {
                Invoke-GameWipOwnedTreeRemoval `
                    -Path $incoming `
                    -OwnedRoot $managedRoot `
                    -SuppressMutationTracking
            }

            catch
            {
                $null = $_.Exception
            }
        }
    }
}
